/* SPDX-License-Identifier: MIT */
#include "ar_pwr_auto.h"
#include <assert.h>
#include <stdio.h>

/* Feeds records every 5 ms from *now until a request comes (or `limit`). */
static unsigned run(struct ar_pwr_auto *c, uint32_t *now, unsigned limit, unsigned errored,
	uint16_t snr, uint8_t gain, uint8_t *target, uint8_t *direction)
{
	for (unsigned i = 0; i < limit; i++) {
		*now += 5;
		if (ar_pwr_auto_record(c, *now, errored, snr, gain, (uint8_t)(gain + 3), target, direction)) { return i + 1; }
	}
	return 0;
}

/* The raw SNR for dB x 10 (raw = 36 x 10^(dB/10)). */
static uint16_t raw_of(int db10)
{
	double v = 36.0; for (int i = 0; i < db10; i++) { v *= 1.023292992; }
	return (uint16_t)(v + 0.5);
}

int main(void)
{
	struct ar_pwr_auto c;
	uint8_t target = 0, direction = 0, slot = 0;
	uint32_t now = 1000;
	unsigned n;

	/* dB from the raw scale: 36 is 0 dB, x10 is +10 dB. */
	assert(ar_pwr_auto_db10(36) == 0 && ar_pwr_auto_db10(360) == 100 && ar_pwr_auto_db10(3600) == 200);
	for (int d = 0; d <= 300; d += 7) { int e = ar_pwr_auto_db10(raw_of(d)) - d; assert(e >= -1 && e <= 1); }
	assert(ar_pwr_auto_db10(0) < -900);

	ar_pwr_auto_init(&c);
	/* Off until a range is given: nothing however weak. */
	assert(!run(&c, &now, 200, 1, raw_of(50), 90, &target, &direction));
	assert(ar_pwr_auto_set_range(&c, 1, 40, 13) == -22 && ar_pwr_auto_set_range(&c, 1, 20, 25) == -22);
	assert(!ar_pwr_auto_set_range(&c, 1, 26, 13));
	ar_pwr_auto_reset(&c);

	/* The power model on the defaults (ceiling 24 dB to 22 dBm, 1 dB less
	 * per dB above), range 13..26. The bench sweep 15-30 m off: 19.3 dB at
	 * 26 dBm -> 22 is best (~20.8 predicted); far, 15 dB at 26 -> stay at
	 * 26; close, 23 dB at 22 -> stay; a fade to 14 dB at 22 -> 26. */
	ar_pwr_auto_reset(&c);
	const struct ar_pwr_auto_policy *pp = &c.policy;
	assert(ar_pwr_auto_best(pp, 193, 26, 13, 26) == 22);
	{ int d = ar_pwr_auto_predict_db10(pp, 193, 26, 22); assert(d >= 205 && d <= 210); }
	assert(ar_pwr_auto_predict_db10(pp, 193, 26, 26) >= 192 && ar_pwr_auto_predict_db10(pp, 193, 26, 26) <= 194);
	assert(ar_pwr_auto_best(pp, 150, 26, 13, 26) == 26);
	assert(ar_pwr_auto_best(pp, 230, 22, 13, 26) == 22);
	assert(ar_pwr_auto_best(pp, 140, 22, 13, 26) == 26);
	assert(ar_pwr_auto_best(pp, 140, 22, 13, 24) == 24);   /* the host's range holds */
	assert(ar_pwr_auto_best(pp, 300, 22, 13, 26) <= 22);   /* at the ceiling: never up */

	/* Records: 19.3 dB at 26 (gain 60, no overload): down after 2 s, 2 dB a
	 * request; at 24 the SNR the model expects (20.4) stays (22 would be
	 * ~0.6 dB better, under the 1 dB a move needs); 21.5 dB at 24 (close
	 * by, near the ceiling) -> 22, again after 2 s. */
	ar_pwr_auto_reset(&c); c.peer_dbm = 26;
	n = run(&c, &now, 1000, 0, raw_of(193), 60, &target, &direction);
	assert(n && n * 5 >= 2000 && target == 24 && direction == 2);
	ar_pwr_auto_sent(&c, target, direction);
	assert(!run(&c, &now, 1000, 0, raw_of(204), 60, &target, &direction));
	n = run(&c, &now, 1000, 0, raw_of(215), 60, &target, &direction);
	assert(n && n * 5 >= 2000 && target == 22 && direction == 2);
	ar_pwr_auto_sent(&c, target, direction);
	assert(!run(&c, &now, 1000, 0, raw_of(208), 60, &target, &direction));   /* holds at 22 */
	/* A fade at 22 (14 dB): up 2 dB within ~15 records (the fast SNR
	 * settles in a few, then 8 in a row), the next 2 dB ~17 records later. */
	n = run(&c, &now, 1000, 0, raw_of(140), 75, &target, &direction);
	assert(n && n <= 15 && target == 24 && direction == 1);
	ar_pwr_auto_sent(&c, target, direction);
	n = run(&c, &now, 1000, 0, raw_of(140), 75, &target, &direction);
	assert(n && n <= 17 && target == 26 && direction == 1);
	ar_pwr_auto_sent(&c, target, direction);
	/* Out of the fade (19.3 dB at 26, the model's pick 22): held 10 s after
	 * the last up, then the usual 2 s of the pick and down 2 dB. */
	{
		const uint32_t up = now;
		n = run(&c, &now, 4000, 0, raw_of(193), 60, &target, &direction);
		assert(n && now - up >= AR_PWR_AUTO_UP_HOLD_MS + AR_PWR_AUTO_DOWN_MS && now - up <= AR_PWR_AUTO_UP_HOLD_MS + AR_PWR_AUTO_DOWN_MS + 50);
		assert(target == 24 && direction == 2);
		ar_pwr_auto_sent(&c, target, direction);
	}
	/* A short fade inside the hold keeps it: up again, and no down for
	 * another 10 s. */
	n = run(&c, &now, 1000, 0, raw_of(140), 75, &target, &direction);
	assert(n && target == 26 && direction == 1);
	ar_pwr_auto_sent(&c, target, direction);
	assert(!run(&c, &now, 1900, 0, raw_of(193), 60, &target, &direction));   /* 9.5 s: still held */

	/* At the gain floor with the SNR short (gain 28, 17 dB): down after 300
	 * ms, whatever the model says. */
	ar_pwr_auto_reset(&c); c.peer_dbm = 26;
	n = run(&c, &now, 200, 0, raw_of(170), 28, &target, &direction);
	assert(n && n * 5 >= 300 && n * 5 <= 310 && target == 25 && direction == 2);

	/* Overloaded at the minimum: nothing (never up at the gain floor, nothing
	 * under the minimum). Off: nothing. */
	ar_pwr_auto_reset(&c); c.peer_dbm = 13;
	assert(!run(&c, &now, 1000, 0, raw_of(150), 28, &target, &direction));
	assert(!ar_pwr_auto_set_range(&c, 0, 0, 0) && !c.enabled);
	ar_pwr_auto_reset(&c);
	assert(!run(&c, &now, 1000, 0, raw_of(100), 80, &target, &direction));

	/* The word on the air and back. */
	uint32_t w = ar_pwr_auto_word(22, 2, 0);
	assert(w == (6u | 22u << 4 | 2u << 12));
	assert(!ar_pwr_auto_parse(w, &target, &direction, &slot) && target == 22 && direction == 2 && !slot);
	assert(ar_pwr_auto_parse(0x50, &target, &direction, &slot) == -22);         /* frame mode 5 */
	assert(ar_pwr_auto_parse(w | 1u << 24, &target, &direction, &slot) == -22);

	/* The announcement, and the peer's power it gives the requester. */
	uint8_t dbm = 0;
	assert(ar_pwr_auto_announce_word(17) == (13u | 17u << 4));
	assert(!ar_pwr_auto_parse_announce(ar_pwr_auto_announce_word(17), &dbm) && dbm == 17);
	assert(ar_pwr_auto_parse_announce(ar_pwr_auto_announce_word(5), &dbm) == -22);
	assert(ar_pwr_auto_parse_announce(w, &dbm) == -22 && ar_pwr_auto_parse_announce(0x1d | 1u << 12, &dbm) == -22);
	ar_pwr_auto_init(&c); assert(!ar_pwr_auto_set_range(&c, 1, 26, 13));
	ar_pwr_auto_peer(&c, 17);
	n = run(&c, &now, 1000, 0, raw_of(140), 75, &target, &direction);
	assert(n && target == 19 && direction == 1);   /* from 17 at 14 dB: 24 is the model's best, 2 dB a request */

	/* The receiving side: clamped to its host's range; up to a target not
	 * above the current power is current + 1; nothing without a range. */
	assert(ar_pwr_auto_apply(20, 18, 2, 13, 26) == 18);
	assert(ar_pwr_auto_apply(20, 10, 2, 13, 26) == 13);
	assert(ar_pwr_auto_apply(20, 30, 1, 13, 26) == 26);
	assert(ar_pwr_auto_apply(20, 19, 1, 13, 26) == 21);
	assert(ar_pwr_auto_apply(26, 26, 1, 13, 26) == 0);
	assert(ar_pwr_auto_apply(20, 18, 2, 20, 20) == 0);
	assert(ar_pwr_auto_apply(20, 18, 3, 13, 26) == 0);
	puts("Automatic power (transmitter ceiling model): dB scale, best power (a move for 1 dB or more), steps down, fade up 2 dB a step, hold after an up, gain floor, range, wire word and apply rules passed");
	return 0;
}
