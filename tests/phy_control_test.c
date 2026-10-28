/* SPDX-License-Identifier: MIT */
#include "ar_phy_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_phy_control state;
static unsigned locked, writes, calls, fields, outgoing_seen, command_seen, length_seen, idle_calls;
static uint8_t packets[8][7];
static void lock(void *context) { assert(context == &state && !locked); locked = 1; }
static void unlock(void *context) { assert(context == &state && locked); locked = 0; }
static void field(uint32_t address, uint32_t value, void *context)
{
	assert(context == &state && locked && address == 0x0d301008 && (value == 0 || value == 255)); fields++;
}
static void write(const uint8_t packet[7], void *context)
{
	assert(context == &state && locked && writes < 8); memcpy(packets[writes++],packet,7);
}
static void dispatch(unsigned outgoing, unsigned command, uint8_t *data, size_t length, void *context)
{
	assert(context == &state && locked && data == (outgoing ? state.tx : state.rx)+3);
	calls++; outgoing_seen = outgoing; command_seen = command; length_seen = length;
}
static void idle(uint8_t packet[7], void *context)
{
	assert(context == &state && locked && packet == state.tx); idle_calls++;
	memcpy(packet,(uint8_t[]){0xa5,0x40,0x76,0x12,0x34,0x56,0x78},7);
}
static const struct ar_phy_control_ops ops = {lock,unlock,field,write,dispatch,idle};
static void reset(void)
{
	memset(&state,0,sizeof state); state.role = 1; state.callbacks = 0x3ff; state.wake = 1;
	locked = writes = calls = fields = idle_calls = 0;
}
int main(void)
{
	const uint8_t data[4] = {0x80,3,0x12,0x34};
	reset(); state.tx[6] = 0x78;
	assert(!ar_phy_control_enqueue(&state,4,2,data,3,&ops,&state));
	assert(state.tx[1] == 0x24 && state.tx[2] == 6 && state.tx[6] == 0x78 && fields == 1);
	struct ar_phy_control before = state;
	assert(ar_phy_control_enqueue(&state,3,1,data,2,&ops,&state) == -1);
	assert(!memcmp(&before,&state,sizeof state));
	assert(!ar_phy_control_tick(&state,100,&ops,&state));
	assert(writes == 1 && packets[0][1] == 0x24 && state.tx[1] == 0x14 && !calls);
	assert(!ar_phy_control_tick(&state,101,&ops,&state));
	assert(writes == 2 && packets[1][1] == 0x14 && state.tx[1] == 4 && !calls);
	uint8_t retained_checksum = state.tx[0];
	assert(!ar_phy_control_tick(&state,102,&ops,&state));
	assert(calls == 1 && outgoing_seen == 1 && command_seen == 4 && length_seen == 3);
	assert(state.tx[1] == 0 && writes == 3 && packets[2][0] == retained_checksum && fields == 2);
	assert(!ar_phy_control_tick(&state,103,&ops,&state) && writes == 3 && state.ticks == 4);
	reset(); state.mode = 3; state.tx[0] = 0xb0;
	assert(!ar_phy_control_enqueue(&state,3,31,data,2,&ops,&state));
	for (unsigned i = 0; i < 3; i++) { assert(!ar_phy_control_tick(&state,i,&ops,&state)); }
	assert(!calls && state.tx[1] == 0xf3 && (state.tx[2] & 1) && (state.tx[0] & 0xf0) == 0xb0);
	assert(!ar_phy_control_cancel(&state,&ops,&state));
	assert(writes == 4 && state.tx[1] == 0xf0 && state.tx[0] == packets[3][0] && fields == 2);
	assert(!ar_phy_control_cancel(&state,&ops,&state) && writes == 4);
	reset(); state.role = 0; state.frame_us = 10000;
	assert(!ar_phy_control_tick(&state,60000,&ops,&state) && !writes);
	assert(!ar_phy_control_tick(&state,60001,&ops,&state) && writes == 1);
	assert(state.tx[1] == 7 && state.lead_ms == 11 && state.time_value == 60001);
	assert(state.tx[3] == (60012 & 255) && state.tx[4] == (60012 >> 8));
	assert(!ar_phy_control_tick(&state,60002,&ops,&state) && writes == 2 && !state.tx[1] && !calls);
	/* A period of 1 s, and the lead following a shorter frame. */
	state.time_period_ms = 1000; state.frame_us = 3251;
	assert(!ar_phy_control_tick(&state,61000,&ops,&state) && writes == 2);
	assert(!ar_phy_control_tick(&state,61001,&ops,&state) && writes == 3 && state.tx[1] == 7);
	assert(state.lead_ms == 4 && state.tx[3] == (61005 & 255) && state.tx[4] == (61005 >> 8));
	reset(); state.rx[1] = 0x14; state.rx[2] = 6;
	assert(!ar_phy_control_expire(&state,&ops,&state));
	assert(calls == 1 && !outgoing_seen && command_seen == 4 && length_seen == 3);
	assert(!ar_phy_control_expire(&state,&ops,&state) && calls == 2);
	state.rx[1] = 0x1f;
	assert(!ar_phy_control_expire(&state,&ops,&state) && calls == 2);
	state.rx[1] = 4;
	assert(!ar_phy_control_expire(&state,&ops,&state) && calls == 2);
	reset(); state.role = 0; state.mode = 3; state.tx[1] = 4; state.tx[2] = 6;
	assert(!ar_phy_control_tick(&state,0,&ops,&state));
	assert(calls == 1 && idle_calls == 1 && packets[0][0] == 0xa5 && packets[0][1] == 0x40);
	before = state;
	assert(ar_phy_control_enqueue(&state,10,1,data,3,&ops,&state) == -22);
	assert(ar_phy_control_enqueue(&state,4,32,data,3,&ops,&state) == -22);
	assert(ar_phy_control_enqueue(&state,4,1,data,5,&ops,&state) == -22);
	assert(ar_phy_control_enqueue(&state,4,1,NULL,3,&ops,&state) == -22);
	assert(!memcmp(&before,&state,sizeof state) && !locked);
	/* Open8030 frame stamps (command 10): between the AP time's messages, at
	 * most once per stamp period, 32 bits packed as command 7's ms (bits
	 * 24..27 in byte 3's low nibble, 28..31 in the TTL nibble);
	 * one tick in the slot, then cleared; never from a ground. */
	memset(&state, 0, sizeof state); writes = 0;
	state.time_period_ms = 1000; state.stamp_period_ms = 100; state.stamp_us = UINT64_C(0xabcdef1234);
	assert(ar_phy_control_tick(&state, 5000, &ops, &state) == 0 && writes == 1 && (packets[0][1] & 15) == 7);
	assert(ar_phy_control_tick(&state, 5010, &ops, &state) == 0 && writes == 2 && !(packets[1][1] & 15));
	assert(ar_phy_control_tick(&state, 5150, &ops, &state) == 0 && writes == 3);
	assert((packets[2][1] & 15) == 10 && (packets[2][1] >> 4) == 0xc && ((packets[2][2] >> 1) & 7) == 4);
	assert(packets[2][3] == 0x34 && packets[2][4] == 0x12 && packets[2][5] == 0xef && packets[2][6] == 0x0d);
	assert(packets[2][0] == ar_phy_control_checksum(packets[2]) && state.stamps == 1);
	assert(ar_phy_control_enqueue(&state, 4, 16, (const uint8_t[]){1, 2, 3}, 3, &ops, &state) == -1);
	assert(ar_phy_control_tick(&state, 5160, &ops, &state) == 0 && writes == 4 && !(state.tx[1] & 15));
	assert(ar_phy_control_tick(&state, 5170, &ops, &state) == 0 && writes == 4);   /* not before 100 ms */
	writes = 0; state.role = 1; state.stamp_ms = 0;
	assert(ar_phy_control_tick(&state, 9000, &ops, &state) == 0 && writes == 0);
	/* Open8030 resend maps (command 11): once retry_every frames passed since
	 * the last, never ahead of a stamp that is due; 24 bits of map, the
	 * newest frame's index split as the stamps split theirs; one tick in the
	 * slot, then cleared; never from a ground. */
	memset(&state, 0, sizeof state); writes = 0;
	state.time_period_ms = 1000; state.stamp_period_ms = 100; state.retry_every = 6;
	state.retry_map = 0xa5123456; state.retry_frame = 5;
	assert(ar_phy_control_tick(&state, 5000, &ops, &state) == 0 && writes == 1 && (packets[0][1] & 15) == 7);
	assert(ar_phy_control_tick(&state, 5010, &ops, &state) == 0 && writes == 2 && !(state.tx[1] & 15));
	assert(ar_phy_control_tick(&state, 5020, &ops, &state) == 0 && writes == 3 && (packets[2][1] & 15) == 10);  /* the stamp first */
	assert(ar_phy_control_tick(&state, 5030, &ops, &state) == 0 && writes == 4 && !(state.tx[1] & 15));
	assert(ar_phy_control_tick(&state, 5040, &ops, &state) == 0 && writes == 4);   /* 5 frames: not yet */
	state.retry_frame = 0x26;
	assert(ar_phy_control_tick(&state, 5050, &ops, &state) == 0 && writes == 5);
	assert(packets[4][1] == (0x20 | 11) && ((packets[4][2] >> 1) & 7) == 4 && packets[4][6] == 6);
	assert(packets[4][3] == 0x56 && packets[4][4] == 0x34 && packets[4][5] == 0x12);
	assert(packets[4][0] == ar_phy_control_checksum(packets[4]) && state.retry_sent_frame == 0x26);
	assert(ar_phy_control_tick(&state, 5060, &ops, &state) == 0 && writes == 6 && !(state.tx[1] & 15));
	state.retry_frame = 0x2b;
	assert(ar_phy_control_tick(&state, 5070, &ops, &state) == 0 && writes == 6);   /* 5 frames on */
	writes = 0; state.retry_frame = 0x2c;
	assert(ar_phy_control_tick(&state, 5120, &ops, &state) == 0 && writes == 1 && (packets[0][1] & 15) == 10);  /* a due stamp first */
	assert(ar_phy_control_tick(&state, 5130, &ops, &state) == 0 && writes == 2);
	assert(ar_phy_control_tick(&state, 5140, &ops, &state) == 0 && writes == 3 && packets[2][1] == (0x20 | 11) && packets[2][6] == 0xc);
	assert(ar_phy_control_tick(&state, 5150, &ops, &state) == 0 && writes == 4 && !(state.tx[1] & 15));
	writes = 0; state.role = 1; state.retry_frame = 0x60;
	assert(ar_phy_control_tick(&state, 5600, &ops, &state) == 0 && writes == 0);
	puts("PHY control queue, TTL, cancellation, timer, frame stamp and resend map tests passed");
	return 0;
}
