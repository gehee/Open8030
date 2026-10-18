/* SPDX-License-Identifier: MIT */
#include "ar_power_config.h"
#include <string.h>

static int hex_digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
		c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static int calibration_byte(struct ar_json value, uint8_t *out)
{
	char text[8];
	if (ar_json_string(value, text, sizeof text)) { return -22; }
	unsigned i = !strncmp(text, "0x", 2) || !strncmp(text, "0X", 2) ? 2 : 0;
	unsigned first = i, n = 0;
	for (; text[i] && text[i] != ','; i++) {
		int digit = hex_digit(text[i]);
		if (digit < 0 || n > 15) { return -22; } n = n * 16 + digit;
	}
	if (i == first || (text[i] && text[i + 1])) { return -22; }
	*out = n; return 0;
}
int ar_power_calibration_load(struct ar_power_calibration *out,
	struct ar_json root, const char *board)
{
	if (!out) { return -22; }
	struct ar_power_calibration c = {.csma_offset = 8};
	struct ar_json tables, selected, rows, row, value;
	if (ar_json_path(root, "baseband.power_calibration", &tables)) { return -22; }
	int error = -2;
	if (board && *board) { error = ar_json_member(tables, board, &selected); }
	if (error == -2) { error = ar_json_member(tables, "default", &selected); }
	else if (!error) { c.board_specific = 1; }
	if (error || ar_json_member(selected, "closeloop", &rows)) { return -22; }
	for (unsigned level = 9; level <= 39; level++) {
		char key[6]; unsigned n = 0;
		if (level >= 10) { key[n++] = '0' + level / 10; }
		key[n++] = '0' + level % 10; memcpy(key + n, "dbm", 4);
		error = ar_json_member(rows, key, &row);
		if (error == -2) { continue; }
		if (error) { return -22; }
		for (unsigned i = 0; i < 4; i++) {
			if (ar_json_index(row, i, &value) || calibration_byte(value, &c.rows[level - 9][i])) { return -22; }
			if (!(i & 1) && c.rows[level - 9][i] > 63) { return -22; }
		}
		/* Four values, or eight: two sub-band groups of four. Every row
		 * must have the same count. */
		unsigned groups = ar_json_index(row, 4, &value) == -2 ? 1 : 2;
		if (groups == 2) {
			for (unsigned i = 0; i < 4; i++) {
				if (ar_json_index(row, 4 + i, &value) || calibration_byte(value, &c.rows_high[level - 9][i])) { return -22; }
				if (!(i & 1) && c.rows_high[level - 9][i] > 63) { return -22; }
			}
			if (ar_json_index(row, 8, &value) != -2) { return -22; }
		}
		if (c.groups && c.groups != groups) { return -22; }
		c.groups = (uint8_t)groups;
		c.valid |= UINT32_C(1) << (level - 9);
	}
	if (!c.valid) { return -22; }
	if (c.groups == 2) {
		/* The first sub-band edge splits the groups (a stock air config: 5850 MHz;
		 * its AP at 5839 MHz runs the first group). */
		struct ar_json bands, edge; int32_t khz;
		if (ar_json_member(selected, "sub_bands", &bands) || ar_json_index(bands, 0, &edge) ||
		    ar_json_integer(edge, &khz) || khz < 1000000) { return -22; }
		c.sub_band_khz = (uint32_t)khz;
	} else { c.groups = 1; }
	error = ar_json_path(root, "baseband.basic.power.pwr_offset", &row);
	if (!error) {
		for (unsigned i = 0; i < 4; i++) {
			int32_t v;
			if (ar_json_index(row, i, &value) || ar_json_integer(value, &v) || v < -6 || v > 6) { return -22; }
			c.fallback_offsets[i] = v;
		}
		if (ar_json_index(row, 4, &value) != -2) { return -22; }
		c.has_fallback = 1;
	} else if (error != -2) { return -22; }
	error = ar_json_path(root, "baseband.basic.power.csma_offset", &value);
	if (!error) {
		int32_t v; if (ar_json_integer(value, &v) || v < 0 || v > 255) { return -22; } c.csma_offset = v;
	} else if (error != -2) { return -22; }
	*out = c; return 0;
}
int ar_power_factory_decode(int8_t out[4], const uint8_t *record, size_t length)
{
	if (!out || !record || length != 8) { return -22; }
	unsigned version = record[0] | (unsigned)record[1] << 8;
	if (version != 3 && version != 4) { return -2; }
	int8_t decoded[4];
	for (unsigned i = 0; i < 4; i++) {
		/* Express two's-complement byte decoding without an out-of-range
		 * unsigned-to-signed conversion. Original reserved bytes are ignored. */
		int n = record[i + 4] < 128 ? record[i + 4] : (int)record[i + 4] - 256;
		if (version == 3 && (i & 1) && n >= 9 && n <= 15) { n -= 12; }
		decoded[i] = n;
	}
	memcpy(out, decoded, sizeof decoded); return 0;
}
int ar_power_calibration_resolve(struct ar_power_config *out,
	const struct ar_power_calibration *cal, enum ar_power_factory_state state,
	const uint8_t *record, size_t length)
{
	if (!out || !cal || !cal->valid || (cal->valid & UINT32_C(0x80000000))) { return -22; }
	if (state == AR_POWER_FACTORY_UNREAD) { return -11; }
	if (state < AR_POWER_FACTORY_UNAVAILABLE || state > AR_POWER_FACTORY_JSON_ONLY ||
	    (state != AR_POWER_FACTORY_RECORD && (record || length))) { return -22; }
	int8_t offsets[4] = {0};
	int source = cal->has_fallback ? AR_POWER_OFFSET_JSON : AR_POWER_OFFSET_DEFAULT;
	if (cal->has_fallback) { memcpy(offsets, cal->fallback_offsets, sizeof offsets); }
	if (state == AR_POWER_FACTORY_RECORD) {
		int result = ar_power_factory_decode(offsets, record, length);
		if (result && result != -2) { return result; }
		if (!result) { source = record[0] == 3 ? AR_POWER_OFFSET_FACTORY_V3 : AR_POWER_OFFSET_FACTORY_V4; }
	}
	struct ar_power_config c = *out;
	memcpy(c.calibration, cal->rows, sizeof c.calibration); c.valid = cal->valid;
	c.path_offset[0] = offsets[0]; c.path_offset[1] = offsets[2]; c.csma_offset = cal->csma_offset;
	*out = c; return source;
}
