/* SPDX-License-Identifier: MIT */
#include "ar_channel_transaction.h"

static void put32(uint8_t *output, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++) { output[i] = value >> (8*i); }
}

int ar_channel_route_request(const struct ar_channel_route *route,
	uint32_t selector, uint16_t channel,
	const struct ar_channel_transaction_ops *ops, void *context)
{
	if (!route || !ops || !ops->lock || !ops->unlock || !ops->phy || !ops->post) { return -22; }
	enum ar_channel_transaction_lock which = route->role ?
		AR_CHANNEL_TRANSACTION_DEV : AR_CHANNEL_TRANSACTION_AP;
	ops->lock(which, context);
	if (route->role ? route->dev_receive == channel :
		(!selector && route->ap_broadcast_transmit == channel)) {
		ops->unlock(which, context);
		return 0;
	}
	uint8_t flags;
	if (route->role) { flags = (route->slot & 15) | 0x80; }
	else if (!selector) {
		flags = route->channel_flags & 8 ? 0xc0 : 0x80;
		if (route->mode == 3) { flags |= 0x10; }
	} else {
		flags = route->mode == 1 && route->relay == 1 ? 0x70 : 0x50;
		if (route->channel_flags & 8) { flags |= 0xa0; }
	}
	uint8_t payload[3] = {flags, (uint8_t)channel, (uint8_t)(channel >> 8)};
	int result = ops->phy(4, 16, payload, sizeof(payload), context);
	ops->unlock(which, context);
	if (result) {
		uint8_t completion[16] = {0};
		put32(completion+4, selector);
		completion[8] = (uint8_t)channel; completion[9] = channel >> 8;
		put32(completion+12, 1);
		(void)ops->post(0, 0x109, completion, sizeof(completion), UINT32_MAX, context);
	}
	return 0;
}

static void reset_histories(struct ar_channel_histories *histories)
{
	for (unsigned i = 0; i < 10; i++) {
		if (histories->first[i]) { *histories->first[i] = 0; }
	}
	for (unsigned i = 0; i < 8; i++) {
		if (histories->second[i]) { *histories->second[i] = 0; }
	}
}

int ar_channel_complete(struct ar_channel_policy *state,
	struct ar_channel_histories *histories, const uint32_t *rows,
	const uint8_t *groups, size_t count, uint32_t success, uint32_t selector,
	uint16_t channel, uint32_t error,
	const struct ar_channel_transaction_ops *ops, void *context)
{
	if (!state || !histories || !rows || !groups || !count || count > 255 ||
		!ops || !ops->lock || !ops->unlock || !ops->post ||
		(selector == 1 && success && channel == state->request.pending_channel &&
			(uint8_t)channel >= count)) { return -22; }
	ops->lock(AR_CHANNEL_TRANSACTION_POLICY, context);
	if (selector == 1) {
		if (state->request.flags & 0x20) {
			state->request.flags &= (uint8_t)~0x20u;
			histories->marker = 0;
			reset_histories(histories);
		}
		if (success) {
			if (channel == state->request.pending_channel) {
				state->channel = channel;
				state->band = (rows[(uint8_t)channel] >> 27) & 3;
				state->group = groups[(uint8_t)channel] & 15;
				state->request.flags &= (uint8_t)~8u;
			}
		} else if (!error) {
			state->request.flags &= (uint8_t)~0x20u;
			(void)ar_channel_request(&state->request, 1, state->channel, ops->post, context);
		}
	} else {
		if (state->request.flags & 0x40) {
			state->request.flags &= (uint8_t)~0x40u;
			reset_histories(histories);
		}
		if (success && channel == state->request.broadcast_channel) {
			state->request.flags &= (uint8_t)~0x10u;
		}
	}
	ops->unlock(AR_CHANNEL_TRANSACTION_POLICY, context);
	return 0;
}
