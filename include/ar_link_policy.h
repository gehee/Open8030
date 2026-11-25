/* SPDX-License-Identifier: MIT */
#ifndef AR_LINK_POLICY_H
#define AR_LINK_POLICY_H
#include <stdint.h>
/* The ground's rate and width rules that are Open8030's own (stock has
 * neither), from the configuration's dev.mcs.slot.open8030 block. A key left
 * out keeps the value below, so a configuration without the block runs as
 * before it existed. SNR values are the radio's own scale, as the mcs_tab
 * rows (36 x 10^(dB/10)); MCS values are the configuration's (stock) ones. */
struct ar_link_policy {
	/* error_rule: a row that loses more than `errors` of `frames` frames is
	 * left at once and barred for hold_ms, doubled each time it fails again
	 * within a minute of coming back, to hold_max_ms. */
	uint8_t error_rule;
	uint16_t error_frames, error_count;
	uint32_t error_hold_ms, error_hold_max_ms;
	/* wide: the top MCS at 40 MHz (internal, stock + 2), MCS 8's down/up at
	 * 40 MHz, MCS 10's down/up at 20 MHz when 40 is allowed, and the 40 MHz
	 * width switch: narrow under narrow_snr (narrow_snr_hold within hold_ms
	 * of widening), widen from widen_snr. */
	uint8_t wide_max_mcs;
	uint16_t mcs8_snr_40[2], mcs10_snr_20[2];
	uint16_t narrow_snr_40, widen_snr_40, narrow_snr_40_hold;
	uint32_t hold_40_ms;
};
/* open8030.profiles: up to AR_LINK_PROFILES named sets of the keys above,
 * each starting from the block's own values; open8030.profile the one in use
 * at start. The host picks another at any time (AR_REQ_RF_LINK_PROFILE), with
 * no restart. Without profiles: one, the block itself. */
#define AR_LINK_PROFILES 4
struct ar_link_profiles {
	uint8_t count, current;
	struct ar_link_policy profile[AR_LINK_PROFILES];
};
#define AR_LINK_POLICY_DEFAULTS { .error_rule = 1, .error_frames = 256, .error_count = 2, \
	.error_hold_ms = 10000u, .error_hold_max_ms = 80000u, \
	.wide_max_mcs = 8 + 2, .mcs8_snr_40 = {1805, 2550}, .mcs10_snr_20 = {1805, 2550}, \
	.narrow_snr_40 = 2859, .widen_snr_40 = 4532, .narrow_snr_40_hold = 1433, .hold_40_ms = 4000 }
#endif
