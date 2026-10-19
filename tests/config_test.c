/* SPDX-License-Identifier: MIT */
#include "ar_config.h"
#include "ar_dev_mcs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_fixture.h"
#include "ap_config_fixture.h"
static uint8_t keys[100];
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void changed(const char *before, const char *after, int expected)
{
	char text[sizeof config_fixture + 256]; const char *p = strstr(config_fixture, before); assert(p);
	size_t prefix = p - config_fixture; memcpy(text, config_fixture, prefix);
	strcpy(text + prefix, after); strcpy(text + prefix + strlen(after), p + strlen(before));
	struct ar_config c; memset(&c, 0xa5, sizeof c); struct ar_config saved = c;
	assert(ar_config_parse(&c, text, strlen(text), keys, sizeof keys) == expected);
	if (expected) { assert(!memcmp(&c, &saved, sizeof c)); }
}
static struct ar_config dev_parsed;
static int dev_changed(const char *before, const char *after)
{
	char text[sizeof config_fixture + 256]; const char *p = strstr(config_fixture, before); assert(p);
	size_t prefix = p - config_fixture; memcpy(text, config_fixture, prefix);
	strcpy(text + prefix, after); strcpy(text + prefix + strlen(after), p + strlen(before));
	return ar_config_parse(&dev_parsed, text, strlen(text), keys, sizeof keys);
}
static struct ar_config ap_parsed;
static int ap_changed(const char *before, const char *after)
{
	char text[sizeof ap_config_fixture + 256]; const char *p = strstr(ap_config_fixture, before); assert(p);
	size_t prefix = p - ap_config_fixture; memcpy(text, ap_config_fixture, prefix);
	strcpy(text + prefix, after); strcpy(text + prefix + strlen(after), p + strlen(before));
	return ar_config_parse(&ap_parsed, text, strlen(text), keys, sizeof keys);
}
static void ap_tests(void)
{
	struct ar_config c;
	assert(!ar_config_parse(&c, ap_config_fixture, sizeof ap_config_fixture - 1, keys, sizeof keys));
	/* AP04 plan: init_chan -1 starts on 5914 MHz (index 1), not a search. */
	assert(!memcmp(c.plan + 16, "AP04", 4) && !c.search && le32(c.plan) == 5914000 && c.plan[16 + 21] == 1);
	assert(!memcmp(c.plan + 20, (uint8_t[]){0xa1,0xb2,0xc3,0xd4}, 4) && c.plan[8] == (0xa1^0xb2^0xc3^0xd4));
	assert(c.plan[16 + 20] == 0 && c.plan[16 + 28] == 10 && c.plan[16 + 29] == 100);
	/* Two calibration groups; 5914 MHz is above the 5850 MHz edge: the second. */
	assert(c.calibration.groups == 2 && c.calibration.sub_band_khz == 5850000);
	assert(!memcmp(c.plan + 40, (uint8_t[]){0x1d,0x37,0x29,0x35}, 4));
	assert(!memcmp(c.calibration.rows[1], (uint8_t[]){0x23,0x31,0x29,0x35}, 4));
	struct ar_dev_mcs_config m;
	assert(!ar_dev_mcs_decode_config(&m, c.plan + 48, 136) && m.count == 2 && m.hold_ms == 1000 && m.rows[0].mcs == 1);
	assert(!memcmp(c.plan + 184, keys, 100) && c.plan_length == 284 + 12);
	/* No factory record on the air unit's radio: JSON rows, AP role. */
	assert(!ar_config_resolve_power(&c, AR_POWER_FACTORY_UNAVAILABLE, NULL, 0));
	assert(c.power.dev_role == 0 && c.power_source == AR_POWER_OFFSET_DEFAULT);
	/* Below the edge the first group; a fixed init_chan is kept. */
	assert(!ap_changed("\"init_chan\":-1", "\"init_chan\":0"));
	assert(le32(ap_parsed.plan) == 5839000 && !memcmp(ap_parsed.plan + 40, (uint8_t[]){0x23,0x31,0x29,0x35}, 4));
	/* What the AP path does not run is refused. */
	assert(ap_changed("\"tx_mode\":\"2TX_STBC\"", "\"tx_mode\":\"2TX\"") == -95);
	assert(ap_changed("\"rx_mode\":\"1T1R\"", "\"rx_mode\":\"2T2R_STBC\"") == -95);
	assert(ap_changed("\"mode\":\"1V1\"", "\"mode\":\"1V3\"") == -95);
	assert(ap_changed("\"pwr_init\":10", "\"pwr_init\":11") == -22);   /* no 11 dBm row */
	assert(ap_changed("\"retx_count\":100", "\"retx_count\":128") == -22);
	assert(ap_changed("\"1D\",\"37\",\"29\",\"35\"]}", "\"1D\",\"37\",\"29\"]}") == -22); /* 7 values */
	assert(ap_changed("[5850000,5990000]", "[]") == -22);
	/* The channel policy: stock's defaults, then the configuration's. */
	assert(c.hop.measure_start && !c.hop.auto_start && c.hop.blind_errors == 16 && c.hop.blind_ms == 3000 && c.hop.tier_count == 3);
	assert(c.hop.tiers[0][0] == 3 && c.hop.tiers[0][1] == 20 && c.hop.tiers[2][0] == 9 && c.hop.tiers[2][1] == 5);
	assert(!ap_changed("\"rc_hop\":{\"enable\":false}}", "\"rc_hop\":{\"enable\":false},"
		"\"hop_ex\":{\"blind_hop\":{\"at_err_cnt\":100,\"duration\":2000}},"
		"\"hop_para\":[{\"retx_cnt\":3,\"power_diff\":20},{\"retx_cnt\":6,\"power_diff\":10},"
		"{\"retx_cnt\":9,\"power_diff\":5},{\"retx_cnt\":16,\"power_diff\":1}]}"));
	assert(ap_parsed.hop.blind_errors == 100 && ap_parsed.hop.blind_ms == 2000 && ap_parsed.hop.tier_count == 4);
	assert(ap_parsed.hop.tiers[3][0] == 16 && ap_parsed.hop.tiers[3][1] == 1);
	assert(ap_changed("\"rc_hop\":{\"enable\":false}}", "\"rc_hop\":{\"enable\":false},"
		"\"hop_para\":[{\"retx_cnt\":3}]}") == -22);
	assert(!ap_changed("\"init_chan\":-1", "\"init_chan\":2") && !ap_parsed.hop.measure_start);
	assert(!ap_changed("\"init_band\":\"5G\",\"mode\":\"manu\"", "\"init_band\":\"5G\",\"mode\":\"auto\"") &&
		ap_parsed.hop.auto_start);
	assert(ap_changed("\"init_band\":\"5G\",\"mode\":\"manu\"", "\"init_band\":\"5G\",\"mode\":\"fast\"") == -95);
}
int main(void)
{
	for (unsigned i = 0; i < 25; i++) { keys[i*4] = i % 3; keys[i*4+1] = i/3%3; keys[i*4+2] = i/9; }
	ap_tests();
	struct ar_config c;
	assert(!ar_config_parse(&c, config_fixture, sizeof config_fixture-1, keys, sizeof keys));
	assert(c.search && c.plan_length == 296 && le32(c.plan) == 4915000 && !le32(c.plan+4));
	assert(!memcmp(c.plan+20, (uint8_t[]){0x12,0x34,0x56,0x78}, 4));
	assert(!memcmp(c.plan+24, (uint8_t[]){0x87,0x65,0x43,0x21}, 4));
	for (unsigned i=28;i<38;i++) { assert(!c.plan[i]); }
	assert(!memcmp(c.plan+40, (uint8_t[]){17,74,18,75,25,7,0,0}, 8));
	assert(c.calibration.valid == 1u << 16 && c.calibration.csma_offset == 8 && !c.calibration.has_fallback);
	assert(!memcmp(c.calibration.rows[16], c.plan + 40, 4));
	struct ar_config unresolved=c;
	assert(ar_config_resolve_power(&c,AR_POWER_FACTORY_UNREAD,NULL,0)==-11 && !memcmp(&c,&unresolved,sizeof c));
	assert(!ar_config_resolve_power(&c,AR_POWER_FACTORY_UNAVAILABLE,NULL,0));
	assert(c.power_source==AR_POWER_OFFSET_DEFAULT && !memcmp(c.plan,unresolved.plan,sizeof c.plan));
	struct ar_config resolved=c; uint8_t factory[8]={4,0,0,0,1,0,0,0};
	assert(ar_config_resolve_power(&c,AR_POWER_FACTORY_RECORD,factory,8)==-95 && !memcmp(&c,&resolved,sizeof c));
	assert(ar_config_resolve_power(&c,AR_POWER_FACTORY_RECORD,factory,7)==-22 && !memcmp(&c,&resolved,sizeof c));
	factory[4]=0;
	assert(!ar_config_resolve_power(&c,AR_POWER_FACTORY_RECORD,factory,8) && c.power_source==AR_POWER_OFFSET_FACTORY_V4);
	struct ar_dev_mcs_config m;
	assert(!ar_dev_mcs_decode_config(&m,c.plan+48,136));
	assert(m.enabled && m.automatic && m.count==2 && m.hold_ms==101 && m.max_wait_ms==4001 && !m.delay_samples);
	assert(m.rows[1].mcs==2 && m.rows[1].snr_down==60 && m.rows[1].up_ms==902);
	assert(!memcmp(c.plan+184,keys,100));
	ar_config_identity(&c,(uint8_t[]){1,2,3,5}); assert(c.plan[8]==5 && !memcmp(c.plan+20,(uint8_t[]){1,2,3,5},4));
	/* Open8030: dist_calc.offset (raw units, default 20) rides in the MCS
	 * block's byte 12 next to the enable in byte 10; MCS rows reach 13. */
	assert(m.distance_enabled == 1 && m.distance_offset == 20);
	{
		const char *before = "\"dist_calc\":{\"enable\":true}";
		char text[sizeof config_fixture + 64]; const char *at = strstr(config_fixture, before); assert(at);
		size_t prefix = at - config_fixture; memcpy(text, config_fixture, prefix);
		const char *after = "\"dist_calc\":{\"enable\":true,\"offset\":59}";
		strcpy(text + prefix, after); strcpy(text + prefix + strlen(after), at + strlen(before));
		struct ar_config o; struct ar_dev_mcs_config om;
		assert(!ar_config_parse(&o, text, strlen(text), keys, sizeof keys));
		assert(!ar_dev_mcs_decode_config(&om, o.plan + 48, 136) && om.distance_offset == 59);
	}
	changed("\"dist_calc\":{\"enable\":true}", "\"dist_calc\":{\"enable\":true,\"offset\":100}", 0);
	changed("\"dist_calc\":{\"enable\":true}", "\"dist_calc\":{\"enable\":true,\"offset\":101}", -22);
	changed("\"dist_calc\":{\"enable\":true}", "\"dist_calc\":{\"enable\":true,\"offset\":-1}", -22);
	changed("\"mcs\":0", "\"mcs\":13", 0);
	changed("\"init_chan\":-1", "\"init_chan\":2", 0);
	/* The width target: bw_auto.range.high, 20 MHz without one. */
	assert(!dev_changed("\"init_chan\":-1", "\"init_chan\":-1") && dev_parsed.bw_target == 4);
	assert(dev_parsed.bw_min == 2 && dev_parsed.bw_narrow_mcs == 5 && dev_parsed.bw_widen_mcs == 8);
	assert(!dev_changed("\"bw_auto\":{\"enable\":false}", "\"bw_auto\":{\"enable\":false,"
		"\"range\":{\"low\":\"10\",\"high\":\"40\"},\"threshold\":{\"low\":2,\"high\":9}}") &&
		dev_parsed.bw_min == 3 && dev_parsed.bw_target == 5 && dev_parsed.bw_narrow_mcs == 4 && dev_parsed.bw_widen_mcs == 11);
	assert(dev_changed("\"bw_auto\":{\"enable\":false}",
		"\"bw_auto\":{\"enable\":false,\"range\":{\"low\":\"40\",\"high\":\"20\"}}") == -22);
	assert(!dev_changed("\"bw_auto\":{\"enable\":false}",
		"\"bw_auto\":{\"enable\":false,\"range\":{\"low\":\"5\",\"high\":\"40\"}}") && dev_parsed.bw_target == 5);
	assert(dev_changed("\"bw_auto\":{\"enable\":false}",
		"\"bw_auto\":{\"enable\":false,\"range\":{\"high\":\"30\"}}") == -22);
	changed("\"init_chan\":-1", "\"init_chan\":3", -22);
	changed("5220000", "4915000", -22);
	changed("\"role\":\"dev\"", "\"role\":\"ap\"", -22); /* an AP document needs its ap block */
	changed("\"hold_time\":101", "\"hold_time\":65536", -22);
	changed("\"hold_time\":101", "\"hold_time\":101,\"hold_time\":102", -22);
	changed("\"mac\":\"12345678\"", "\"mac\":\"1234567z\"", -22);
	changed("\"bw_auto\":{\"enable\":false}", "\"bw_auto\":{\"enable\":true}", -95);
	changed("\"4A,\"", "\"4A,garbage\"", -22);
	/* A malformed unused row cannot become a future unchecked power setting. */
	changed("\"25dbm\":", "\"14dbm\":[\"40\",\"10\",\"10\",\"10\"],\"25dbm\":", -22);
	changed("\"mcs\":0", "\"mcs\":14", -95);
	changed("\"tintlv_len\":12", "\"tintlv_len\":24", -95);
	changed("\"payload\":true", "\"payload\":false", -95);
	changed("\"tx_mode\":\"2TX\"", "\"tx_mode\":\"1TX\"", -95);
	changed("\"fch_info_len\":96", "\"fch_info_len\":\"96\"", -22);
	assert(ar_config_parse(&c,config_fixture,sizeof config_fixture-2,keys,sizeof keys)==-22);
	struct ar_config saved = c;
	keys[3] = 128;
	assert(ar_config_parse(&c,config_fixture,sizeof config_fixture-1,keys,sizeof keys)==-22);
	keys[3] = 0; memcpy(keys + 4, keys, 4);
	assert(ar_config_parse(&c,config_fixture,sizeof config_fixture-1,keys,sizeof keys)==-22);
	assert(!memcmp(&c,&saved,sizeof c));
	puts("loader configuration channels, calibration, identity, MCS and failure atomicity passed");
}
