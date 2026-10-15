/* SPDX-License-Identifier: MIT */
#include "ar_power_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct capture {
	struct ar_power_control *state;
	struct ar_power_write writes[8];
	uint8_t first_level[8], last_level[8];
	unsigned count;
};
static void field(uint32_t address, uint32_t value, void *context)
{
	struct capture *c = context;
	assert(c->count < 8);
	c->writes[c->count] = (struct ar_power_write){address,value};
	c->first_level[c->count] = c->state->level[0];
	c->last_level[c->count++] = c->state->level[11];
}
int main(void)
{
	struct ar_power_control state = {.config={.valid=0x7fffffff,.closed={0,1},.high_clock=1}};
	for (unsigned i = 0; i < AR_POWER_LEVELS; i++) {
		state.config.calibration[i][0] = 10+i;
		state.config.calibration[i][1] = 2*i;
		state.config.calibration[i][2] = 40-i;
		state.config.calibration[i][3] = 3*i;
	}
	state.level[0] = 9; state.level[11] = 11;
	struct capture c = {.state=&state};
	assert(!ar_power_control_apply(&state,11,25,field,&c));
	assert(c.count == 3 && c.writes[0].field == 0xa580006 && c.writes[0].value == 26);
	assert(c.first_level[0] == 9 && c.first_level[1] == 25 && c.first_level[2] == 25);
	for (unsigned i = 0; i < c.count; i++) { assert(c.last_level[i] == 11); }
	assert(state.level[0] == 25 && state.level[11] == 25);
	struct ar_power_control saved = state;
	c.count = 0;
	assert(ar_power_control_apply(&state,0,8,field,&c) == -3);
	assert(ar_power_control_apply(&state,12,25,field,&c) == -22);
	assert(ar_power_control_apply(&state,0,25,NULL,&c) == -22);
	assert(ar_power_control_apply(NULL,0,25,field,&c) == -22);
	assert(!c.count && !memcmp(&state,&saved,sizeof state));
	state.config.valid = 0;
	saved = state;
	assert(ar_power_control_apply(&state,11,25,field,&c) == -95);
	assert(!c.count && !memcmp(&state,&saved,sizeof state));

	/* Mixed paths clamp the offset but preserve first/second write order. */
	assert(!ar_power_band_offset(&state.config,3,-6,field,&c));
	assert(c.count == 2 && c.writes[0].field == 0xb2c1804 && c.writes[0].value == 12);
	assert(c.writes[1].field == 0xb2c1004 && c.writes[1].value == 9);
	assert(!memcmp(&state,&saved,sizeof state));
	c.count = 0; state.config.closed[0] = 1;
	assert(!ar_power_band_offset(&state.config,10,6,field,&c));
	assert(c.count == 2 && c.writes[0].field == 0xb701c04 && c.writes[0].value == 9);
	assert(c.writes[1].field == 0xb701804 && c.writes[1].value == 15);
	c.count = 0;
	assert(!ar_power_band_offset(&state.config,11,0,field,&c));
	assert(!ar_power_band_offset(&state.config,0,7,field,&c));
	assert(!ar_power_band_offset(&state.config,0,-7,field,&c));
	assert(ar_power_band_offset(&state.config,12,0,field,&c) == -22);
	assert(ar_power_band_offset(&state.config,0,0,NULL,&c) == -22);
	assert(ar_power_band_offset(NULL,0,0,field,&c) == -22);
	state.config.closed[0] = 2;
	assert(ar_power_band_offset(&state.config,0,0,field,&c) == -22 && !c.count);
	puts("Power cache alias, effect ordering, band fields and atomic failures passed");
}
