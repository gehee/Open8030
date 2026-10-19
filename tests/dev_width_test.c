/* SPDX-License-Identifier: MIT */
#include "ar_dev_width.h"
#include <assert.h>
#include <stdio.h>

/* Runs the policy every 10 ms from *t for ms, returns the last answer. */
static unsigned run(const struct ar_width_config *c, struct ar_width_state *s, unsigned current,
	unsigned mcs, uint32_t snr, uint32_t *t, uint32_t ms)
{
	unsigned want = current;
	for (uint32_t end = *t + ms; *t < end; *t += 10) {
		want = ar_width_decide(c, s, current, mcs, snr, *t);
		if (want != current) { *t += 10; break; }
	}
	return want;
}

int main(void)
{
	/* 5..40 MHz; the goggle's table: MCS 5 holds down to 241, MCS 6 is
	 * entered from 598; no widening at MCS 3 (internal 5) or below. */
	struct ar_width_config c = {.min = 2, .max = 5, .narrow_mcs = 5, .narrow_snr = 241, .widen_snr = 598,
		.narrow_ms = 200, .widen_ms = 2000, .settle_ms = 500};
	struct ar_width_state s; ar_width_reset(&s);
	uint32_t t = 1000;
	/* Link-up at 5 MHz with a strong SNR: one jump to 40 MHz (8000 / 8 =
	 * 1000 still enters MCS 6), after the settle and 2 s. */
	uint32_t from = t;
	assert(run(&c, &s, 2, 12, 8000, &t, 5000) == 5 && t - from >= 2500 && t - from < 2600);
	/* A fade at 40 MHz (flight 2: SNR well under what MCS 5 needs): one step
	 * down after the settle and 200 ms. */
	from = t;
	assert(run(&c, &s, 5, 3, 120, &t, 2000) == 4 && t - from >= 700 && t - from < 800);
	{
		/* Strong again at once: back up after the settle and widen_ms, with
		 * nothing holding it (in flight the link changes by the second). */
		struct ar_width_state h = s; uint32_t ht = t;
		assert(run(&c, &h, 4, 10, 5000, &ht, 3000) == 5 && ht - t < 2600);
	}
	/* At 20 MHz the same fade reads twice the SNR, above 241: no cascade,
	 * whatever the MCS still says. */
	assert(run(&c, &s, 4, 3, 240 * 2, &t, 5000) == 4);
	/* Deeper: under 241 at 20 MHz too, one more step. */
	assert(run(&c, &s, 4, 3, 200, &t, 2000) == 3);
	/* Back up: at 10 MHz the SNR carries two steps (5000 / 4 = 1250 >= 598). */
	assert(run(&c, &s, 3, 10, 5000, &t, 5000) == 5);
	/* Between narrowing (under 241) and widening (598 halved per step): stay,
	 * however long - the band that keeps it from switching back and forth. */
	assert(run(&c, &s, 5, 8, 300, &t, 20000) == 5);
	/* Errors with a good SNR (MCS down at the narrow MCS): no widening. */
	ar_width_reset(&s); t = 0;
	assert(run(&c, &s, 4, 5, 9000, &t, 10000) == 4);
	/* In between (241..1196 at 20 MHz): stay. */
	assert(run(&c, &s, 4, 8, 900, &t, 10000) == 4);
	/* A dip shorter than 200 ms, or one inside the settle, does nothing. */
	ar_width_reset(&s); t = 0;
	assert(run(&c, &s, 5, 8, 100, &t, 400) == 5);
	assert(run(&c, &s, 5, 8, 900, &t, 1000) == 5 && run(&c, &s, 5, 8, 100, &t, 150) == 5);
	/* Never below the narrowest; a lowered cap or raised floor wins at once. */
	ar_width_reset(&s); t = 0;
	assert(run(&c, &s, 2, 2, 10, &t, 3000) == 2);
	struct ar_width_config capped = c; capped.max = 4;
	assert(ar_width_decide(&capped, &s, 5, 12, 9000, t) == 4);
	capped.min = 3;
	assert(ar_width_decide(&capped, &s, 2, 2, 10, t) == 3);
	assert(ar_width_decide(NULL, &s, 4, 12, 9000, 0) == 4 && ar_width_decide(&c, &s, 6, 12, 9000, 0) == 6);
	puts("DEV width policy: jump to 40 MHz, one step per fade (no cascade), no hold-off, interference guard, bounds passed");
	return 0;
}
