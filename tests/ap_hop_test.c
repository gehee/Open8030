/* SPDX-License-Identifier: MIT */
#include "ar_ap_hop.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
	struct ar_hop_measure m;
	ar_hop_measure_reset(&m);
	/* New blocks two a frame: nothing went again. */
	for (unsigned frame = 0; frame < 20; frame++) {
		uint8_t first = (uint8_t)((frame * 2) & 15), second = (uint8_t)((frame * 2 + 1) & 15);
		assert(!ar_hop_measure_feed(&m, first, second));
	}
	assert(ar_hop_measure_value(&m) == 0 && m.newest == (39 & 15));
	/* The same two again for five frames: a run of five, five in the window. */
	for (unsigned i = 0; i < 5; i++) { assert(ar_hop_measure_feed(&m, 6, 7)); }
	assert(m.run == 5 && ar_hop_measure_value(&m) == 5);
	/* One new frame ends the run; the window keeps the five. */
	assert(!ar_hop_measure_feed(&m, 8, 9) && m.run == 0 && ar_hop_measure_value(&m) == 5);
	/* A run longer than the window counts (less one). */
	for (unsigned i = 0; i < 30; i++) { ar_hop_measure_feed(&m, 8, 9); }
	assert(m.run == 30 && ar_hop_measure_value(&m) == 29);
	/* One ID only (no second block), and invalid input. */
	ar_hop_measure_reset(&m);
	assert(!ar_hop_measure_feed(&m, 3, 255) && !ar_hop_measure_feed(&m, 4, 255) && ar_hop_measure_feed(&m, 4, 255));
	assert(!ar_hop_measure_feed(&m, 16, 255) && !ar_hop_measure_feed(NULL, 1, 2));

	/* A stock air configuration: tiers {3,20} {6,10} {9,5} {16,1}, blind 100 / 2000 ms. */
	struct ar_hop_config c = {.blind_ms = 2000, .blind_errors = 100, .tier_count = 4,
		.tiers = {{3, 20}, {6, 10}, {9, 5}, {16, 1}}};
	uint32_t blind = 0;
	assert(ar_hop_decide(&c, 0, 1, -70, 1, -105, 1000, &blind) == AR_HOP_STAY);          /* clean */
	assert(ar_hop_decide(&c, 1, 1, -86, 1, -105, 1000, &blind) == AR_HOP_STAY);          /* 19 dB < 20 */
	assert(ar_hop_decide(&c, 3, 1, -85, 1, -105, 1000, &blind) == AR_HOP_BY_LEVEL);      /* 20 dB */
	assert(ar_hop_decide(&c, 4, 1, -94, 1, -105, 1000, &blind) == AR_HOP_BY_LEVEL);      /* tier 6: 10 dB */
	assert(ar_hop_decide(&c, 4, 1, -96, 1, -105, 1000, &blind) == AR_HOP_STAY);
	assert(ar_hop_decide(&c, 12, 1, -104, 1, -105, 1000, &blind) == AR_HOP_BY_LEVEL);    /* tier 16: 1 dB */
	assert(ar_hop_decide(&c, 12, 1, -105, 1, -104, 1000, &blind) == AR_HOP_STAY);        /* the best is louder */
	assert(ar_hop_decide(&c, 40, 1, -105, 1, -100, 1000, &blind) == AR_HOP_BY_LEVEL);    /* past every tier */
	assert(ar_hop_decide(&c, 40, 1, -105, 0, 0, 1000, &blind) == AR_HOP_STAY);           /* nothing to go to */
	assert(!blind);
	/* Blind: at blind_errors, then not again within blind_ms. */
	assert(ar_hop_decide(&c, 100, 0, 0, 0, 0, 5000, &blind) == AR_HOP_BLIND && blind == 5000);
	assert(ar_hop_decide(&c, 100, 1, -105, 1, -100, 6000, &blind) == AR_HOP_BY_LEVEL && blind == 5000);
	assert(ar_hop_decide(&c, 100, 1, -105, 0, 0, 7000, &blind) == AR_HOP_STAY);
	assert(ar_hop_decide(&c, 120, 0, 0, 0, 0, 7001, &blind) == AR_HOP_BLIND && blind == 7001);
	/* Stock's defaults: three tiers, blind at 16 every 3 s. */
	struct ar_hop_config d = {.blind_ms = 3000, .blind_errors = 16, .tier_count = 3, .tiers = {{3, 20}, {6, 10}, {9, 5}}};
	blind = 0;
	assert(ar_hop_decide(&d, 12, 1, -105, 1, -100, 1000, &blind) == AR_HOP_BY_LEVEL);    /* past every tier */
	assert(ar_hop_decide(&d, 16, 1, -105, 1, -100, 1000, &blind) == AR_HOP_BLIND);
	assert(ar_hop_decide(NULL, 5, 1, 0, 1, 0, 0, &blind) == AR_HOP_STAY);
	puts("AP hop measure (runs, window, resent IDs), tiers, past-tier and blind hops passed");
	return 0;
}
