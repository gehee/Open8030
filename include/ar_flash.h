/* SPDX-License-Identifier: MIT */
#ifndef AR_FLASH_H
#define AR_FLASH_H
#include <stddef.h>
#include <stdint.h>

#define AR_FLASH_TABLE_BYTES 4096u
struct ar_flash_partition { uint32_t offset, length; };
struct ar_flash_factory_info {
	uint32_t table_attempts, table_base, partition_offset, partition_length;
	int32_t table_errors[2], record_error;
};
/* Matched radio layout, not generic GPT: header at window+512, 128-byte
 * entries at +1024, and partition LBAs relative to the window's flash base.
 * Returns -2 for a valid table without the name, -74 for invalid metadata.
 * Output is unchanged on failure. Neither function writes flash. */
int ar_flash_find_partition(const uint8_t *window, size_t length,
	uint32_t base, uint32_t capacity, const char *name, struct ar_flash_partition *out);
typedef int (*ar_flash_read_fn)(void *context, uint32_t offset, uint8_t *out, size_t length);
/* Caller owns the 4 KiB scratch area. Callback success means all requested
 * bytes were read. A valid primary table is authoritative even if the named
 * partition is absent; only invalid/unreadable tables select the backup.
 * The record is copied out only after a successful complete eight-byte read.
 * Diagnostics record failed attempts as well as a successful selection. */
int ar_flash_read_factory(ar_flash_read_fn read, void *context, uint32_t capacity,
	uint8_t *scratch, size_t scratch_length, uint8_t record[8], struct ar_flash_factory_info *info);
#endif
