/* SPDX-License-Identifier: MIT */
#include "ar_power.h"

static unsigned adjusted(unsigned level, int offset)
{
	int n = (int)level + offset;
	return n < 9 ? 9 : n > 39 ? 39 : (unsigned)n;
}
static void append(struct ar_power_plan *p, uint32_t field, unsigned value)
{
	p->writes[p->count++] = (struct ar_power_write){field, value};
}
int ar_power_plan(const struct ar_power_config *c, unsigned user,
	unsigned level, struct ar_power_plan *out)
{
	if (!c || !out || user >= AR_POWER_USERS) { return -22; }
	if (level < 9 || level > 39) { return -3; }
	if (c->closed[0] > 1 || c->closed[1] > 1 || c->high_clock > 1 || c->dev_role > 1) { return -22; }
	unsigned rows[2];
	for (unsigned path = 0; path < 2; path++) {
		rows[path] = adjusted(level, c->path_offset[path] + c->user_offset[user]) - 9;
		/* Open-loop paths use the unadjusted analog entry. Do not require
		 * calibration rows that will never be written on that path. */
		unsigned row = c->closed[path] ? rows[path] : level - 9;
		if (!(c->valid & (UINT32_C(1) << row))) { return -95; }
		if (c->calibration[row][path * 2] > 63) { return -22; }
	}
	struct ar_power_plan p = {.user = user, .level = level};
	/* Original 0x24515a writes shared open-loop fields first. Closed paths
	 * select independent, clamped calibration rows after signed offsets. */
	for (unsigned path = 0; path < 2; path++) {
		if (!c->closed[path]) {
			append(&p, path ? 0xa580806 : 0xa580006, c->calibration[level - 9][path * 2]);
		}
	}
	for (unsigned path = 0; path < 2; path++) {
		if (!c->closed[path]) { continue; }
		unsigned gain = c->calibration[rows[path]][path * 2];
		unsigned amplitude = c->calibration[rows[path]][path * 2 + 1];
		if (!c->high_clock) { amplitude = (amplitude + 1) / 2; }
		if (c->dev_role && user == 8) {
			gain = gain > c->csma_offset ? gain - c->csma_offset : 0;
			amplitude = 0;
		}
		if (!path) {
			uint32_t base = (0x9a4 + user * 8) << 16;
			append(&p, base | 0x806, gain); append(&p, base | 8, amplitude);
		} else {
			append(&p, (0xda4 + user * 8) << 16 | 6, gain);
			append(&p, (0xda0 + user * 8) << 16 | 0x1808, amplitude);
		}
	}
	*out = p;
	return 0;
}
