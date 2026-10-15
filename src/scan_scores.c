/* SPDX-License-Identifier: MIT */
#include "ar_scan_scores.h"
#include <limits.h>

int32_t ar_scan_power_score(uint32_t power)
{
	/* Integer thresholds ceil(10^(k/10)), k=1..19, calculated independently.
	 * This implements the original decimal-mantissa conversion, including its
	 * 99 plateau and the change of offset above level30. */
	static const uint8_t thresholds[19] = {2,2,2,3,4,4,6,7,8,10,13,16,20,26,32,40,51,64,80};
	int32_t level = 0;
	while (power >= 100) { power /= 10; level += 10; }
	for (unsigned i = 0; i < 19; i++) { if (power + 1 >= thresholds[i]) { level++; } }
	return level - (level > 30 ? 154 : 156);
}
static int32_t signed_word(uint32_t value)
{
	return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX-value);
}
static unsigned eligible(uint32_t row, unsigned band)
{
	return (row & 0x01000000) && ((row >> 27) & 3) == band;
}
static int32_t average(const uint32_t *rows, const struct ar_scan_history *h,
	size_t count, unsigned band, int32_t empty)
{
	uint32_t sum = 0; unsigned n = 0;
	for (size_t i = 0; i < count; i++) {
		if (eligible(rows[i],band)) { sum += (uint32_t)h[i].score; n++; }
	}
	return n ? signed_word(sum) / (int32_t)n : empty;
}
int ar_scan_score_feed(struct ar_scan_scores *s, struct ar_scan_history *h,
	const uint32_t *rows, const uint8_t *groups, size_t count,
	unsigned channel, uint32_t power)
{
	if (!s || !h || !rows || !count || count > 255 || channel >= count ||
		s->ring >= 10 || s->ready > 1 || s->startup > 1 || s->initial_band > 3 ||
		s->group_count > 6 || (s->group_count && !groups)) { return -22; }
	struct ar_scan_history *current = &h[channel];
	current->latest = power; current->raw[s->ring] = power;
	uint64_t total = 0;
	for (unsigned i = 0; i < 10; i++) { total += current->raw[i]; }
	current->means[s->ring] = total / 10;
	total = 0;
	for (unsigned i = 0; i < 10; i++) { total += current->means[i]; }
	current->score = ar_scan_power_score(total / 10);
	for (size_t i = channel+1; i < count; i++) { if (rows[i] & 0x01000000) { return 0; } }
	if (++s->ring >= 10) { s->ring = 0; s->ready = 1; }
	int action = AR_SCAN_SCORE_ROUND;
	if (!s->ready) { return action; }
	action |= AR_SCAN_SCORE_READY;
	for (unsigned band = 0; band < 3; band++) {
		s->best_band[band] = 255; s->band_score[band] = 0;
		if (!(s->band_mask & (1u << band))) { continue; }
		for (size_t i = 0; i < count; i++) {
			if (eligible(rows[i],band) && (s->best_band[band] == 255 || h[i].score < h[s->best_band[band]].score)) {
				s->best_band[band] = i;
			}
		}
		s->band_score[band] = average(rows,h,count,band,0);
	}
	for (unsigned group = 0; group < s->group_count; group++) {
		s->best_group[group] = 255;
		for (size_t i = 0; i < count; i++) {
			if ((rows[i] & 0x01000000) && (group == 3 || (groups[i] & 15) == group) &&
				(s->best_group[group] == 255 || h[i].score < h[s->best_group[group]].score)) { s->best_group[group] = i; }
		}
	}
	if (!s->startup) { return action; }
	unsigned band = s->initial_band;
	if (band == 3) {
		int32_t values[3] = {0};
		for (unsigned b = 0; b < 3; b++) {
			if (s->band_mask & (1u << b)) { values[b] = average(rows,h,count,b,-1); }
		}
		for (unsigned b = 0; b < 3; b++) {
			if (values[b]) { values[b] = signed_word((uint32_t)values[b] - (uint32_t)(int32_t)s->bias); break; }
		}
		band = values[0] <= values[1] ? 0 : 1;
		if (values[2] < values[band]) { band = 2; }
	}
	/* The original dereferences the winner unchecked. Keep startup pending if
	 * configuration/eligibility leaves no valid winner, without posting one. */
	if (s->best_band[band] >= count) { return action | AR_SCAN_SCORE_NO_SELECTION; }
	s->selected = s->best_band[band]; s->startup = 0;
	return action | AR_SCAN_SCORE_SELECTED;
}
