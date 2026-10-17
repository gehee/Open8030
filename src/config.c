/* SPDX-License-Identifier: MIT */
#include "ar_config.h"
#include "ar_json.h"
#include "ar_dev_mcs.h"
#include <string.h>

int ar_config_resolve_power(struct ar_config *config, enum ar_power_factory_state state,
	const uint8_t *record, size_t length)
{
	if (!config || config->plan_length < 48 || memcmp(config->plan + 16, "DEV4", 4)) { return -22; }
	struct ar_power_config power = {.closed={1,1}, .high_clock=1, .dev_role=1};
	int source = ar_power_calibration_resolve(&power, &config->calibration, state, record, length);
	if (source < 0) { return source; }
	struct ar_power_plan initial;
	int result = ar_power_plan(&power, 0, config->plan[44], &initial);
	if (result) { return result; }
	/* Existing DEV4 initial fields are the ordered user-0 closed-loop values. */
	for (unsigned i = 0; i < 4; i++) { config->plan[40 + i] = initial.writes[i].value; }
	config->power = power; config->power_source = source;
	return 0;
}

static void put16(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; i++) { p[i] = n >> (8 * i); } }
static int number(struct ar_json root, const char *path, int32_t low, int32_t high, int32_t *out)
{
	struct ar_json v; int error = ar_json_path(root, path, &v);
	if (error || ar_json_integer(v, out) || *out < low || *out > high) { return -22; } return 0;
}
static int optional_number(struct ar_json root, const char *path, int32_t fallback, int32_t low, int32_t high, int32_t *out)
{
	struct ar_json v; int error = ar_json_path(root, path, &v);
	if (error == -2) { *out = fallback; return 0; }
	return number(root, path, low, high, out);
}
static int boolean(struct ar_json root, const char *path, unsigned *out)
{
	struct ar_json v; return ar_json_path(root, path, &v) || ar_json_boolean(v, out) ? -22 : 0;
}
static int equal(struct ar_json root, const char *path, const char *text)
{
	struct ar_json v; if (ar_json_path(root, path, &v)) { return -22; }
	return ar_json_equal(v, text) ? 0 : -95;
}
static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int mac(struct ar_json root, const char *path, uint8_t out[4])
{
	struct ar_json v; char text[9];
	if (ar_json_path(root, path, &v) || ar_json_string(v, text, sizeof text) || strlen(text) != 8) { return -22; }
	for (unsigned i = 0; i < 4; i++) {
		int a = hex_digit(text[i * 2]), b = hex_digit(text[i * 2 + 1]);
		if (a < 0 || b < 0) { return -22; } out[i] = a * 16 + b;
	}
	return 0;
}
void ar_config_identity(struct ar_config *c, const uint8_t identity[4])
{
	memmove(c->plan + 20, identity, 4);
	c->plan[8] = identity[0] ^ identity[1] ^ identity[2] ^ identity[3];
}
int ar_config_parse(struct ar_config *out, const char *json, size_t length,
	const uint8_t *keys, size_t keys_length)
{
	if (!out || !keys || keys_length != 100) { return -22; }
	for (unsigned i = 0; i < 25; i++) {
		uint32_t key = 0;
		for (unsigned b = 0; b < 4; b++) { key |= (uint32_t)keys[i * 4 + b] << (b * 8); }
		if (key & ~UINT32_C(0x01070303)) { return -22; }
		for (unsigned j = 0; j < i; j++) { if (!memcmp(keys + i * 4, keys + j * 4, 4)) { return -22; } }
	}
	struct ar_json root, basic, dev, channel, policy, array, row, value;
	struct ar_config c = {0}; uint8_t *plan = c.plan, *p = plan + 16;
	int32_t n; unsigned flag;
	if (ar_json_parse(&root, json, length) ||
	    ar_json_path(root, "baseband.basic", &basic) ||
	    ar_json_path(basic, "dev", &dev) || ar_json_path(basic, "channel", &channel) ||
	    ar_json_path(dev, "mcs.slot", &policy)) { return -22; }
	/* These settings select hardware paths implemented and validated on Ascent VRX.
	 * Reject incompatible requests rather than silently retaining compiled data. */
	const struct { const char *path, *text; } required[] = {
		{"role","dev"}, {"dev.clock","200MHz"},
		{"channel.band_mode","band_single"}, {"channel.init_band","5G"},
		{"channel.mode","manu"}, {"channel.band_hop","manu"}, {"channel.fs_bw","5"},
		{"power.mode","closeloop"}, {"power.pwr_auto","manu"}, {"power.link_auto","manu"},
		{"lna.inner.mode","manu"}, {"lna.inner.manu","on"}, {"lna.fem.mode","manu"}, {"lna.fem.manu","on"},
		{"para.br.tx.bandwidth","5"}, {"para.br.rx.bandwidth","5"},
		{"para.slot.tx.bandwidth","2.5"}, {"para.slot.rx.bandwidth","2.5"}
	};
	for (unsigned i = 0; i < sizeof required / sizeof required[0]; i++) {
		int error = equal(basic, required[i].path, required[i].text); if (error) { return error; }
	}
	const char *profiles[] = {"para.br.tx", "para.br.rx", "para.slot.tx", "para.slot.rx"};
	for (unsigned i = 0; i < 4; i++) {
		if (ar_json_path(basic, profiles[i], &row)) { return -22; }
		const char *enabled[] = {"payload", "enable_tintlv"};
		for (unsigned j = 0; j < 2; j++) {
			if (boolean(row, enabled[j], &flag)) { return -22; } if (!flag) { return -95; }
		}
		const char *fields[] = {"tintlv_num", "tintlv_len", "fch_info_len"};
		const int32_t expected[] = {i < 2 ? 2 : 1, 12, 96};
		for (unsigned j = 0; j < 3; j++) {
			if (number(row, fields[j], 0, INT32_MAX, &n)) { return -22; } if (n != expected[j]) { return -95; }
		}
		int error = equal(row, i & 1 ? "rx_mode" : "tx_mode", i & 1 ? "2T2R_STBC" : i == 0 ? "2TX" : "2TX_STBC");
		if (error) { return error; }
	}
	if (boolean(policy, "bw_auto.enable", &flag)) { return -22; } if (flag) { return -95; }
	if (boolean(basic, "channel.multi_mode.enable", &flag)) { return -22; } if (flag) { return -95; }
	if (boolean(basic, "channel.rc_hop.enable", &flag)) { return -22; } if (flag) { return -95; }
	memcpy(p, "DEV4", 4); put32(plan + 12, 76); /* Firmware-only continuous preparation. */
	if (mac(dev, "mac", p + 4) || mac(dev, "ap_mac", p + 8)) { return -22; }
	ar_config_identity(&c, p + 4);
	/* Candidates belong to the original backend; no factory peer is compiled. */
	p[22] = p[23] = 2;
	if (ar_json_path(channel, "freq", &array)) { return -22; }
	unsigned count = 0;
	for (; count < 42; count++) {
		int error = ar_json_index(array, count, &value); if (error == -2) { break; }
		if (error || ar_json_integer(value, &n) || n <= 4300000 || n > 7500000) { return -22; }
		put32(p + 268 + count * 4, (uint32_t)n);
		for (unsigned i = 0; i < count; i++) { if (!memcmp(p + 268 + i * 4, p + 268 + count * 4, 4)) { return -22; } }
	}
	if (!count || ar_json_index(array, count, &value) != -2 || number(channel, "init_chan", -1, (int32_t)count - 1, &n)) { return -22; }
	c.search = n < 0; p[21] = n < 0 ? 0 : n;
	memcpy(plan, p + 268 + p[21] * 4, 4); c.plan_length = 284 + count * 4;
	if (number(basic, "power.manu_init", 9, 36, &n)) { return -22; } p[28] = n;
	/* Retain every supplied calibration row for subsequent host power changes.
	 * The matched loader has no board selector yet; default is explicit. The
	 * initial DEV4 row retains its accepted behavior until factory resolution
	 * and the dynamic power application path are integrated. */
	if (ar_power_calibration_load(&c.calibration, root, NULL) ||
	    !(c.calibration.valid & (UINT32_C(1) << (n - 9)))) { return -22; }
	memcpy(p + 24, c.calibration.rows[n - 9], 4);
	if (number(basic, "para.slot.tx.retx_count", 0, 127, &n)) { return -22; } p[29] = n;
	uint8_t *m = p + 32;
	if (boolean(policy, "enable", &flag)) { return -22; } m[0] = flag;
	if (!equal(policy, "mode", "auto")) { m[0] |= 2; } else if (equal(policy, "mode", "manu")) { return -95; }
	if (number(policy, "init", -2, 23, &n)) { return -22; } m[1] = n + 2;
	if (number(policy, "hold_time", 0, 65535, &n)) { return -22; } put16(m + 4, n);
	if (optional_number(policy, "delay_start", 0, 0, 65535, &n)) { return -22; } put16(m + 6, n);
	if (number(policy, "max_wait_time", 0, 65535, &n)) { return -22; } put16(m + 8, n);
	if (boolean(basic, "dist_calc.enable", &flag)) { return -22; } m[10] = flag; m[11] = 2;
	/* Ranging offset in raw units (stock 0..100; distance = (raw - offset) * 1.5 m). */
	if (optional_number(basic, "dist_calc.offset", 20, 0, 100, &n)) { return -22; } m[12] = n;
	if (ar_json_path(policy, "mcs_tab", &array)) { return -22; }
	for (unsigned i = 0; i < 10; i++) {
		int error = ar_json_index(array, i, &row); if (error == -2) { break; } if (error) { return -22; }
		uint8_t *r = m + 16 + i * 12;
		/* Internal index = mcs + 2; the FCH key table covers 25 entries, so
		 * rows up to MCS 13 (index 15, 1024-QAM 3/4) are valid. */
		if (number(row, "mcs", -2, 13, &n)) { return -95; } r[0] = n + 2;
		if (i && r[0] <= r[-12]) { return -22; }
		if (number(row, "ldpc_up_num", 0, 255, &n)) { return -22; } r[1] = n;
		if (number(row, "ldpc_dw_num", 0, 255, &n)) { return -22; } r[2] = n;
		if (number(row, "ldpc_dw_conti_num", -128, 127, &n)) { return -22; } r[3] = n;
		const char *fields[] = {"snr_up","snr_dw","up_keep_time","dw_keep_time"};
		for (unsigned j = 0; j < 4; j++) { if (number(row, fields[j], 0, 65535, &n)) { return -22; } put16(r + 4 + j * 2, n); }
		m[2]++;
	}
	if (!m[2] || ar_json_index(array, m[2], &value) != -2) { return -22; }
	struct ar_dev_mcs_config checked;
	if (ar_dev_mcs_decode_config(&checked, m, AR_DEV_MCS_CONFIG_BYTES)) { return -22; }
	memcpy(p + 168, keys, 100);
	*out = c; return 0;
}
