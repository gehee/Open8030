/* SPDX-License-Identifier: MIT */
#include "ar_channel_monitor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_channel_monitor state;
static unsigned posts, resets, fields, losses, hops, hop_channel, preference;
static uint8_t last_payload[16];
static char order[16];
static unsigned events;
static void note(char c) { assert(events < sizeof(order)); order[events++] = c; }
static void reset(unsigned per_user, unsigned user, void *context)
{
	assert(context == &state && per_user == !!state.mode && user < 10); resets++; note('r');
}
static void field(uint32_t address, uint32_t value, void *context)
{
	assert(context == &state && address == 0x09a41801 && value == 0);
	assert(!(state.hold_mask & 1)); fields++; note('f');
}
static int post(unsigned destination, unsigned message, const uint8_t *data,
	size_t length, uint32_t timeout, void *context)
{
	assert(context == &state && destination == 0 && message == 0x109 && length == 16 && timeout == UINT32_MAX);
	memcpy(last_payload,data,16); posts++; note('p'); return -6;
}
static void lost(unsigned user, struct ar_channel_monitor_user *entry, void *context)
{
	assert(context == &state && user == 8 && entry == &state.users[8]);
	assert(!(entry->flags & 0x40)); losses++; note('l');
	*entry = (struct ar_channel_monitor_user){255,7,99,0x87,0x98};
}
static uint32_t current(unsigned selector, void *context)
{
	assert(context == &state && selector == 3); note('c'); return 5;
}
static uint32_t preferred(unsigned group, unsigned channel, void *context)
{
	assert(context == &state && group == 1 && channel == 5); note('b'); return preference;
}
static int next(uint16_t *channel, unsigned group, void *context)
{
	assert(context == &state && group == 1 && !*channel); *channel = 2; note('n'); return 0;
}
static int last(uint16_t *channel, unsigned group, void *context)
{
	assert(context == &state && group == 1 && !*channel); *channel = 17; note('e'); return 0;
}
static void hop(uint16_t channel, void *context)
{
	assert(context == &state && !state.unlocks && state.hop_ms[1] == 0xabcdef01);
	hops++; hop_channel = channel; note('h');
}
static uint32_t now(void *context) { assert(context == &state); return 0xabcdef01; }
static const struct ar_channel_monitor_ops ops = {reset,field,post,lost,current,preferred,next,last,hop,now};
static void clear(void)
{
	memset(&state,0,sizeof state); memset(order,0,sizeof order);
	posts = resets = fields = losses = hops = events = 0;
}
int main(void)
{
	clear(); state.hold_mask = 0xffff;
	state.users[0] = (struct ar_channel_monitor_user){0x61,100,4,7,116};
	assert(!ar_channel_monitor_observe(&state,0,1,NULL,0,&ops,&state));
	assert(state.users[0].flags == 0x49 && state.successes == 1 && fields == 1);
	assert(!ar_channel_monitor_observe(&state,0,1,NULL,0,&ops,&state));
	assert(state.users[0].flags == 3 && !state.successes && !posts);
	clear(); state.successes = 1;
	state.users[3] = (struct ar_channel_monitor_user){0x43,100,4,7,116};
	state.users[8] = (struct ar_channel_monitor_user){0x43,100,4,9,116};
	assert(!ar_channel_monitor_observe(&state,3,1,NULL,0,&ops,&state));
	assert(!posts && state.successes == 2);
	assert(!ar_channel_monitor_observe(&state,8,1,NULL,0,&ops,&state));
	assert(posts == 1 && !state.successes && state.users[8].flags == 3);
	assert(last_payload[0] == 1 && last_payload[4] == 1 && last_payload[8] == 9);
	for (unsigned i = 9; i < 16; i++) { assert(!last_payload[i]); }
	clear(); state.role = 1; state.mode = 1;
	state.users[8] = (struct ar_channel_monitor_user){0x43,60,59,9,60};
	assert(!ar_channel_monitor_tick(&state,&ops,&state));
	assert(!strcmp(order,"rpl") && posts == 1 && resets == 1 && losses == 1);
	assert(!last_payload[0] && state.users[8].flags == 0xb9 && !state.users[8].missed);
	assert(state.users[8].threshold == 7 && state.users[8].channel == 0x87);
	clear(); state.users[8] = (struct ar_channel_monitor_user){0x43,255,255,9,0};
	assert(!ar_channel_monitor_tick(&state,&ops,&state));
	assert(posts == 1 && !losses && !resets && !state.users[8].missed);
	clear(); state.policy_flags = 4; state.role = 1; state.group = 1; state.band = 2; state.unlocks = 10;
	state.users[8].flags = 3; preference = 7;
	assert(!ar_channel_monitor_observe(&state,8,0,NULL,0,&ops,&state));
	assert(!strcmp(order,"cbh") && hops == 1 && hop_channel == 7);
	events = 0; memset(order,0,sizeof order); state.unlocks = 40; preference = 255;
	assert(!ar_channel_monitor_observe(&state,8,0,NULL,0,&ops,&state));
	assert(!strcmp(order,"cbneh") && hops == 2 && hop_channel == 17);
	events = 0; struct ar_channel_monitor before = state;
	assert(ar_channel_monitor_observe(&state,10,0,NULL,0,&ops,&state) == -22 && !events);
	assert(!memcmp(&before,&state,sizeof state));
	assert(ar_channel_monitor_observe(&state,8,0,NULL,1,&ops,&state) == -22);
	assert(ar_channel_monitor_tick(NULL,&ops,&state) == -22);
	puts("Channel monitor completion, loss and hopping tests passed");
	return 0;
}
