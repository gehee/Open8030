/* SPDX-License-Identifier: MIT */
#include "ar_channel_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_channel_policy state;
static uint32_t rows[8];
static uint8_t groups[8], mode;
static int32_t scores[8];
static int phy_result, post_result;
static unsigned locks, events, sends, timers, posts;
static char trace[32];
static uint8_t phy_payload[4], backup_payload[8];
static void note(char event) { assert(events < sizeof(trace)); trace[events++] = event; }
static void lock(enum ar_channel_policy_lock which, void *context)
{
	assert(context == &state && locks == (unsigned)which);
	locks++; note(which ? 'a' : 'p');
}
static void unlock(enum ar_channel_policy_lock which, void *context)
{
	assert(context == &state && locks == (unsigned)which + 1);
	locks--; note(which ? 'A' : 'P');
}
static int phy(unsigned command, unsigned ttl, const uint8_t *payload,
	size_t length, void *context)
{
	assert(context == &state && locks == 2 && command == 8 && ttl == 8 && length == 4);
	memcpy(phy_payload, payload, 4); note('f');
	return phy_result;
}
static int control(unsigned slot, unsigned command, const uint8_t *payload,
	size_t length, void *context)
{
	assert(context == &state && locks == 2 && command == 10 && length == 8);
	assert(state.role ? slot == 0 : (state.slots & (1u << slot)));
	memcpy(backup_payload, payload, 8); sends++; note('c');
	return -6;
}
static void timer(uint32_t milliseconds, void *context)
{
	assert(context == &state && locks == 2 && milliseconds == 3000);
	assert(!(state.auxiliary_flags & 16)); timers++; note('t');
}
static int post(uint32_t destination, uint32_t message, const uint8_t *payload,
	size_t length, uint32_t timeout, void *context)
{
	assert(context == &state && locks == 1 && destination == 1 && message == 0x108);
	assert(length == 8 && timeout == UINT32_MAX && payload[0] == 1);
	assert((payload[4] | (unsigned)payload[5] << 8) == state.request.pending_channel);
	assert(state.request.flags & 8); posts++; note('q');
	return post_result;
}
static const struct ar_channel_policy_ops ops = {lock, unlock, phy, control, timer, post};
static void reset(void)
{
	state = (struct ar_channel_policy){.channel = 7, .auxiliary_channel = 4,
		.transmit_channel = 0x1256, .mode = 1, .group = 3, .role = 1};
	state.request.flags = 4;
	memset(trace, 0, sizeof(trace)); memset(phy_payload, 0, sizeof(phy_payload));
	locks = events = sends = timers = posts = 0;
	phy_result = post_result = 0;
}
static int apply(const uint32_t *channel)
{
	return ar_channel_policy_set(&state, rows, groups, scores, 8, &mode,
		channel, &ops, &state);
}
int main(void)
{
	for (unsigned i = 0; i < 8; i++) {
		rows[i] = 0x01000000 | (i % 3) << 27; groups[i] = i % 6; scores[i] = -100 - (int)i;
	}
	reset(); mode = 0; uint32_t channel = 0x10007;
	post_result = -6;
	assert(!apply(&channel) && posts == 1 && state.request.pending_channel == 7);
	assert(!state.mode && state.request.flags == 8 && state.channel == 7);
	assert(!strcmp(trace, "paAqP"));
	reset(); mode = 255; channel = 7;
	assert(!apply(&channel) && !posts && state.mode == 1 && state.request.flags == 4);
	assert(!strcmp(trace, "paAP"));
	reset(); state.auxiliary_flags = 6; mode = 0; channel = 2;
	assert(!apply(&channel) && !posts && !state.mode && !state.request.flags);
	assert(state.channel == 4 && state.band == 1 && state.group == 4);
	assert(state.auxiliary_flags == 0x26 && phy_payload[0] == 0x24 && phy_payload[1] == 8);
	assert(!phy_payload[2] && !phy_payload[3] && !strcmp(trace, "pafAP"));
	reset(); state.auxiliary_flags = 6; mode = 0; phy_result = -6;
	assert(apply(NULL) == -6 && state.channel == 7 && state.mode == 1);
	assert(state.auxiliary_flags == 6 && !posts);
	reset(); state.auxiliary_flags = 6; mode = 0; phy_result = -2; channel = 2;
	assert(!apply(&channel) && posts == 1 && !state.mode && state.channel == 7);
	assert(state.request.flags == 0x28 && !strcmp(trace, "pafAqP"));
	reset(); state.auxiliary_flags = 4; mode = 2;
	assert(!apply(NULL) && state.mode == 2 && state.auxiliary_flags == 0x16);
	assert(sends == 1 && timers == 1 && !posts && !strcmp(trace, "pactAP"));
	for (unsigned i = 0; i < 4; i++) {
		assert(state.backups[i] == (0x107 - i));
		assert(backup_payload[2*i] == 7-i && backup_payload[2*i+1] == 1);
	}
	reset(); state.auxiliary_flags = 6; state.role = 0; state.slots = 0x81; mode = 2;
	assert(!apply(NULL) && state.mode == 1 && sends == 2 && timers == 1);
	assert(!strcmp(trace, "pacctAP"));
	reset(); state.auxiliary_flags = 6; state.role = 0; state.slots = 0; mode = 2;
	assert(!apply(NULL) && !sends && timers == 1 && state.auxiliary_flags == 0x16);
	reset(); state.auxiliary_flags = 5; mode = 2; phy_result = -6;
	assert(!apply(NULL) && state.mode == 2 && !sends && !timers);
	assert(state.auxiliary_flags == 7 && phy_payload[0] == 0x64 && phy_payload[1] == 5);
	reset(); state.auxiliary_flags = 4; state.group = 15; mode = 2;
	assert(!apply(NULL) && state.backups[0] == 0x107 && state.backups[1] == 255);
	assert(backup_payload[2] == 255 && backup_payload[3] == 0);
	reset(); state.channel = 8;
	struct ar_channel_policy saved = state;
	assert(apply(NULL) == -22 && !events && !memcmp(&saved, &state, sizeof(state)));
	reset(); state.auxiliary_channel = 8;
	assert(apply(NULL) == -22 && !events);
	reset(); struct ar_channel_policy_ops missing = ops; missing.timer = NULL;
	assert(ar_channel_policy_set(&state, rows, groups, scores, 8, NULL, NULL,
		&missing, &state) == -22 && !events);
	assert(ar_channel_policy_set(&state, rows, groups, scores, 0, NULL, NULL,
		&ops, &state) == -22 && !events);
	puts("Channel policy tests passed");
	return 0;
}
