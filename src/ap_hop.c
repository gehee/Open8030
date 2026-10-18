/* SPDX-License-Identifier: MIT */
#include "ar_ap_hop.h"

void ar_hop_measure_reset(struct ar_hop_measure *measure)
{
	if (measure) { *measure = (struct ar_hop_measure){.newest = 255}; }
}

unsigned ar_hop_measure_feed(struct ar_hop_measure *measure, uint8_t first, uint8_t second)
{
	if (!measure || first > 15 || (second > 15 && second != 255)) { return 0; }
	unsigned again = 0;
	if (measure->newest != 255) {
		unsigned d = (unsigned)(first - measure->newest) & 15u;
		again = d != 1 && d != 2;
		if (second != 255) { d = (unsigned)(second - measure->newest) & 15u; again |= d != 1 && d != 2; }
	}
	/* The newest ID seen: a resent block is older and does not move it. */
	if (measure->newest == 255 || ((unsigned)(first - measure->newest) & 15u) < 8) { measure->newest = first; }
	if (second != 255 && ((unsigned)(second - measure->newest) & 15u) < 8) { measure->newest = second; }
	measure->bits = (uint16_t)(measure->bits << 1 | again);
	measure->run = again ? (uint8_t)(measure->run < 255 ? measure->run + 1 : 255) : 0;
	return again;
}

unsigned ar_hop_measure_value(const struct ar_hop_measure *measure)
{
	if (!measure) { return 0; }
	unsigned count = 0;
	for (uint16_t bits = measure->bits; bits; bits &= (uint16_t)(bits - 1)) { count++; }
	return measure->run > 1 && measure->run - 1u > count ? measure->run - 1u : count;
}

enum ar_hop_decision ar_hop_decide(const struct ar_hop_config *config, unsigned measure,
	int own_known, int32_t own_level, int best_known, int32_t best_level,
	uint32_t now_ms, uint32_t *last_blind_ms)
{
	if (!config || !last_blind_ms || !measure) { return AR_HOP_STAY; }
	if (measure < config->blind_errors || (*last_blind_ms && now_ms - *last_blind_ms <= config->blind_ms)) {
		if (!best_known || !own_known) { return AR_HOP_STAY; }
		int32_t quieter = own_level - best_level;
		unsigned tiers = config->tier_count < AR_HOP_TIERS ? config->tier_count : AR_HOP_TIERS;
		for (unsigned i = 0; i < tiers; i++) {
			if (measure <= config->tiers[i][0]) { return quieter < config->tiers[i][1] ? AR_HOP_STAY : AR_HOP_BY_LEVEL; }
		}
		return AR_HOP_BY_LEVEL;
	}
	*last_blind_ms = now_ms ? now_ms : 1;
	return AR_HOP_BLIND;
}
