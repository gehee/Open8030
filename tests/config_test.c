/* SPDX-License-Identifier: MIT */
#include "ar_config.h"
#include "ar_dev_mcs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_fixture.h"
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
int main(void)
{
	for (unsigned i = 0; i < 25; i++) { keys[i*4] = i % 3; keys[i*4+1] = i/3%3; keys[i*4+2] = i/9; }
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
	changed("\"init_chan\":-1", "\"init_chan\":2", 0);
	changed("\"init_chan\":-1", "\"init_chan\":3", -22);
	changed("5220000", "4915000", -22);
	changed("\"role\":\"dev\"", "\"role\":\"ap\"", -95);
	changed("\"hold_time\":101", "\"hold_time\":65536", -22);
	changed("\"hold_time\":101", "\"hold_time\":101,\"hold_time\":102", -22);
	changed("\"mac\":\"12345678\"", "\"mac\":\"1234567z\"", -22);
	changed("\"bw_auto\":{\"enable\":false}", "\"bw_auto\":{\"enable\":true}", -95);
	changed("\"4A,\"", "\"4A,garbage\"", -22);
	/* A malformed unused row cannot become a future unchecked power setting. */
	changed("\"25dbm\":", "\"14dbm\":[\"40\",\"10\",\"10\",\"10\"],\"25dbm\":", -22);
	changed("\"mcs\":0", "\"mcs\":11", -95);
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
