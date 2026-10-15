/* SPDX-License-Identifier: MIT */
#include "ar_power_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char fixture[] =
"{\"baseband\":{\"basic\":{\"power\":{\"pwr_offset\":[-6,6,3,-3],\"csma_offset\":9}},"
"\"power_calibration\":{"
"\"default\":{\"closeloop\":{\"9dbm\":[\"01,\",\"0xFF,\",\"3f\",\"0X80\"],\"39dbm\":[\"02\",\"00\",\"03\",\"01\"]}},"
"\"board.v2\":{\"closeloop\":{\"14dbm\":[\"04\",\"21\",\"05\",\"22\"]}},"
"\"bad-board\":{\"closeloop\":{\"14dbm\":[\"40\",\"10\",\"05\",\"10\"]}}}}}";

static void changed(const char *before, const char *after)
{
	char text[sizeof fixture + 128]; const char *p = strstr(fixture, before); assert(p);
	size_t prefix = p - fixture; memcpy(text, fixture, prefix);
	strcpy(text + prefix, after); strcpy(text + prefix + strlen(after), p + strlen(before));
	struct ar_json root; assert(!ar_json_parse(&root, text, strlen(text)));
	struct ar_power_calibration c; memset(&c, 0xa5, sizeof c); struct ar_power_calibration saved = c;
	assert(ar_power_calibration_load(&c, root, NULL) == -22 && !memcmp(&c, &saved, sizeof c));
}
int main(void)
{
	struct ar_json root; assert(!ar_json_parse(&root, fixture, sizeof fixture - 1));
	struct ar_power_calibration c;
	assert(!ar_power_calibration_load(&c, root, NULL));
	assert(c.valid == (1u | 1u << 30) && c.has_fallback && c.csma_offset == 9 && !c.board_specific);
	assert(!memcmp(c.rows[0], (uint8_t[]){1,255,63,128}, 4));
	assert(!memcmp(c.fallback_offsets, (int8_t[]){-6,6,3,-3}, 4));
	assert(!ar_power_calibration_load(&c, root, "board.v2") && c.board_specific && c.valid == 1u << 5);
	assert(c.rows[5][0] == 4 && c.rows[5][1] == 33);
	assert(!ar_power_calibration_load(&c, root, "absent") && !c.board_specific && c.valid == (1u | 1u << 30));
	struct ar_power_calibration saved_cal = c;
	assert(ar_power_calibration_load(&c, root, "bad-board") == -22 && !memcmp(&c, &saved_cal, sizeof c));
	changed("\"0xFF,\"", "\"100\""); changed("\"0xFF,\"", "\"0x\"");
	changed("\"0xFF,\"", "\"FF,tail\""); changed("\"3f\"", "\"40\"");
	changed("\"3f\"", "\"-1\""); changed("\"3f\"", "63");
	changed("\"03\",\"01\"", "\"03\""); changed("\"03\",\"01\"", "\"03\",\"01\",\"02\"");
	changed("[-6,6,3,-3]", "[-7,6,3,-3]"); changed("[-6,6,3,-3]", "[-6,7,3,-3]");
	changed("[-6,6,3,-3]", "[-6,6]"); changed("[-6,6,3,-3]", "[-6,6,3,-3,0]");
	changed("\"csma_offset\":9", "\"csma_offset\":256");
	changed("\"csma_offset\":9", "\"csma_offset\":-1");
	/* Every possible signed byte is covered independently in all four
	 * positions. Only the odd version-3 bytes in [9,15] are translated. */
	for (unsigned version = 3; version <= 4; version++) {
		for (unsigned position = 0; position < 4; position++) {
			for (unsigned byte = 0; byte < 256; byte++) {
				uint8_t record[8] = {version,0,0xaa,0x55,0,0,0,0}; record[4 + position] = byte;
				int8_t offsets[4]; assert(!ar_power_factory_decode(offsets, record, 8));
				int expected = byte < 128 ? (int)byte : (int)byte - 256;
				if (version == 3 && (position & 1) && expected >= 9 && expected <= 15) { expected -= 12; }
				for (unsigned i = 0; i < 4; i++) { assert(offsets[i] == (i == position ? expected : 0)); }
			}
		}
	}
	int8_t offsets[4] = {1,2,3,4}; const int8_t saved_offsets[4] = {1,2,3,4};
	uint8_t record[8] = {2,0,0,0,6,9,5,15};
	assert(ar_power_factory_decode(offsets, record, 8) == -2);
	assert(ar_power_factory_decode(offsets, record, 7) == -22);
	assert(ar_power_factory_decode(offsets, NULL, 8) == -22);
	assert(!memcmp(offsets, saved_offsets, 4));
	struct ar_power_config config = {.closed={1,1}, .high_clock=1, .dev_role=1, .user_offset={2}};
	struct ar_power_config saved = config;
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_UNREAD, NULL, 0) == -11);
	assert(!memcmp(&config, &saved, sizeof config));
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_UNAVAILABLE, NULL, 0) == AR_POWER_OFFSET_JSON);
	assert(config.path_offset[0] == -6 && config.path_offset[1] == 3 && config.user_offset[0] == 2);
	assert(config.closed[0] && config.closed[1] && config.high_clock && config.dev_role && config.csma_offset == 9);
	assert(config.valid == c.valid && !memcmp(config.calibration, c.rows, sizeof c.rows));
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_RECORD, record, 8) == AR_POWER_OFFSET_JSON);
	record[0] = 3;
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_RECORD, record, 8) == AR_POWER_OFFSET_FACTORY_V3);
	assert(config.path_offset[0] == 6 && config.path_offset[1] == 5);
	record[0] = 4;
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_RECORD, record, 8) == AR_POWER_OFFSET_FACTORY_V4);
	saved = config;
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_JSON_ONLY, record, 8) == -22);
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_RECORD, record, 7) == -22);
	assert(!memcmp(&config, &saved, sizeof config));
	c.has_fallback = 0;
	assert(ar_power_calibration_resolve(&config, &c, AR_POWER_FACTORY_JSON_ONLY, NULL, 0) == AR_POWER_OFFSET_DEFAULT);
	assert(!config.path_offset[0] && !config.path_offset[1]);
	puts("power table selection, sparse rows, factory versions, precedence and atomic failure passed");
}
