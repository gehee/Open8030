/* SPDX-License-Identifier: MIT */
#ifndef AR_BOOT_H
#define AR_BOOT_H
#include <stddef.h>
#include <stdint.h>
#include "ar_flash_spi.h"

/* Explicit board facts, not original firmware code or a generic board profile. */
struct ar_boot_profile {
	const uint8_t *defaults, *pins, *plan;
	size_t defaults_length, pins_length, plan_length;
	const uint8_t *fch_keys;
	size_t keys_length;
	const struct ar_flash_part *flash_parts;
	size_t flash_part_count;
};
enum ar_boot_stage {
	AR_BOOT_IDLE, AR_BOOT_VALIDATE, AR_BOOT_PROBE, AR_BOOT_PINS,
	AR_BOOT_DEFAULTS, AR_BOOT_CALIBRATE, AR_BOOT_PSRAM, AR_BOOT_PREPARE,
	AR_BOOT_READY, AR_BOOT_FACTORY
};
struct ar_boot_info {
	uint32_t version, stage;
	int32_t error;
	uint32_t default_bytes, rf_bound_ms, config_bytes, config_crc32c, identity_source;
	uint32_t factory_stage;
	int32_t factory_error;
	uint32_t flash_capacity, factory_partition, power_source;
	int32_t power_offsets[2];
	uint32_t flash_id[2], flash_source;
};
/* Initializes with RF held; only the real backend's candidate command starts RF.
 * Stops at the first failure. It does not recover/restart an active session. */
int ar_boot_prepare(const struct ar_boot_profile *profile);
/* Normal loader region, bounded at the next executable segment. */
int ar_boot_prepare_loader(const struct ar_boot_profile *profile, const char *json, size_t capacity);
const struct ar_boot_info *ar_boot_information(void);
#ifdef AR_BOOT_EMBEDDED
extern const struct ar_boot_profile ar_boot_embedded_profile;
#endif
#endif
