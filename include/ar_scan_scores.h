/* SPDX-License-Identifier: MIT */
#ifndef AR_SCAN_SCORES_H
#define AR_SCAN_SCORES_H
#include <stddef.h>
#include <stdint.h>

/* Owned native state, not an original firmware layout. Scores are the original
 * integer conversion, with no independently established physical units. */
struct ar_scan_history {
	uint32_t raw[10], means[10], latest;
	int32_t score;
};
struct ar_scan_scores {
	uint8_t ring, ready, startup, band_mask, group_count, initial_band;
	int8_t bias;
	uint16_t selected, best_band[3], best_group[6];
	int32_t band_score[3];
};
enum ar_scan_score_action {
	AR_SCAN_SCORE_ROUND = 1,
	AR_SCAN_SCORE_READY = 2,
	AR_SCAN_SCORE_SELECTED = 4,
	AR_SCAN_SCORE_NO_SELECTION = 8,
};
int32_t ar_scan_power_score(uint32_t power);
/* DEV/base-channel processing. Rows retain frequency/enable/band fields as in
 * ar_scan; groups are low-nibble values. Zero group_count permits measurement
 * collection before group policy is configured. A completed startup selection
 * returns SELECTED: caller must hold RX and post the original selection event.
 * AP/subchannel processing, message posting and physical sample attribution are
 * separate responsibilities. Native structural errors return -22 unchanged. */
int ar_scan_score_feed(struct ar_scan_scores *state, struct ar_scan_history *history,
	const uint32_t *rows, const uint8_t *groups, size_t count,
	unsigned channel, uint32_t power);
#endif
