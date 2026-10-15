/* SPDX-License-Identifier: MIT */
#ifndef AR_POWER_H
#define AR_POWER_H
#include <stdint.h>

#define AR_POWER_USERS 12u
#define AR_POWER_LEVELS 31u
#define AR_POWER_MAX_WRITES 6u
/* Calibration is supplied board data. A clear bit means that row is absent,
 * not that zero is a usable calibration value. Rows cover levels 9..39. */
struct ar_power_config {
	uint8_t calibration[AR_POWER_LEVELS][4];
	uint32_t valid;
	int8_t path_offset[2], user_offset[AR_POWER_USERS];
	uint8_t closed[2], high_clock, dev_role, csma_offset;
};
struct ar_power_write { uint32_t field, value; };
struct ar_power_plan {
	struct ar_power_write writes[AR_POWER_MAX_WRITES];
	unsigned count;
	uint8_t user, level;
};
/* Pure calculation of ordered field writes, with no MMIO or state mutation.
 * The caller must serialize applying the complete plan with RF control. */
int ar_power_plan(const struct ar_power_config *config, unsigned user,
	unsigned level, struct ar_power_plan *out);
#endif
