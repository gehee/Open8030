/* SPDX-License-Identifier: MIT */
#include "ar_power_control.h"

int ar_power_control_initialize(struct ar_power_control *state, unsigned level,
	ar_power_field field, void *context)
{
	if (!state || !field) { return -22; }
	struct ar_power_plan plan;
	for (unsigned user = 0; user < AR_POWER_USERS; user++) {
		int result = ar_power_plan(&state->config,user,level,&plan);
		if (result) { return result; }
	}
	for (unsigned user = 0; user < AR_POWER_USERS; user++) {
		(void)ar_power_control_apply(state,user,level,field,context);
	}
	return 0;
}

int ar_power_control_apply(struct ar_power_control *state, unsigned user,
	unsigned level, ar_power_field field, void *context)
{
	if (!state || !field) { return -22; }
	struct ar_power_plan plan;
	int result = ar_power_plan(&state->config,user,level,&plan);
	if (result) { return result; }
	unsigned open = !state->config.closed[0] + !state->config.closed[1];
	for (unsigned i = 0; i < open; i++) { field(plan.writes[i].field,plan.writes[i].value,context); }
	state->level[0] = level;
	for (unsigned i = open; i < plan.count; i++) { field(plan.writes[i].field,plan.writes[i].value,context); }
	state->level[user] = level;
	return 0;
}

int ar_power_band_offset(const struct ar_power_config *config, unsigned user,
	int8_t offset, ar_power_field field, void *context)
{
	if (!config || !field || user >= AR_POWER_USERS || config->closed[0] > 1 ||
		config->closed[1] > 1) { return -22; }
	if (offset < -6 || offset > 6 || user == 11) { return 0; }
	unsigned first = 12, second;
	if (config->closed[0] && config->closed[1]) {
		/* Express the paired gain steps independently of original table layout. */
		if (offset < 0) {
			first = offset < -4 ? 9-offset : offset < -1 ? 10-offset : 12;
			second = offset < -4 ? 9 : offset < -1 ? 10 : 11;
		} else {
			first = offset < 3 ? 12 : offset < 5 ? 14-offset : 9;
			second = offset < 2 ? 12+offset : offset < 6 ? 14 : 15;
		}
	} else { second = 12 + (offset < -3 ? -3 : offset > 3 ? 3 : offset); }
	uint32_t base = ((user >> 1) + 0x2ca) * 0x40000 | (user & 1) << 12;
	field(user == 10 ? 0xb701c04 : base | 0x804,first,context);
	field(user == 10 ? 0xb701804 : base | 4,second,context);
	return 0;
}
