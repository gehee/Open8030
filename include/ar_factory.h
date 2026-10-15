/* SPDX-License-Identifier: MIT */
#ifndef AR_FACTORY_H
#define AR_FACTORY_H
#include "ar_flash_spi.h"

enum ar_factory_stage {
	AR_FACTORY_IDLE, AR_FACTORY_CONTROLLER, AR_FACTORY_IDENTIFY,
	AR_FACTORY_PARTITION, AR_FACTORY_READY
};
struct ar_factory_info {
	uint32_t stage;
	int32_t error;
	struct ar_flash_identity identity;
	struct ar_flash_factory_info partition;
	uint32_t transfers, bytes, last_offset, last_opcode;
	uint8_t observed_id[6], id_valid, reserved;
};
/* A serialized, read-only attempt. The record is unchanged on failure and the
 * stage distinguishes unsupported identification from actual partition IO.
 * The caller owns scratch and decides whether an observed failure permits a
 * calibration fallback. No absent-storage conclusion is implied by init. */
int ar_factory_read(const struct ar_flash_spi_io *io, void *context,
	const struct ar_flash_part *parts, size_t count, uint8_t *scratch, size_t length,
	uint8_t record[8], struct ar_factory_info *info);
/* Physical GND binding, called once during boot with RF held. */
int ar_factory_device_read(const struct ar_flash_part *parts, size_t count,
	uint8_t record[8], struct ar_factory_info *info);
#endif
