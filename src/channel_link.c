/* SPDX-License-Identifier: MIT */
#include "ar_channel_link.h"

static void notify(const struct ar_channel_link_ops *ops, void *context,
	unsigned user, unsigned direction, uint16_t channel, uint16_t previous)
{
	uint8_t payload[4] = {user, direction, channel, previous};
	(void)ops->notify(2, payload, context);
}

static void prepare(struct ar_channel_link *state, const uint32_t *rows,
	unsigned user, uint16_t channel, const struct ar_channel_link_ops *ops, void *context)
{
	if (((rows[(uint8_t)channel] >> 27) & 3) != state->current_band) {
		state->monitor_flags |= 0x20;
		state->hold_mask |= 1u << user;
		ops->field((0x9a4u + 8*user) << 16 | 0x1801, 1, context);
	}
	state->monitor_flags |= 0x40;
	state->monitor_channel = (uint8_t)channel;
	state->monitor_deadline = (uint8_t)(state->monitor_threshold + 16);
}

int ar_channel_link_apply(struct ar_channel_link *state,
	const uint32_t *rows, size_t count, uint32_t outgoing,
	const uint8_t *payload, size_t length,
	const struct ar_channel_link_ops *ops, void *context)
{
	if (!state || !rows || !count || count > 255 || !payload || length != 3 ||
		(payload[0] & 15) > 7 || !ops || !ops->lock || !ops->unlock ||
		!ops->field || !ops->tune || !ops->notify || !ops->post) { return -22; }
	uint8_t flags = payload[0];
	uint16_t channel = payload[1] | (uint16_t)payload[2] << 8;
	unsigned slot = flags & 15;
	unsigned arms = outgoing && !state->mode && (state->role ?
		(state->slot < 8 && slot == state->slot && (flags & 0x80)) : (flags & 0x40));
	if (arms && (uint8_t)channel >= count) { return -22; }
	ops->lock(AR_CHANNEL_LINK_NODE, context);
	if (!state->role) {
		if (!outgoing) {
			if (state->ignore_once) { state->ignore_once = 0; }
			else if (flags & 0x80) {
				uint16_t previous = state->ap.slot_tx[slot];
				state->ap.slot_tx[slot] = channel;
				if (!state->mode) { state->ap.broadcast_tx = channel; }
				(void)ops->tune(state->mode ? slot : 8, 0, channel, context);
				notify(ops, context, slot, 0, channel, previous);
			}
		} else {
			if (flags & 0x40) {
				uint16_t previous = state->ap.receive;
				state->ap.receive = channel;
				if (!state->mode) { prepare(state, rows, 0, channel, ops, context); }
				for (unsigned i = 0; i < 8; i++) { (void)ops->tune(i, 1, channel, context); }
				if (state->mode == 1) { notify(ops, context, 0, 1, channel, previous); }
			}
			if (flags & 0x10) {
				state->ap.broadcast_rx = channel;
				(void)ops->tune(8, 1, channel, context);
			}
			if (flags & 0x20) {
				state->ap.broadcast_tx = channel;
				(void)ops->tune(8, 0, channel, context);
			}
			if (flags & 0x80) {
				if (state->mode == 1) {
					for (unsigned i = 0; i < 8; i++) {
						state->ap.slot_tx[i] = channel;
						(void)ops->tune(i, 0, channel, context);
					}
				} else if (state->mode == 3) {
					uint16_t previous = state->ap.broadcast_tx;
					state->ap.broadcast_tx = channel;
					(void)ops->tune(8, 0, channel, context);
					notify(ops, context, 0, 0, channel, previous);
				}
			}
		}
	} else if (state->slot < 8) {
		if (outgoing) {
			if (slot == state->slot && (flags & 0x80)) {
				uint16_t previous = state->dev.slot_rx;
				state->dev.slot_rx = channel;
				if (!state->mode) {
					state->dev.broadcast_rx = channel;
					prepare(state, rows, 8, channel, ops, context);
				}
				(void)ops->tune(state->mode == 1 ? state->slot : 8, 1, channel, context);
				notify(ops, context, 0, 1, channel, previous);
			}
		} else if (!slot) {
			if (state->ignore_once > 0) { state->ignore_once = 0; }
			else {
				if (flags & 0x40) {
					uint16_t previous = state->dev.slot_tx;
					state->dev.slot_tx = channel;
					(void)ops->tune(state->slot, 0, channel, context);
					notify(ops, context, 0, 0, channel, previous);
				}
				if (flags & 0x10) {
					state->dev.broadcast_tx = channel;
					(void)ops->tune(8, 0, channel, context);
				}
				if (flags & 0x20) {
					state->dev.broadcast_rx = channel;
					(void)ops->tune(8, 1, channel, context);
				}
				if (flags & 0x80) {
					if (state->mode == 1) {
						state->dev.slot_rx = channel;
						(void)ops->tune(state->slot, 1, channel, context);
					} else if (state->mode == 3) {
						uint16_t previous = state->dev.broadcast_rx;
						state->dev.broadcast_rx = state->dev.slot_rx = channel;
						(void)ops->tune(8, 1, channel, context);
						notify(ops, context, 0, 1, channel, previous);
					}
				}
			}
		}
	}
	ops->unlock(AR_CHANNEL_LINK_NODE, context);
	ops->lock(AR_CHANNEL_LINK_AUXILIARY, context);
	if ((state->auxiliary_flags & 1) && (flags & 0x20)) {
		state->auxiliary_channel = (channel & 255) | 0x100;
		(void)ops->tune(state->auxiliary_user, state->auxiliary_direction,
			state->auxiliary_channel, context);
	}
	ops->unlock(AR_CHANNEL_LINK_AUXILIARY, context);
	if (outgoing && !(state->monitor_flags & 0x40)) {
		uint8_t completion[16] = {1, 0, 0, 0, 1, 0, 0, 0};
		if (!state->role) {
			completion[4] = state->mode == 3 ? 0 :
				state->channel_flags & 8 ? 1 : !(flags & 0x80);
		}
		completion[8] = (uint8_t)channel; completion[9] = channel >> 8;
		(void)ops->post(0, 0x109, completion, sizeof(completion), UINT32_MAX, context);
	}
	return 0;
}
