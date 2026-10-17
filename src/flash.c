/* SPDX-License-Identifier: MIT */
#include "ar_flash.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }
static uint32_t crc32(const uint8_t *p, size_t size, unsigned zero_begin, unsigned zero_end)
{
	uint32_t crc = UINT32_MAX;
	for (size_t i = 0; i < size; i++) {
		crc ^= i >= zero_begin && i < zero_end ? 0 : p[i];
		for (unsigned b = 0; b < 8; b++) { crc = crc >> 1 ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0); }
	}
	return ~crc;
}
int ar_flash_find_partition(const uint8_t *data, size_t length,
	uint32_t base, uint32_t capacity, const char *name, struct ar_flash_partition *out)
{
	if (!data || length != AR_FLASH_TABLE_BYTES || !name || !out ||
	    (base != 0x8000 && base != 0x9000) || capacity < base || capacity - base < length) { return -22; }
	size_t name_length = 0;
	while (name_length < 37 && name[name_length]) { name_length++; }
	if (!name_length || name_length > 36) { return -22; }
	if (data[510] != 0x55 || data[511] != 0xaa || memcmp(data + 512, "EFI PART", 8) ||
	    le32(data + 520) != 0x10000 || le32(data + 524) != 92 || le32(data + 596) != 128) { return -74; }
	unsigned count = le32(data + 592);
	if (count > (AR_FLASH_TABLE_BYTES - 1024) / 128 ||
	    le32(data + 528) != crc32(data + 512, 92, 16, 20) ||
	    le32(data + 600) != crc32(data + 1024, count * 128, 0, 0)) { return -74; }
	unsigned found = 0; struct ar_flash_partition selected = {0};
	for (unsigned i = 0; i < count; i++) {
		const uint8_t *entry = data + 1024 + i * 128;
		uint64_t first = le64(entry + 32), last = le64(entry + 40);
		/* The original prefix ends at a non-increasing LBA pair. Retain
		 * that rule, including its exclusion of single-sector entries. */
		if (last <= first) { break; }
		/* Check before multiplication/addition; original truncation of high
		 * LBA words is not a valid native flash address. */
		uint64_t sectors = (capacity - base) / 512;
		unsigned match = 1;
		for (size_t j = 0; j < name_length; j++) {
			if (entry[56 + j * 2] != (uint8_t)name[j] || entry[57 + j * 2]) { match = 0; break; }
		}
		if (match && name_length < 36 && (entry[56 + name_length * 2] || entry[57 + name_length * 2])) { match = 0; }
		if (match) {
			/* Only the selected partition must fit: a USB ground unit's table lists
			 * app1 up to 0x210000 on a 2 MiB part, which stock never checks. */
			if (last >= sectors) { return -74; }
			if (found) { return -74; }
			selected.offset = base + (uint32_t)(first * 512);
			selected.length = (uint32_t)((last - first + 1) * 512); found = 1;
		}
	}
	if (!found) { return -2; }
	*out = selected; return 0;
}
int ar_flash_read_factory(ar_flash_read_fn read, void *context, uint32_t capacity,
	uint8_t *scratch, size_t scratch_length, uint8_t record[8], struct ar_flash_factory_info *info)
{
	if (!read || !scratch || scratch_length < AR_FLASH_TABLE_BYTES || !record || !info || capacity < 0xa000) { return -22; }
	struct ar_flash_factory_info state = {0};
	int result = -74;
	for (unsigned attempt = 0; attempt < 2; attempt++) {
		uint32_t base = 0x8000 + attempt * 0x1000;
		struct ar_flash_partition partition;
		state.table_attempts++;
		result = read(context, base, scratch, AR_FLASH_TABLE_BYTES);
		if (!result) { result = ar_flash_find_partition(scratch, AR_FLASH_TABLE_BYTES, base, capacity, "factory", &partition); }
		state.table_errors[attempt] = result;
		if (result && result != -2) { continue; }
		state.table_base = base;
		if (result == -2) { break; }
		state.partition_offset = partition.offset; state.partition_length = partition.length;
		uint8_t bytes[8]; result = read(context, partition.offset, bytes, sizeof bytes);
		state.record_error = result;
		if (!result) { memcpy(record, bytes, sizeof bytes); }
		break;
	}
	*info = state; return result;
}
