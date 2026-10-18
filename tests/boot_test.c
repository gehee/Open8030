/* SPDX-License-Identifier: MIT */
#include "ar_boot.h"
#include "ar_rf_device.h"
#include "ar_factory.h"
#include "ar_power_config.h"
#include "config_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static uint8_t defaults[5632], pins[120], plan[288];
static size_t expected_pins = 40;
static unsigned calls, fail_at, bad_chip, loader_scenario;
static uint8_t keys[100];
static struct ar_rf_info radio;
static unsigned factory_calls;
static int step(unsigned expected)
{
	assert(++calls == expected);
	return calls == fail_at ? -110 : 0;
}
void ar_rf_set_power_config(const struct ar_power_config *config) { (void)config; }
void ar_rf_set_power_bands(const uint8_t (*high)[4], uint32_t edge_khz) { (void)high; (void)edge_khz; }
void ar_rf_set_hop_config(const struct ar_hop_config *hop) { (void)hop; }
int ar_rf_probe(void) { radio.chip_profile = bad_chip ? 2 : 1; return step(1); }
const struct ar_rf_info *ar_rf_information(void) { return &radio; }
void ar_rf_set_io_domains(unsigned mask) { (void)mask; }
int ar_rf_setup_pins(const uint8_t *data, size_t size)
{
	assert(data == pins && size == expected_pins); return step(2);
}
int ar_rf_load_defaults(const uint8_t *data, size_t size)
{
	if (calls == 2) { assert(data == defaults && size == 4096); return step(3); }
	assert(data == defaults + 4096 && size == 1536); return step(4);
}
int ar_rf_calibrate(void) { return step(5); }
int ar_rf_setup_psram(void) { return step(6); }
int ar_rf_prepare_host(uint32_t frequency, uint32_t ms, uint8_t identity, const uint8_t *data, size_t size)
{
	assert(frequency == 5770000 && ms == 60000 && identity == 0xcb);
	assert(data == plan + 16 && size == sizeof plan - 16); return step(7);
}
int ar_rf_prepare_host_continuous(uint32_t frequency, uint8_t identity, const uint8_t *data, size_t size)
{
	assert(frequency == 5770000 && identity == 0xcb);
	assert(data == plan + 16 && size == sizeof plan - 16); return step(7);
}
int ar_rf_board_identity(uint8_t mac[4])
{
	assert(loader_scenario >= 15 && calls == 1);
	memcpy(mac, (uint8_t[]){1,2,3,5}, 4);
	return loader_scenario == 16 ? -2 : loader_scenario == 17 ? -110 : 0;
}
int ar_factory_device_read(const struct ar_flash_part *parts, size_t count,
	uint8_t record[8], struct ar_factory_info *info)
{
	(void)parts; (void)count; assert(calls==1 && !factory_calls++);
	*info=(struct ar_factory_info){.stage=AR_FACTORY_PARTITION,.error=-2};
	info->identity.capacity=0x400000; info->identity.source=AR_FLASH_ID_TABLE;
	memcpy(info->identity.id,(uint8_t[]){0xef,0x12,0x34,0,0,0},6);
	memcpy(info->observed_id,info->identity.id,6); info->id_valid=1;
	if (loader_scenario==27 || loader_scenario==28) {
		info->stage=loader_scenario==27?AR_FACTORY_IDENTIFY:AR_FACTORY_CONTROLLER;
		return info->error=loader_scenario==27?-74:-110;
	}
	if (loader_scenario==29) { return info->error=-110; }
	if (loader_scenario<23 || loader_scenario==31) { return -2; }
	info->stage=AR_FACTORY_READY; info->error=0; info->partition.partition_offset=0xa000;
	memcpy(record,(uint8_t[]){4,0,0,0,0,0,0,0},8);
	if (loader_scenario==24) { record[0]=3; }
	if (loader_scenario==25 || loader_scenario==33) { record[0]=2; }
	if (loader_scenario==26 || loader_scenario==30) { record[4]=1; record[6]=255; }
	return 0;
}
int ar_rf_prepare_config(uint32_t f, uint8_t id, const uint8_t *p, size_t n, unsigned search, const struct ar_power_config *power)
{
	assert(f == 4915000 && n == 280 && search == 1);
	assert(id == (loader_scenario == 16 ? (0x12^0x34^0x56^0x78) : 5));
	assert(!memcmp(p, "DEV4", 4) && p[28] == 25 && p[29] == 7);
	assert(factory_calls==1 && power && power->closed[0] && power->closed[1] && power->high_clock && power->dev_role);
	struct ar_power_plan initial;
	assert(!ar_power_plan(power,0,p[28],&initial));
	for (unsigned i = 0; i < 4; i++) { assert(initial.writes[i].value == p[24+i]); }
	const uint8_t *expected = (loader_scenario==30 || loader_scenario==31 || loader_scenario==33) ?
		(uint8_t[]){33,34,19,20} : (uint8_t[]){17,74,18,75};
	assert(!memcmp(p+24,expected,4));
	return step(7);
}
static void run(unsigned scenario)
{
	struct ar_boot_profile profile = {.defaults=defaults, .pins=pins, .plan=plan, .defaults_length=sizeof defaults, .pins_length=40, .plan_length=sizeof plan};
	for (unsigned i = 0; i < 10; i++) { pins[i*4] = i / 5; pins[i*4+1] = i % 5; }
	const uint8_t prefix[16] = {0x10,0x0b,0x58,0, 0x60,0xea,0,0, 0xcb,0,0,0, 76,0,0,0};
	memcpy(plan, prefix, 16); memcpy(plan + 16, "DEV4", 4);
	fail_at = scenario <= 7 ? scenario : 0;
	bad_chip = scenario == 8;
	if (scenario == 9) { pins[36] = 4; } /* Banks 0-3 exist. */
	/* USB board: stock's full boot pinmux, banks 0-3 with 5, 5, 6
	 * and 13 pins, any count up to 29 entries; banks 2-3 carry a pad byte. */
	if (scenario >= 37) {
		static const uint8_t counts[4] = {5, 5, 6, 13};
		unsigned n = 0;
		for (unsigned bank = 0; bank < 4; bank++)
			for (unsigned pin = 0; pin < counts[bank]; pin++, n++) {
				pins[n*4] = bank; pins[n*4+1] = pin; pins[n*4+2] = (bank + pin) & 15;
				pins[n*4+3] = bank >= 2 ? 0x10 + pin : 0;
			}
		profile.pins_length = n * 4;   /* 29 entries */
		if (scenario == 38) { profile.pins_length = 0; }
		if (scenario == 39) { profile.pins_length = 30; }                 /* not whole entries */
		if (scenario == 40) { pins[6*4+1] = 0; }                          /* bank 1 pin 0 twice */
		if (scenario == 41) { pins[10*4+1] = 6; }                         /* bank 2 has pins 0-5 */
		if (scenario == 42) { pins[0*4+3] = 1; }                          /* no pad byte on bank 0 */
		if (scenario == 43) { profile.pins_length = 30 * 4; pins[29*4] = 3; pins[29*4+1] = 12; }  /* 30 entries */
		if (scenario == 44) { pins[0*4+2] = 16; }                          /* function above 15 */
	}
	expected_pins = profile.pins_length;
	if (scenario == 10) { defaults[5630] = 1; defaults[5626] = 1; }
	if (scenario == 11) { plan[12] = 75; }
	if (scenario == 12) { profile.plan_length--; }
	assert(ar_boot_information()->stage == AR_BOOT_IDLE);
	if (scenario == 14) { memset(plan + 4, 0, 4); }
	loader_scenario = scenario;
	for (unsigned i=0;i<25;i++) { keys[i*4]=i%3; keys[i*4+1]=i/3%3; keys[i*4+2]=i/9; }
	profile.fch_keys=keys; profile.keys_length=sizeof keys;
	char overlay[16384]; memset(overlay, scenario >= 35 ? 0 : 'x', sizeof overlay);
	size_t bom_bytes = scenario == 21 ? 3 : 0;
	if (bom_bytes) { memcpy(overlay,"\xef\xbb\xbf",3); }
	memcpy(overlay + bom_bytes,config_fixture,sizeof config_fixture-1);
	size_t json_bytes=sizeof config_fixture-1+bom_bytes;
	if (scenario>=30 && scenario<=33) {
		const char *marker=strstr(config_fixture,"\"25dbm\":"); size_t prefix=marker-config_fixture;
		const char rows[]="\"24dbm\":[\"11\",\"12\",\"13\",\"14\"],\"26dbm\":[\"21\",\"22\",\"23\",\"24\"],";
		memmove(overlay+prefix+sizeof rows-1,overlay+prefix,json_bytes-prefix);
		memcpy(overlay+prefix,rows,sizeof rows-1); json_bytes+=sizeof rows-1;
		if (scenario>=31) {
			marker=strstr(config_fixture,"\"manu_init\":25"); prefix=marker-config_fixture;
			const char fallback[]="\"pwr_offset\":[1,0,-1,0],";
			memmove(overlay+prefix+sizeof fallback-1,overlay+prefix,json_bytes-prefix);
			memcpy(overlay+prefix,fallback,sizeof fallback-1); json_bytes+=sizeof fallback-1;
		}
	}
	if (scenario==34) { profile.flash_part_count=1; }
	if (scenario == 22) { overlay[20] = 0; }
	if (scenario == 36) { memset(overlay, 0, sizeof overlay); }
	int result = scenario >= 15 ? ar_boot_prepare_loader(&profile,
		scenario >= 20 ? overlay : scenario == 18 ? "{}" : config_fixture,
		scenario >= 20 ? sizeof overlay : scenario == 19 ? sizeof config_fixture - 2 : sizeof config_fixture) :
		ar_boot_prepare(scenario == 13 ? NULL : &profile);
	const struct ar_boot_info *info = ar_boot_information();
	if (!scenario || scenario == 14 || scenario == 15 || scenario == 16 || scenario == 20 || scenario == 21 ||
	    scenario==23 || scenario==24 || scenario==25 || scenario==35 || scenario==37 || (scenario>=29 && scenario<=33)) {
		assert(!result && calls == 7 && info->stage == AR_BOOT_READY && info->default_bytes == 5632);
	}
	else if (scenario <= 7) {
		static const unsigned stages[] = {0,AR_BOOT_PROBE,AR_BOOT_PINS,AR_BOOT_DEFAULTS,AR_BOOT_DEFAULTS,AR_BOOT_CALIBRATE,AR_BOOT_PSRAM,AR_BOOT_PREPARE};
		assert(result == -110 && calls == scenario && info->stage == stages[scenario]);
		assert(info->default_bytes == (scenario > 4 ? 5632u : scenario == 4 ? 4096u : 0u));
	} else if (scenario == 17) { assert(result == -110 && calls == 1 && info->stage == AR_BOOT_PROBE); }
	else if (scenario == 8) { assert(result == -95 && calls == 1 && info->stage == AR_BOOT_PROBE); }
	else if (scenario>=26 && scenario<=28) {
		assert(result==(scenario==26?-95:scenario==27?-74:-110) && calls==1 && info->stage==AR_BOOT_FACTORY);
		assert(!info->power_source);
	}
	else { assert(result == -22 && !calls && info->stage == AR_BOOT_VALIDATE); }
	assert(info->error == result);
	if (scenario == 14) { assert(!info->rf_bound_ms); }
	if (scenario == 15 || scenario == 16 || scenario == 20 || scenario == 21) {
		assert(!info->rf_bound_ms && info->config_bytes == sizeof config_fixture - 1 + bom_bytes && info->config_crc32c);
		assert(info->identity_source == (scenario == 16 ? 1u : 2u));
	}
	if (scenario>=15 && !result) {
		assert(info->version==2 && factory_calls==1 && info->flash_capacity==0x400000 && info->flash_id[0]==0x3412ef);
		unsigned source= scenario==23 || scenario==30 || scenario==32 || scenario==35 || scenario==37 ? AR_POWER_OFFSET_FACTORY_V4 :
			scenario==24 ? AR_POWER_OFFSET_FACTORY_V3 : scenario==31 || scenario==33 ? AR_POWER_OFFSET_JSON : AR_POWER_OFFSET_DEFAULT;
		assert(info->power_source==source && info->config_bytes==json_bytes);
		assert(info->factory_error==(scenario==29?-110:scenario<23 || scenario==31?-2:0));
		if (scenario==30 || scenario==31 || scenario==33) { assert(info->power_offsets[0]==1 && info->power_offsets[1]==-1); }
	}
	unsigned before = calls;
	assert(ar_boot_prepare(&profile) == -114 && calls == before); /* No implicit retry/reset. */
}
int main(void)
{
	/* Separate boots have separate firmware storage. Child processes isolate that
	 * storage while testing each failure boundary without a production reset hook. */
	for (unsigned i = 0; i < 45; i++) {
		pid_t pid = fork(); assert(pid >= 0);
		if (!pid) { run(i); _exit(0); }
		int status; assert(waitpid(pid, &status, 0) == pid);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
	puts("autonomous boot sequencing, failure containment and one-shot ownership passed");
	return 0;
}
