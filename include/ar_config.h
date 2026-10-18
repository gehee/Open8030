/* SPDX-License-Identifier: MIT */
#ifndef AR_CONFIG_H
#define AR_CONFIG_H
#include <stddef.h>
#include <stdint.h>
#include "ar_power_config.h"
#define AR_CONFIG_PLAN_BYTES 452
#include "ar_ap_hop.h"   /* struct ar_hop_config: an AP's channel policy */
struct ar_config {
	uint8_t plan[AR_CONFIG_PLAN_BYTES];
	size_t plan_length;
	unsigned search;
	/* Complete available rows and JSON fallback inputs. Factory storage has
	 * not been read merely because this JSON parser succeeded. */
	struct ar_power_calibration calibration;
	struct ar_power_config power;
	unsigned power_source;
	struct ar_hop_config hop;   /* AP only */
};
/* Parse loader JSON into the implemented DEV mode-0 configuration. Board
 * register defaults/pins and FCH encoding keys have separate provenance. */
int ar_config_parse(struct ar_config *out, const char *json, size_t length,
	const uint8_t *fch_keys, size_t keys_length);
void ar_config_identity(struct ar_config *config, const uint8_t mac[4]);
/* Resolve storage before using the initial user-0 gain fields. The supported
 * JSON profile is DEV, high clock and closed loop on both paths. Atomic on
 * failure, including missing rows after signed-offset/clamp selection. */
int ar_config_resolve_power(struct ar_config *config, enum ar_power_factory_state state,
	const uint8_t *record, size_t length);
#endif
