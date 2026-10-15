/* SPDX-License-Identifier: MIT */
#include "ar_channel_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_channel_link state;
static uint32_t rows[8];
static unsigned depth, events, tunes, notifications, posts, fields;
static char trace[64];
static uint8_t last_notice[4], completion[16];
static uint32_t last_field;
static void note(char event) { assert(events < sizeof(trace)); trace[events++] = event; }
static void lock(enum ar_channel_link_lock which, void *context)
{
	assert(context == &state && !depth); depth = 1; note(which ? 'a' : 'l');
}
static void unlock(enum ar_channel_link_lock which, void *context)
{
	assert(context == &state && depth == 1); depth = 0; note(which ? 'A' : 'L');
}
static void field(uint32_t address, uint32_t value, void *context)
{
	assert(context == &state && depth == 1 && value == 1);
	assert((state.monitor_flags & 0x60) == 0x20);
	assert(state.hold_mask & (state.role ? 0x100 : 1));
	last_field = address; fields++; note('f');
}
static int tune(unsigned user, unsigned direction, uint16_t channel, void *context)
{
	assert(context == &state && depth == 1 && user <= 10 && direction <= 1);
	assert(channel == 0x102); tunes++; note('t'); return -6;
}
static int notify(unsigned event, const uint8_t payload[4], void *context)
{
	assert(context == &state && depth == 1 && event == 2);
	memcpy(last_notice, payload, 4); notifications++; note('n'); return -6;
}
static int post(uint32_t destination, uint32_t message, const uint8_t *payload,
	size_t length, uint32_t timeout, void *context)
{
	assert(context == &state && !depth && destination == 0 && message == 0x109);
	assert(length == 16 && timeout == UINT32_MAX);
	memcpy(completion, payload, 16); posts++; note('q'); return -6;
}
static const struct ar_channel_link_ops ops = {lock, unlock, field, tune, notify, post};
static void reset(void)
{
	state = (struct ar_channel_link){.dev = {0x101,0x202,0x303,0x404},
		.ap = {.broadcast_rx = 0x111, .broadcast_tx = 0x222, .receive = 0x333},
		.auxiliary_channel = 0x505, .role = 1, .slot = 3, .current_band = 1,
		.auxiliary_flags = 1, .auxiliary_user = 10, .auxiliary_direction = 1,
		.monitor_flags = 3, .monitor_threshold = 250};
	for (unsigned i = 0; i < 8; i++) { state.ap.slot_tx[i] = 0x801+i; rows[i] = (i % 3) << 27; }
	depth = events = tunes = notifications = posts = fields = 0;
	memset(trace, 0, sizeof(trace)); memset(completion, 0xa5, sizeof(completion));
}
static int apply(unsigned outgoing, uint8_t flags)
{
	const uint8_t payload[3] = {flags,2,1};
	return ar_channel_link_apply(&state, rows, 8, outgoing, payload, 3, &ops, &state);
}
int main(void)
{
	reset(); assert(!apply(1, 0x83));
	assert(state.dev.slot_rx == 0x102 && state.dev.broadcast_rx == 0x102);
	assert(state.monitor_flags == 0x63 && state.monitor_channel == 2 && state.monitor_deadline == 10);
	assert(state.hold_mask == 0x100 && last_field == 0x09e41801 && fields == 1);
	assert(!posts && tunes == 1 && notifications == 1 && !strcmp(trace, "lftnLaA"));
	const uint8_t receive_notice[4] = {0,1,2,4}; assert(!memcmp(last_notice, receive_notice, 4));
	reset(); state.current_band = 2; assert(!apply(1, 0x83));
	assert(!fields && !state.hold_mask && state.monitor_flags == 0x43 && !posts);
	reset(); state.ignore_once = 1; assert(!apply(0, 0x70));
	assert(!state.ignore_once && state.dev.slot_tx == 0x303 && tunes == 1 && !notifications);
	assert(state.auxiliary_channel == 0x102 && !strcmp(trace, "lLatA"));
	reset(); state.ignore_once = -1; assert(!apply(0, 0x70));
	assert(state.ignore_once == -1 && state.dev.slot_tx == 0x102 && tunes == 4 && notifications == 1);
	reset(); state.role = 0; state.ignore_once = -1; assert(!apply(0, 0xf0));
	assert(!state.ignore_once && state.ap.slot_tx[0] == 0x801 && tunes == 1 && !notifications);
	reset(); state.slot = 8; assert(!apply(1, 0xa0));
	assert(state.dev.slot_rx == 0x404 && tunes == 1 && posts == 1);
	assert(completion[0] == 1 && completion[4] == 1 && completion[8] == 2 && completion[9] == 1);
	for (unsigned i = 10; i < 16; i++) { assert(!completion[i]); }
	reset(); state.slot = 8; state.monitor_flags |= 0x40; assert(!apply(1, 0xa0));
	assert(tunes == 1 && !posts);
	reset(); state.role = 0; state.mode = 1; assert(!apply(1, 0xf0));
	assert(tunes == 19 && notifications == 1 && posts == 1 && !completion[4]);
	for (unsigned i = 0; i < 8; i++) { assert(state.ap.slot_tx[i] == 0x102); }
	assert(state.ap.receive == 0x102 && state.ap.broadcast_tx == 0x102 && state.ap.broadcast_rx == 0x102);
	reset(); state.role = 0; state.mode = 3; assert(!apply(1, 0xf0));
	assert(tunes == 12 && notifications == 1 && !completion[4]);
	const uint8_t repeated_notice[4] = {0,0,2,2}; assert(!memcmp(last_notice, repeated_notice, 4));
	reset(); state.role = 0; state.mode = 1; state.channel_flags = 8;
	assert(!apply(1, 0x80) && completion[4] == 1);
	reset(); state.role = 0; assert(!apply(1, 0x40));
	assert(fields == 1 && last_field == 0x09a41801 && state.hold_mask == 1 && !posts && tunes == 8);
	reset(); struct ar_channel_link before = state;
	assert(apply(1, 0x8f) == -22 && !events && !memcmp(&before, &state, sizeof(state)));
	const uint8_t bad_channel[3] = {0x83,255,0};
	assert(ar_channel_link_apply(&state, rows, 8, 1, bad_channel, 3, &ops, &state) == -22 && !events);
	assert(ar_channel_link_apply(&state, rows, 8, 1, bad_channel, 2, &ops, &state) == -22 && !events);
	assert(ar_channel_link_apply(&state, rows, 8, 1, NULL, 3, &ops, &state) == -22 && !events);
	puts("Channel link tests passed");
	return 0;
}
