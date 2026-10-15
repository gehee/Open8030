/* SPDX-License-Identifier: MIT */
#include "ar_channel_transaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_channel_policy state;
static struct ar_channel_histories histories;
static uint8_t first[10], second[8], phy_bytes[3], posted[16];
static uint32_t rows[8], destination, message;
static uint8_t groups[8];
static unsigned depth, posts, events;
static int phy_result, post_result;
static char trace[32];
static void note(char event) { assert(events < sizeof(trace)); trace[events++] = event; }
static void lock(enum ar_channel_transaction_lock which, void *context)
{
	assert(context == &state && !depth && which <= AR_CHANNEL_TRANSACTION_AP);
	depth = 1; note('l');
}
static void unlock(enum ar_channel_transaction_lock which, void *context)
{
	assert(context == &state && depth == 1 && which <= AR_CHANNEL_TRANSACTION_AP);
	depth = 0; note('u');
}
static int phy(unsigned command, unsigned ttl, const uint8_t *payload,
	size_t length, void *context)
{
	assert(context == &state && depth == 1 && command == 4 && ttl == 16 && length == 3);
	memcpy(phy_bytes, payload, 3); note('f'); return phy_result;
}
static int post(uint32_t target, uint32_t id, const uint8_t *payload,
	size_t length, uint32_t timeout, void *context)
{
	assert(context == &state && timeout == UINT32_MAX);
	assert((id == 0x109 && target == 0 && length == 16 && !depth) ||
		(id == 0x108 && target == 1 && length == 8 && depth == 1));
	destination = target; message = id;
	memcpy(posted, payload, length); posts++; note('q'); return post_result;
}
static const struct ar_channel_transaction_ops ops = {lock, unlock, phy, post};
static void reset(void)
{
	state = (struct ar_channel_policy){.channel = 0x101, .band = 1, .group = 5};
	state.request = (struct ar_channel_request_state){0x102, 1, 0xff};
	histories.marker = 0xabcd;
	for (unsigned i = 0; i < 10; i++) { first[i] = 123; histories.first[i] = i & 1 ? &first[i] : NULL; }
	for (unsigned i = 0; i < 8; i++) { second[i] = 234; histories.second[i] = i & 1 ? &second[i] : NULL; }
	memset(trace, 0, sizeof(trace)); memset(posted, 0xa5, sizeof(posted));
	depth = posts = events = 0; phy_result = post_result = 0;
}
static int complete(uint32_t success, uint32_t selector, uint16_t channel, uint32_t error)
{
	return ar_channel_complete(&state, &histories, rows, groups, 8, success,
		selector, channel, error, &ops, &state);
}
static void cleared(void)
{
	for (unsigned i = 0; i < 10; i++) { assert(first[i] == (i & 1 ? 0 : 123)); }
	for (unsigned i = 0; i < 8; i++) { assert(second[i] == (i & 1 ? 0 : 234)); }
}
int main(void)
{
	for (unsigned i = 0; i < 8; i++) { rows[i] = (i % 3) << 27; groups[i] = 0xf0 | (i % 6); }
	const struct {uint8_t mode, flags, relay, expected;} ap[] = {
		{0,0,0,0x50}, {0,8,0,0xf0}, {1,0,1,0x70},
		{1,8,1,0xf0}, {1,0,2,0x50}, {3,0,1,0x50}};
	for (unsigned i = 0; i < sizeof(ap)/sizeof(ap[0]); i++) {
		reset(); struct ar_channel_route route = {.mode = ap[i].mode,
			.channel_flags = ap[i].flags, .relay = ap[i].relay};
		assert(!ar_channel_route_request(&route, 1, 0x102, &ops, &state));
		assert(phy_bytes[0] == ap[i].expected && phy_bytes[1] == 2 && phy_bytes[2] == 1);
		assert(!posts && !strcmp(trace, "lfu"));
	}
	reset(); struct ar_channel_route route = {.mode = 3, .channel_flags = 8};
	assert(!ar_channel_route_request(&route, 0, 0x102, &ops, &state) && phy_bytes[0] == 0xd0);
	reset(); route = (struct ar_channel_route){.role = 1, .slot = 255, .dev_receive = 0x101};
	phy_result = 7; post_result = -6;
	assert(!ar_channel_route_request(&route, 0xabcdef01, 0x102, &ops, &state));
	assert(phy_bytes[0] == 0x8f && !strcmp(trace, "lfuq") && posts == 1);
	const uint8_t failed[16] = {0,0,0,0,1,0xef,0xcd,0xab,2,1,0,0,1,0,0,0};
	assert(destination == 0 && message == 0x109 && !memcmp(posted, failed, 16));
	reset(); assert(!ar_channel_route_request(&route, 1, 0x101, &ops, &state));
	assert(!strcmp(trace, "lu") && !posts);
	reset(); route.role = 0; route.ap_broadcast_transmit = 0x101;
	assert(!ar_channel_route_request(&route, 0, 0x101, &ops, &state));
	assert(!strcmp(trace, "lu") && !posts);

	reset(); assert(!complete(2, 1, 0x102, 7));
	assert(state.channel == 0x102 && state.band == 2 && state.group == 2);
	assert(state.request.flags == 0xd7 && !histories.marker && !posts); cleared();
	reset(); assert(!complete(1, 1, 2, 0));
	assert(state.channel == 0x101 && state.request.flags == 0xdf && !histories.marker);
	assert(!posts); cleared();
	reset(); post_result = -6; assert(!complete(0, 1, 0x102, 0));
	assert(state.channel == 0x101 && state.request.pending_channel == 0x101);
	assert(state.request.flags == 0xdf && !histories.marker && posts == 1);
	const uint8_t rollback[8] = {1,0,0,0,1,1,0,0};
	assert(message == 0x108 && !memcmp(posted, rollback, 8) && !strcmp(trace, "lqu")); cleared();
	reset(); assert(!complete(0, 1, 0x102, 0));
	assert(state.request.flags == 0xff && posts == 1);
	reset(); assert(!complete(0, 1, 0x102, 1));
	assert(!posts && state.request.pending_channel == 0x102 && state.request.flags == 0xdf); cleared();
	reset(); assert(!complete(1, 0, 1, 0));
	assert(state.request.flags == 0xaf && histories.marker == 0xabcd && !posts); cleared();
	reset(); assert(!complete(1, UINT32_MAX, 0x101, 0));
	assert(state.request.flags == 0xbf && histories.marker == 0xabcd); cleared();
	reset(); state.request.flags = 8; assert(!complete(1, 1, 0x102, 0));
	assert(!state.request.flags && histories.marker == 0xabcd && first[1] == 123 && second[1] == 234);
	reset(); state.request.pending_channel = 255;
	assert(complete(1, 1, 255, 0) == -22 && !events && state.request.flags == 255);
	assert(histories.marker == 0xabcd && first[1] == 123);
	reset(); struct ar_channel_transaction_ops missing = ops; missing.post = NULL;
	assert(ar_channel_route_request(&route, 0, 0, &missing, &state) == -22 && !events);
	assert(ar_channel_complete(&state, &histories, rows, groups, 8, 0, 1, 0, 0,
		&missing, &state) == -22 && !events);
	puts("Channel transaction tests passed");
	return 0;
}
