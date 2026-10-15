/* SPDX-License-Identifier: MIT */
#ifndef AR_POWER_CONTROL_H
#define AR_POWER_CONTROL_H
#include "ar_power.h"

struct ar_power_control {
	struct ar_power_config config;
	uint8_t level[AR_POWER_USERS];
};
typedef void (*ar_power_field)(uint32_t field, uint32_t value, void *context);
/* Compared offline; initial loader user0 application is integrated. Dynamic
 * tuning/host-policy and band-offset runtime integration remain pending. */
/* Caller serializes this operation. Open-loop writes precede level[0], then
 * closed-loop writes precede level[user]. The original shared current-level
 * byte aliases user0: applying another user changes both cached levels.
 * Native validation/planning errors leave state and hardware untouched. */
int ar_power_control_apply(struct ar_power_control *state, unsigned user,
	unsigned level, ar_power_field field, void *context);
/* Initialize all twelve users in order. Native preflight rejects unavailable
 * rows before any effects; the original loop's unlock return hid errors. */
int ar_power_control_initialize(struct ar_power_control *state, unsigned level,
	ar_power_field field, void *context);
/* Band-offset nibbles for TX users0..10; user11 has no fields. Offsets outside
 * -6..6 have no effects. Both closed paths use the paired gain policy; otherwise
 * the first nibble is12 and the second is12+clamp(offset,-3,3).
 * This helper does not change calibration offsets or cached power levels. */
int ar_power_band_offset(const struct ar_power_config *config, unsigned user,
	int8_t offset, ar_power_field field, void *context);
#endif
