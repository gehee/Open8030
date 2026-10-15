/* SPDX-License-Identifier: MIT */
#include "ar_scan.h"

static int find(const uint32_t *rows, size_t count, unsigned first, uint16_t *channel)
{
	for (unsigned i = first & 255; i < count; i++) {
		if (rows[i] & 0x01000000) { *channel = i; return 1; }
	}
	return 0;
}
int ar_scan_advance(const uint32_t *rows, size_t count, uint16_t *channel,
	unsigned sub_enabled, unsigned sub_count)
{
	if ((!rows && count) || count > 255 || !channel || sub_enabled > 1 || sub_count > 255) { return -22; }
	unsigned sub = *channel >> 8;
	if (!sub) {
		if (!find(rows,count,(*channel & 255) + 1,channel)) { (void)find(rows,count,0,channel); }
	} else if (sub_enabled) {
		*channel = (*channel & 255) | (sub < sub_count ? sub + 1 : 1) << 8;
	}
	return 0;
}
int ar_scan_tune(uint32_t frequency, uint64_t pll, unsigned dual_band,
	ar_command_write write_field, ar_command_write write_byte, void *context)
{
	if (!write_field || !write_byte || dual_band > 1 || pll >> 40) { return -22; }
	for (unsigned i = 0; i < 5; i++) { write_byte(0x90a+i,(uint32_t)(pll >> (32-8*i)) & 255,context); }
	unsigned bank = frequency <= 1000000 ? 0 : frequency <= 4300000 ? 1 : 2;
	unsigned dual = dual_band && bank == 2;
	write_field(0x9fc1903,bank == 2 ? 4 : bank == 1 ? 1 : 2,context);
	write_field(dual ? 0x9fc1201 : 0x9fc1601,1,context);
	write_field(dual ? 0x9fc1601 : 0x9fc1201,!dual_band,context);
	return 0;
}
