/* SPDX-License-Identifier: MIT */
#ifndef AR_AP_HOP_H
#define AR_AP_HOP_H
#include <stdint.h>

/* An AP's own channel choice (stock bb_phy_chan_policy_pre_proc), the pure
 * part: the retransmission measure and the hop decision. The configuration
 * is stock's bb_load_channel: channel.hop_para tiers {retx_cnt, power_diff
 * dB}, hop_ex.blind_hop {at_err_cnt, duration ms}, init_chan -1 (the start
 * channel picked by measurement) and channel.mode "auto" (hopping on from
 * the start; a host's SET 5 turns it on and off later). */
#define AR_HOP_TIERS 5
struct ar_hop_config {
	uint16_t blind_ms;
	uint8_t blind_errors, tier_count;
	uint8_t tiers[AR_HOP_TIERS][2];
	uint8_t measure_start, auto_start;
};
/* Each frame's broadcast HARQ request: whether a block went again. The
 * measure is the longer of the run of such frames (less one) and their
 * count in the last 16. */
struct ar_hop_measure { uint16_t bits; uint8_t run, newest; };
void ar_hop_measure_reset(struct ar_hop_measure *measure);
/* The frame's request IDs (0..15; second 255 when there is none). Returns
 * 1 when one of them is not one of the next two new blocks. */
unsigned ar_hop_measure_feed(struct ar_hop_measure *measure, uint8_t first, uint8_t second);
unsigned ar_hop_measure_value(const struct ar_hop_measure *measure);
enum ar_hop_decision { AR_HOP_STAY = 0, AR_HOP_BY_LEVEL = 1, AR_HOP_BLIND = 2 };
/* Levels are the scan's (dBm, lower is quieter): ours, and the quietest
 * other work channel's. Below blind_errors, or within blind_ms of the last
 * blind hop: the first tier whose count is not below the measure needs the
 * best channel that many dB quieter; past every tier, any best channel. At
 * blind_errors and more otherwise: a blind hop (to the best channel, else
 * the caller's next one), and *last_blind_ms takes now_ms (0: never). */
enum ar_hop_decision ar_hop_decide(const struct ar_hop_config *config, unsigned measure,
	int own_known, int32_t own_level, int best_known, int32_t best_level,
	uint32_t now_ms, uint32_t *last_blind_ms);
#endif
