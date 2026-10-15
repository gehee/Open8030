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
	puts("PHY control queue, TTL, cancellation and timer tests passed");
	return 0;
}
