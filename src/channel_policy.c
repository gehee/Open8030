/* SPDX-License-Identifier: MIT */
#include "ar_channel_policy.h"

static int phy_request(uint8_t channel, const struct ar_channel_policy_ops *ops,
	void *context)
{
	uint32_t value = (uint32_t)channel << 4 | 4;
	uint8_t payload[4] = {value, value >> 8, 0, 0};
	return ops->phy(8, 8, payload, sizeof(payload), context);
}

static void backup_request(struct ar_channel_policy *state,
	const uint32_t *rows, const uint8_t *groups, const int32_t *scores,
	size_t count, const struct ar_channel_policy_ops *ops, void *context)
{
	struct ar_channel_candidate candidates[4];
	/* Validated base-channel storage guarantees at least the fallback entry,
	 * so the original retry loop for an empty ranking cannot be reached. */
	int used = ar_channel_candidates(rows, groups, scores, count, state->group,
		state->channel, candidates);
	uint8_t payload[8];
	for (unsigned i = 0; i < 4; i++) {
		state->backups[i] = i < (unsigned)used ?
			(uint16_t)(candidates[i].channel | 0x100) : 255;
		payload[2*i] = state->backups[i];
		payload[2*i+1] = state->backups[i] >> 8;
	}
	for (unsigned slot = 0; slot < 8; slot++) {
		if (state->role ? slot == 0 : (state->slots & (1u << slot))) {
			(void)ops->control(slot, 10, payload, sizeof(payload), context);
		}
	}
	ops->timer(3000, context);
	state->auxiliary_flags |= 16;
}

static int auxiliary(struct ar_channel_policy *state,
	const uint32_t *rows, const uint8_t *groups, const int32_t *scores,
	size_t count, const uint8_t *mode, const uint32_t *channel,
	const struct ar_channel_policy_ops *ops, void *context)
{
	ops->lock(AR_CHANNEL_AUX_LOCK, context);
	int result = -2;
	if (state->auxiliary_flags & 4) {
		if ((state->auxiliary_flags & 2) && mode && !*mode) {
			uint8_t selected = channel ? (uint8_t)*channel : (uint8_t)state->auxiliary_channel;
			result = phy_request(selected | 0x80, ops, context);
			if (!result && !*mode) {
				state->mode = 0;
				state->auxiliary_flags |= 32;
				state->request.flags &= (uint8_t)~4u;
				state->channel = (uint8_t)state->auxiliary_channel;
				state->band = (rows[state->channel] >> 27) & 3;
				state->group = groups[state->channel] & 15;
			}
		}
		if ((state->auxiliary_flags & 2) && mode && *mode == 2) {
			backup_request(state, rows, groups, scores, count, ops, context);
			result = 0;
		}
		if (!(state->auxiliary_flags & 2) && mode && *mode == 2) {
			state->auxiliary_flags = (state->auxiliary_flags & 0xd7) | 2;
			state->request.flags |= 4;
			state->mode = 2;
			result = 0;
			if ((state->auxiliary_flags & 0x2e) == 6) {
				if (!(state->auxiliary_flags & 1)) {
					backup_request(state, rows, groups, scores, count, ops, context);
				} else { (void)phy_request((uint8_t)state->transmit_channel, ops, context); }
			}
		}
	}
	ops->unlock(AR_CHANNEL_AUX_LOCK, context);
	return result;
}

int ar_channel_policy_set(struct ar_channel_policy *state,
	const uint32_t *rows, const uint8_t *groups, const int32_t *scores,
	size_t count, const uint8_t *mode, const uint32_t *channel,
	const struct ar_channel_policy_ops *ops, void *context)
{
	if (!state || !rows || !groups || !scores || !count || count > 128 ||
		state->channel >= count || (uint8_t)state->auxiliary_channel >= count ||
		!ops || !ops->lock || !ops->unlock || !ops->phy || !ops->control ||
		!ops->timer || !ops->post) { return -22; }
	ops->lock(AR_CHANNEL_POLICY_LOCK, context);
	int result = auxiliary(state, rows, groups, scores, count, mode, channel, ops, context);
	if (result == -2) {
		if (mode) {
			state->mode = *mode != 0;
			state->request.flags = (state->request.flags & (uint8_t)~4u) | (*mode ? 4 : 0);
		}
		if (channel && *channel != state->channel) {
			(void)ar_channel_request(&state->request, 1, (uint16_t)*channel, ops->post, context);
		}
		result = 0;
	}
	ops->unlock(AR_CHANNEL_POLICY_LOCK, context);
	return result;
}
