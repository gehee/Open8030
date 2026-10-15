/* SPDX-License-Identifier: MIT */
#include "ar_factory.h"
#include <string.h>

int ar_factory_read(const struct ar_flash_spi_io *io, void *context,
	const struct ar_flash_part *parts, size_t count, uint8_t *scratch, size_t length,
	uint8_t record[8], struct ar_factory_info *info)
{
	if (!info || !record || !scratch || length < AR_FLASH_TABLE_BYTES ||
	    !io || !io->read32 || !io->write32 || !io->time_us || ar_flash_parts_validate(parts, count)) { return -22; }
	*info = (struct ar_factory_info){.stage=AR_FACTORY_CONTROLLER};
	struct ar_flash_spi spi = {0};
	int result = ar_flash_spi_init(&spi, io, context, 0);
	if (result) { goto done; }
	info->stage = AR_FACTORY_IDENTIFY;
	result = ar_flash_spi_identify(&spi, parts, count, scratch, length, &info->identity);
	if (result) { goto done; }
	info->stage = AR_FACTORY_PARTITION;
	result = ar_flash_read_factory(ar_flash_spi_read, &spi, spi.capacity,
		scratch, length, record, &info->partition);
	if (!result) { info->stage = AR_FACTORY_READY; }
done:
	info->error = result; info->transfers = spi.transfers; info->bytes = spi.bytes;
	info->last_offset = spi.last_offset; info->last_opcode = spi.last_opcode;
	memcpy(info->observed_id, spi.observed_id, sizeof info->observed_id); info->id_valid = spi.id_valid;
	return result;
}
