/* SPDX-License-Identifier: MIT */
#include "ar_boot.h"
#include "ar_rf_device.h"
#include "ar_config.h"
#include "ar_json.h"
#include "ar_factory.h"
#include <string.h>

static struct ar_boot_info boot = {.version = 2};
static struct ar_config configured;
const struct ar_boot_info *ar_boot_information(void) { return &boot; }
static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int validate_board(const struct ar_boot_profile *p)
{
	if (!p || !p->defaults || !p->pins || p->defaults_length != 704 * 8 || !p->pins_length || p->pins_length % 4 || p->pins_length > 29 * 4 ||
	    ar_flash_parts_validate(p->flash_parts, p->flash_part_count)) { return -22; }
	/* Reject malformed data before touching hardware; retain write order. */
	for (size_t i = 0; i < p->defaults_length; i += 8) {
		unsigned offset = p->defaults[i] | (unsigned)p->defaults[i + 1] << 8;
		if (offset > 0xdfc || offset % 4 || p->defaults[i + 2] || p->defaults[i + 3]) { return -22; }
	}
	static const uint8_t pin_count[4] = {5, 5, 6, 13};
	uint32_t seen[4] = {0};
	for (size_t i = 0; i < p->pins_length; i += 4) {
		unsigned bank = p->pins[i], pin = p->pins[i + 1];
		if (bank > 3 || pin >= pin_count[bank] || p->pins[i + 2] > 15 || (p->pins[i + 3] && bank < 2)) { return -22; }
		if (seen[bank] & (1u << pin)) { return -22; }
		seen[bank] |= 1u << pin;
	}
	return 0;
}
static int validate(const struct ar_boot_profile *p)
{
	if (validate_board(p) || !p->plan || p->plan_length < 16 + AR_RF_DEV4_PREFIX + 4 ||
	    p->plan_length > 16 + AR_RF_DEV4_PREFIX + AR_RF_MAX_CHANNELS * 4 || p->plan_length % 4) { return -22; }
	const uint8_t *plan = p->plan;
	uint32_t ms = le32(plan + 4);
	if (ms > 60000 || plan[9] || plan[10] || plan[11] || le32(plan + 12) != 76 ||
	    memcmp(plan + 16, "DEV4", 4)) { return -22; }
	return 0;
}

