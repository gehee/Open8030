/* SPDX-License-Identifier: MIT */
#include "ar_dev_width.h"


void ar_width_reset(struct ar_width_state *state)
{
	if (state) { *state = (struct ar_width_state){0}; }
}

unsigned ar_width_decide(const struct ar_width_config *config, struct ar_width_state *state,
	unsigned current, unsigned mcs, uint32_t snr, uint32_t now_ms)
{
	if (!config || !state || current < 2 || current > 5) { return current; }
	unsigned min = config->min < 2 ? 2 : config->min, max = config->max > 5 ? 5 : config->max;
	if (max < min) { max = min; }
	if (current != state->last) {
		/* Moved (by us or the AP): the SNR average starts over. */
		state->last = (uint8_t)current; state->changed_ms = now_ms; state->weak = state->strong = 0;
	}
	if (current > max) { return max; }
	if (current < min) { return min; }
	if (now_ms - state->changed_ms < config->settle_ms) { return current; }
	const uint32_t narrow_snr = current == 5 && config->narrow_snr_40 ? config->narrow_snr_40 : config->narrow_snr;
	if (snr < narrow_snr) {
		state->strong = 0;
		if (current <= min) { state->weak = 0; return current; }
		if (!state->weak) { state->weak = 1; state->weak_since = now_ms; }
		if (now_ms - state->weak_since < config->narrow_ms) { return current; }
		state->weak = 0;
		return current - 1;
	}
	state->weak = 0;
	/* Steps whose halved SNR still enters the widen row. */
	unsigned steps = 0;
	uint64_t need = config->widen_snr;
	while (current + steps < max && need && (uint64_t)snr >= need * 2) {
		if (current + steps + 1 == 5 && config->widen_snr_40 &&
		    (uint64_t)snr < (uint64_t)config->widen_snr_40 << (4 - current)) { break; }
		steps++; need *= 2;
	}
	/* Under 20 MHz: to 20 once the MCS holds climb_mcs, whatever the SNR. */
	const unsigned climb_to = max < 4 ? max : 4;
	if (config->climb_mcs && mcs >= config->climb_mcs && current + steps < climb_to) { steps = climb_to - current; }
	if (!steps || mcs <= config->narrow_mcs) { state->strong = 0; return current; }
	if (!state->strong) { state->strong = 1; state->strong_since = now_ms; }
	if (now_ms - state->strong_since < config->widen_ms) { return current; }
	state->strong = 0;
	return current + steps;
}
