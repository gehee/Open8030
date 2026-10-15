/* SPDX-License-Identifier: MIT */
#ifndef AR_SFDP_H
#define AR_SFDP_H
#include "ar_flash.h"

#define AR_SFDP_SCRATCH_BYTES 2048u
struct ar_sfdp_info {
	uint64_t physical_bytes;
	uint32_t readable_bytes, table_offset;
	uint8_t minor, major, table_words, address_mode;
};
/* Callback reads SFDP address space, not the flash array. Selects the newest
 * major-1 basic table, breaking minor-version ties by table length, as in the
 * matched loader. Only three-byte array addressing is implemented. Published
 * capacity is limited to that address space; physical size is retained.
 * Output is unchanged on errors. Scratch is caller-owned and reusable for GPT. */
int ar_sfdp_discover(ar_flash_read_fn read, void *context, uint8_t *scratch,
	size_t scratch_length, struct ar_sfdp_info *out);
#endif
