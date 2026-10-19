/* SPDX-License-Identifier: MIT */
#ifndef AR_DEV_WIDTH_H
#define AR_DEV_WIDTH_H
#include <stdint.h>

/* The receiver's broadcast width policy (DEV), on the SNR its MCS policy
 * averages. Widths are bandwidth codes: 2 = 5 MHz, 3 = 10, 4 = 20, 5 = 40.
 * Halving the width doubles the SNR (the same power in half the noise),
 * about one row of the MCS ladder; doubling it halves the SNR but, above the
 * lowest rows, carries far more. So the link runs as wide as it can:
 * - it narrows a step when the SNR has been under narrow_snr (the SNR the
 *   row above the narrow MCS needs to stay) for narrow_ms;
 * - it widens as many steps as the SNR, halved per step, keeps at or above
 *   widen_snr (the SNR the widen MCS row needs to be entered), for widen_ms,
 *   unless its MCS is down at the narrow MCS (errors with a good SNR);
 * - after any change it waits settle_ms (the SNR average catches up).
 * Nothing holds it back beyond that: in flight the link changes by the
 * second; the SNR band between narrowing and widening (a step's 3 dB and the
 * rows' gap) keeps it from switching back and forth. The SNR is checked, not
 * the MCS: the MCS climbs a row at a time
 *   after a narrowing, and read as weak it narrowed again and again. */
struct ar_width_config {
	uint8_t min, max;               /* narrowest and widest code */
	uint8_t narrow_mcs;             /* internal MCS: no widening at or below it */
	uint32_t narrow_snr, widen_snr;
	uint16_t narrow_ms, widen_ms, settle_ms;
};
struct ar_width_state {
	uint32_t weak_since, strong_since, changed_ms;
	uint8_t weak, strong, last;
};
void ar_width_reset(struct ar_width_state *state);
/* current: the width now; mcs: the MCS policy's internal MCS; snr: its
 * average. Returns the width to ask for (current: stay). */
unsigned ar_width_decide(const struct ar_width_config *config, struct ar_width_state *state,
	unsigned current, unsigned mcs, uint32_t snr, uint32_t now_ms);
#endif
