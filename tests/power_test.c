/* SPDX-License-Identifier: MIT */
#include "ar_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	struct ar_power_config c = {.valid = 0x7fffffff, .closed = {1,1}, .high_clock = 1, .dev_role = 1, .csma_offset = 8};
	for (unsigned i = 0; i < AR_POWER_LEVELS; i++) {
		c.calibration[i][0] = 40 - i; c.calibration[i][1] = 11 + 7 * i;
		c.calibration[i][2] = 42 - i; c.calibration[i][3] = 13 + 7 * i;
	}
	struct ar_power_plan p;
	assert(!ar_power_plan(&c, 0, 14, &p) && p.count == 4);
	assert(p.writes[0].field == 0x9a40806 && p.writes[0].value == 35);
	assert(p.writes[1].field == 0x9a40008 && p.writes[1].value == 46);
	assert(p.writes[2].field == 0xda40006 && p.writes[2].value == 37);
	assert(p.writes[3].field == 0xda01808 && p.writes[3].value == 48);
	c.high_clock = 0;
	assert(!ar_power_plan(&c, 0, 9, &p) && p.writes[1].value == 6 && p.writes[3].value == 7);
	c.path_offset[0] = -6; c.path_offset[1] = 6; c.user_offset[11] = 2;
	assert(!ar_power_plan(&c, 11, 10, &p));
	assert(p.writes[0].field == 0x9fc0806 && p.writes[0].value == 40 && p.writes[2].value == 33);
	assert(!ar_power_plan(&c, 11, 39, &p) && p.writes[0].value == 14 && p.writes[2].value == 12);
	c.csma_offset = 63;
	assert(!ar_power_plan(&c, 8, 14, &p));
	for (unsigned i = 0; i < p.count; i++) { assert(!p.writes[i].value); }
	c.closed[0] = c.closed[1] = 0;
	assert(!ar_power_plan(&c, 8, 14, &p) && p.count == 2);
	assert(p.writes[0].field == 0xa580006 && p.writes[0].value == 35);
	assert(p.writes[1].field == 0xa580806 && p.writes[1].value == 37);
	c.closed[0] = 1;
	assert(!ar_power_plan(&c, 0, 14, &p) && p.count == 3 && p.writes[0].field == 0xa580806);
	struct ar_power_plan saved = p;
	assert(ar_power_plan(&c, 12, 14, &p) == -22);
	assert(ar_power_plan(&c, 0, 8, &p) == -3 && ar_power_plan(&c, 0, 40, &p) == -3);
	c.valid &= ~1u;
	assert(ar_power_plan(&c, 0, 14, &p) == -95);
	c.valid |= 1u; c.calibration[0][0] = 64;
	assert(ar_power_plan(&c, 0, 14, &p) == -22);
	assert(!memcmp(&p, &saved, sizeof p));
	assert(ar_power_plan(NULL, 0, 14, &p) == -22 && ar_power_plan(&c, 0, 14, NULL) == -22);
	puts("power calibration, offsets, clocks, paths, CSMA and atomic failure passed");
}
