/* SPDX-License-Identifier: MIT */
#include "ar_tune.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned events, bytes, fields, updates, power_calls, locks;
static uint32_t address[16], value[16];
static char trace[32];
static struct ar_tune_power power_state;
static void note(char c) { assert(events < sizeof(trace)); trace[events++] = c; }
static void byte(uint32_t a, uint32_t v, void *context)
{
	assert(context == &power_state && !fields && bytes < 5);
	address[bytes] = a; value[bytes++] = v; note('b');
}
static void field(uint32_t a, uint32_t v, void *context)
{
	assert(context == &power_state && bytes == 5 && fields < 5);
	address[5+fields] = a; value[5+fields++] = v; note('f');
}
static void band_offset(unsigned user, int offset, void *context)
{
	assert(context == &power_state && user == 8 && fields == 3 && offset == -6); note('o');
}
static void lock(void *context) { assert(context == &power_state && !locks); locks++; note('l'); }
static void unlock(void *context) { assert(context == &power_state && locks == 1); locks--; note('u'); }
static int power(unsigned user, unsigned level, void *context)
{
	assert(context == &power_state && locks == 1 && user == 8 && level == 25);
	assert(power_state.offset[user] == -128); power_calls++; note('p'); return -6;
}
static void metadata(unsigned user, unsigned direction, unsigned property, uint32_t v, void *context)
{
	assert(context == &power_state && bytes == 5 && fields >= 3 && !locks);
	assert(user < 12 && direction <= 1 && property == (updates ? 3 : 1));
	address[10+updates] = property; value[10+updates++] = v; note('m');
}
static const struct ar_tune_ops ops = {field, byte, band_offset, lock, unlock, power, metadata};
static void reset(void)
{
	events = bytes = fields = updates = power_calls = locks = 0;
	memset(trace, 0, sizeof(trace));
}
int main(void)
{
	const uint32_t rows[3] = {0xff000000|5770000, 0xab000000|4915000, 0};
	struct ar_subchannel_config sub = {.main_bandwidth = 4, .count = 4}; uint32_t frequency = 99;
	assert(!ar_channel_resolve(rows,3,&sub,0x100,&frequency) && frequency == 5762500);
	assert(!ar_channel_resolve(rows,3,&sub,0x400,&frequency) && frequency == 5777500);
	assert(!ar_channel_resolve(rows,3,&sub,0x500,&frequency) && frequency == 5770000);
	assert(!ar_channel_resolve(rows,3,&sub,0x10000,&frequency) && frequency == 5770000);
	assert(!ar_channel_resolve(rows,3,NULL,0x100,&frequency) && frequency == 5770000);
	sub.main_bandwidth = 255; sub.offset_khz = -1;
	assert(!ar_channel_resolve(rows,3,&sub,0x102,&frequency) && frequency == UINT32_MAX);
	assert(!ar_channel_resolve(rows,3,&sub,0x103,&frequency) && !frequency);
	assert(!ar_channel_resolve(NULL,0,&sub,0,&frequency) && !frequency);
	frequency = 99; assert(ar_channel_resolve(NULL,3,&sub,0,&frequency) == -22 && frequency == 99);
	struct ar_tune_config config = {0};
	reset(); assert(!ar_tune_apply(11,1,5770000,UINT64_C(0xa512345678),0,&config,NULL,&ops,&power_state));
	assert(!strcmp(trace,"bbbbbfffmm") && fields == 3 && updates == 2);
	assert(address[0] == 0x90a && address[4] == 0x90e && value[0] == 0xa5 && value[4] == 0x78);
	assert(address[5] == 0x09fc1903 && value[5] == 4 && value[10] == 2 && value[11] == 5770000);
	config.gain_count = 3;
	config.gain[0] = (struct ar_tune_gain){6000000,9};
	config.gain[1] = (struct ar_tune_gain){1000000,10};
	config.gain[2] = (struct ar_tune_gain){8000000,11};
	reset(); assert(!ar_tune_apply(8,1,5770000,1,0,&config,NULL,&ops,&power_state));
	assert(address[0] == 0x905 && fields == 5 && value[9] == 9);
	reset(); assert(!ar_tune_apply(8,1,6000000,1,0,&config,NULL,&ops,&power_state));
	assert(value[9] == 11);
	reset(); assert(!ar_tune_apply(10,1,6000000,1,0,&config,NULL,&ops,&power_state));
	assert(value[9] == 0x3c);
	config.dual_band = 255; config.band_offset[1] = -6;
	power_state.enabled = 1; power_state.level[8] = 25;
	reset(); assert(!ar_tune_apply(8,0,5770000,1,128,&config,&power_state,&ops,&power_state));
	assert(!strcmp(trace,"bbbbbfffolpumm") && power_calls == 1 && power_state.offset[8] == -128);
	assert(address[0] == 0x900 && address[6] == 0x09e41301 && value[6] == 1);
	assert(address[7] == 0x09e41701 && !value[7]);
	reset(); assert(!ar_tune_apply(8,0,5770000,1,-128,&config,&power_state,&ops,&power_state));
	assert(!power_calls && !strcmp(trace,"bbbbbfffolumm"));
	reset(); assert(ar_tune_apply(12,1,5770000,1,0,&config,NULL,&ops,&power_state) == -22 && !events);
	assert(ar_tune_apply(8,1,5770000,UINT64_C(1)<<40,0,&config,NULL,&ops,&power_state) == -22 && !events);
	config.gain_count = 9;
	assert(ar_tune_apply(8,1,5770000,1,0,&config,NULL,&ops,&power_state) == -22 && !events);
	puts("Channel resolution and tuning tests passed");
	return 0;
}
