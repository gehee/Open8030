/* SPDX-License-Identifier: MIT */
#include "ar_sfdp.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int ar_sfdp_discover(ar_flash_read_fn read, void *context, uint8_t *scratch,
	size_t scratch_length, struct ar_sfdp_info *out)
{
	if (!read || !scratch || scratch_length < AR_SFDP_SCRATCH_BYTES || !out) { return -22; }
	int result = read(context, 0, scratch, 8);
	if (result) { return result; }
	if (memcmp(scratch, "SFDP", 4) || scratch[5] != 1) { return -74; }
	unsigned headers = (unsigned)scratch[6] + 1;
	result = read(context, 8, scratch, headers * 8);
	if (result) { return result; }
	/* The matched loader requires the first header to be a major-1 BFPT. */
	if (scratch[0] || scratch[7] != 0xff || scratch[2] != 1) { return -74; }
	const uint8_t *selected = scratch;
	for (unsigned i = 1; i < headers; i++) {
		const uint8_t *p = scratch + i * 8;
		if (!p[0] && p[7] == 0xff && p[2] == 1 &&
		    (p[1] > selected[1] || (p[1] == selected[1] && p[3] > selected[3]))) { selected = p; }
	}
	struct ar_sfdp_info info = {.minor=selected[1], .major=selected[2], .table_words=selected[3],
		.table_offset=selected[4] | (uint32_t)selected[5] << 8 | (uint32_t)selected[6] << 16};
	if (info.table_words < 9 || (uint32_t)info.table_words * 4 > 0x1000000u - info.table_offset) { return -74; }
	/* The first nine words are mandatory in the original parser. Subsequent
 * write/erase/quad-mode fields are not needed by this read-only transport. */
	result = read(context, info.table_offset, scratch, 36);
	if (result) { return result; }
	info.address_mode = (le32(scratch) >> 17) & 3;
	if (info.address_mode > 1) { return -95; } /* Four-byte-only or reserved. */
	uint32_t density = le32(scratch + 4);
	uint64_t bits;
	if (density & UINT32_C(0x80000000)) {
		unsigned exponent = density & UINT32_C(0x7fffffff);
		if (exponent < 3 || exponent > 63) { return -74; }
		bits = UINT64_C(1) << exponent;
	} else { bits = (uint64_t)density + 1; }
	if ((bits & 7) || bits / 8 < 0xa000) { return -74; }
	info.physical_bytes = bits / 8;
	info.readable_bytes = info.physical_bytes > 0x1000000 ? 0x1000000 : (uint32_t)info.physical_bytes;
	*out = info; return 0;
}
