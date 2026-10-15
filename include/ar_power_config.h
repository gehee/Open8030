/* SPDX-License-Identifier: MIT */
#ifndef AR_POWER_CONFIG_H
#define AR_POWER_CONFIG_H
#include "ar_json.h"
#include "ar_power.h"

struct ar_power_calibration {
	uint8_t rows[AR_POWER_LEVELS][4];
	uint32_t valid;
	int8_t fallback_offsets[4];
	uint8_t has_fallback, csma_offset, board_specific;
};
enum ar_power_factory_state {
	AR_POWER_FACTORY_UNREAD, AR_POWER_FACTORY_UNAVAILABLE,
	AR_POWER_FACTORY_RECORD, AR_POWER_FACTORY_JSON_ONLY
};
enum ar_power_offset_source {
	AR_POWER_OFFSET_DEFAULT = 1, AR_POWER_OFFSET_JSON,
	AR_POWER_OFFSET_FACTORY_V3, AR_POWER_OFFSET_FACTORY_V4
};
/* Board selection is an exact JSON member lookup, followed by default only
 * when that member is absent. A malformed present board entry is an error. */
int ar_power_calibration_load(struct ar_power_calibration *out,
	struct ar_json root, const char *board);
/* Decode only; no flash operations. -2 means unknown version, -22 malformed
 * input. The caller decides whether a missing/unknown record permits fallback. */
int ar_power_factory_decode(int8_t out[4], const uint8_t *record, size_t length);
/* Resolve matched chip-profile-1 path offsets (record bytes 4 and 6). Retains
 * the caller's clock/role/path controls and user offsets. Positive result names
 * the source; negative result leaves output unchanged. UNREAD never resolves. */
int ar_power_calibration_resolve(struct ar_power_config *out,
	const struct ar_power_calibration *calibration, enum ar_power_factory_state state,
	const uint8_t *record, size_t length);
#endif