static int initialize(const struct ar_boot_profile *profile, unsigned loader)
{
	int result;
	const uint8_t *plan = loader ? configured.plan : profile->plan;
	size_t plan_length = loader ? configured.plan_length : profile->plan_length;
	boot.rf_bound_ms = le32(plan + 4);
	boot.stage = AR_BOOT_PROBE;
	if ((result = ar_rf_probe())) { goto fail; }
	if (ar_rf_information()->chip_profile != 1) { result = -95; goto fail; }
	if (loader) {
		uint8_t mac[4]; result = ar_rf_board_identity(mac);
		if (result && result != -2) { goto fail; }
		boot.identity_source = result == -2 ? 1 : 2; /* JSON fallback / hardware OTP. */
#ifdef AR_IDENTITY_FROM_JSON
		/* Keep the loader JSON's dev.mac as this receiver's identity (the OTP
		 * value is still read for the boot record). Lets this ground present a
		 * MAC an air unit is already bound to. */
		boot.identity_source = 1;
#else
		if (!result) { ar_config_identity(&configured, mac); }
#endif
#ifdef AR_PINS_BEFORE_FACTORY
		/* Board pin mux first: on this board the factory flash may hang off
		 * pins the mux has to route before the SPI read can see it. */
		boot.stage = AR_BOOT_PINS;
		if ((result = ar_rf_setup_pins(profile->pins, profile->pins_length))) { goto fail; }
#endif
		boot.stage = AR_BOOT_FACTORY;
		uint8_t record[8]; struct ar_factory_info factory = {0};
		result = ar_factory_device_read(profile->flash_parts, profile->flash_part_count, record, &factory);
		boot.factory_stage = factory.stage; boot.factory_error = result;
		boot.flash_capacity = factory.identity.capacity;
		boot.factory_partition = factory.partition.partition_offset;
		boot.flash_id[0] = le32(factory.observed_id);
		boot.flash_id[1] = factory.observed_id[4] | (uint32_t)factory.observed_id[5] << 8 |
			(uint32_t)factory.id_valid << 16;
		boot.flash_source = factory.identity.source;
		/* Only an actual partition/read result permits the original storage
		 * fallback. Unsupported identification or controller setup is a native
		 * integration failure, not proof that factory calibration is absent. */
#ifndef AR_FACTORY_OPTIONAL
		if (result && factory.stage != AR_FACTORY_PARTITION) { goto fail; }
#endif
		enum ar_power_factory_state state = result ? AR_POWER_FACTORY_UNAVAILABLE : AR_POWER_FACTORY_RECORD;
		result = ar_config_resolve_power(&configured, state, result ? NULL : record, result ? 0 : sizeof record);
		if (result) { goto fail; }
		boot.power_source = configured.power_source;
		ar_rf_set_power_config(&configured.power);
		boot.power_offsets[0] = configured.power.path_offset[0];
		boot.power_offsets[1] = configured.power.path_offset[1];
	}
#ifndef AR_PINS_BEFORE_FACTORY
	boot.stage = AR_BOOT_PINS;
	if ((result = ar_rf_setup_pins(profile->pins, profile->pins_length))) { goto fail; }
#endif
	boot.stage = AR_BOOT_DEFAULTS;
	/* Match the validated probe's two-batch sequence before changing scheduling. */
	for (size_t offset = 0; offset < profile->defaults_length; offset += 4096) {
		size_t count = profile->defaults_length - offset;
		if (count > 4096) { count = 4096; }
		if ((result = ar_rf_load_defaults(profile->defaults + offset, count))) { goto fail; }
		boot.default_bytes += count;
	}
	boot.stage = AR_BOOT_CALIBRATE;
	if ((result = ar_rf_calibrate())) { goto fail; }
	boot.stage = AR_BOOT_PSRAM;
	if ((result = ar_rf_setup_psram())) { goto fail; }
	boot.stage = AR_BOOT_PREPARE;
	result = loader ? ar_rf_prepare_config(le32(plan), plan[8], plan + 16, plan_length - 16, configured.search, &configured.power) :
		boot.rf_bound_ms ? ar_rf_prepare_host(le32(plan), boot.rf_bound_ms, plan[8], plan + 16, plan_length - 16) :
		ar_rf_prepare_host_continuous(le32(plan), plan[8], plan + 16, plan_length - 16);
	if (result) { goto fail; }
	boot.stage = AR_BOOT_READY;
	return 0;
fail:
	boot.error = result;
	return result;
}

int ar_boot_prepare(const struct ar_boot_profile *profile)
{
	if (boot.stage != AR_BOOT_IDLE) { return -114; }
	boot.stage = AR_BOOT_VALIDATE;
	int result = validate(profile);
	if (result) { boot.error = result; return result; }
	return initialize(profile, 0);
}
int ar_boot_prepare_loader(const struct ar_boot_profile *profile, const char *json, size_t capacity)
{
	if (boot.stage != AR_BOOT_IDLE) { return -114; }
	boot.stage = AR_BOOT_VALIDATE;
	int result = -22; size_t length = 0;
	if (validate_board(profile) || !json || !capacity || capacity > 16384) { goto fail; }
	/* The loader writes external bytes over embedded JSON without appending
	 * NUL. The old document's suffix may remain; parse one complete object,
	 * as the original loader parser does, within the reserved memory region. */
	struct ar_json object;
	if (ar_json_object_prefix(&object, json, capacity, &length)) { goto fail; }
	boot.config_bytes = length;
	uint32_t crc = 0;
	for (size_t i = 0; i < length; i++) {
		crc ^= (uint8_t)json[i];
		for (unsigned bit = 0; bit < 8; bit++) { crc = crc >> 1 ^ ((crc & 1) ? UINT32_C(0x82f63b78) : 0); }
	}
	boot.config_crc32c = crc;
	result = ar_config_parse(&configured, object.data, object.length, profile->fch_keys, profile->keys_length);
	if (result) { goto fail; }
	return initialize(profile, 1);
fail:
	boot.error = result; return result;
}
