/* SPDX-License-Identifier: MIT */
#ifndef AR_FLASH_SPI_H
#define AR_FLASH_SPI_H
#include "ar_flash.h"
#include "ar_sfdp.h"

struct ar_flash_spi_io {
	uint32_t (*read32)(void *context, uint32_t address);
	void (*write32)(void *context, uint32_t address, uint32_t value);
	uint64_t (*time_us)(void *context);
};
struct ar_flash_spi {
	const struct ar_flash_spi_io *io;
	void *context;
	uint32_t capacity, transfers, bytes, last_offset, last_opcode;
	int error;
	unsigned ready;
	uint8_t observed_id[6];
	unsigned id_valid;
};
struct ar_flash_part { uint8_t id[6], id_length, reserved; uint32_t capacity; };
int ar_flash_parts_validate(const struct ar_flash_part *parts, size_t count);
enum ar_flash_identity_source { AR_FLASH_ID_TABLE=1, AR_FLASH_ID_SFDP=2 };
struct ar_flash_identity {
	uint8_t id[6], source, reserved;
	uint32_t capacity, part_index;
	struct ar_sfdp_info sfdp;
};
/* Read-only single-lane protocol. Capacity must come from identified board/
 * flash facts, or be zero until discovery. No erase, write-enable, status
 * write or flash mode-change command is available. Calls must be serialized. */
int ar_flash_spi_init(struct ar_flash_spi *spi, const struct ar_flash_spi_io *io,
	void *context, uint32_t capacity);
int ar_flash_spi_read_id(struct ar_flash_spi *spi, uint8_t out[6]);
/* Read ID and SFDP without permitting array reads before capacity is known.
 * Failure clears capacity; a transport error also latches the SPI fault.
 * Outputs are unchanged on failure. Scratch can be reused by factory reads. */
int ar_flash_spi_discover(struct ar_flash_spi *spi, uint8_t *scratch, size_t length,
	uint8_t id[6], struct ar_sfdp_info *info);
/* Known read-only geometries are release-bound board-profile data. First ID
 * prefix match wins, as in the original. Unknown IDs require valid SFDP; no
 * guessed default geometry is installed. Output is atomic on failure. */
int ar_flash_spi_identify(struct ar_flash_spi *spi, const struct ar_flash_part *parts,
	size_t count, uint8_t *scratch, size_t length, struct ar_flash_identity *identity);
/* ar_flash_read_fn compatible. Failure may leave partial bytes in out, but
 * latches the transport fault and prevents subsequent commands until init.
 * The factory reader above publishes a record only after a complete read. */
int ar_flash_spi_read(void *spi, uint32_t offset, uint8_t *out, size_t length);
#endif
