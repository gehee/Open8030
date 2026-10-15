/* SPDX-License-Identifier: MIT */
#ifndef AR_CHANNEL_TRANSACTION_H
#define AR_CHANNEL_TRANSACTION_H
#include "ar_channel_policy.h"

/* Native views of the link's current channel state and completion histories;
 * these are not original object layouts. */
struct ar_channel_route {
	uint16_t dev_receive, ap_broadcast_transmit;
	uint8_t role, slot, mode, channel_flags, relay;
};
struct ar_channel_histories {
	uint16_t marker;
	/* Optional pointers to the individual counter bytes, not object bases. */
	uint8_t *first[10], *second[8];
};
enum ar_channel_transaction_lock {
	AR_CHANNEL_TRANSACTION_POLICY, AR_CHANNEL_TRANSACTION_DEV, AR_CHANNEL_TRANSACTION_AP
};
struct ar_channel_transaction_ops {
	void (*lock)(enum ar_channel_transaction_lock which, void *context);
	void (*unlock)(enum ar_channel_transaction_lock which, void *context);
	int (*phy)(unsigned command, unsigned ttl, const uint8_t *payload,
		size_t length, void *context);
	ar_channel_post post;
};
/* Link message0x108: send command4/TTL16 with a three-byte channel request.
 * An already-selected DEV channel or selector0 AP broadcast channel only locks
 * and unlocks. Any enqueue failure posts a failed PHY completion (message109),
 * after releasing the link lock. Native clears unwritten completion padding.
 * Return0 means handled, not channel-change success; native validation is -22. */
int ar_channel_route_request(const struct ar_channel_route *route,
	uint32_t selector, uint16_t channel,
	const struct ar_channel_transaction_ops *ops, void *context);
/* PHY message0x109: selector1 commits only a matching pending full16-bit
 * channel. Failure with error0 stages rollback; posting failure leaves it
 * pending. Other selectors clear broadcast pending state only on a matching
 * success. Inflight flags control counter resets; selector1 also clears the
 * complete 16-bit history marker. Band/group lookup uses the channel low byte.
 * This does not tune hardware or establish peer acknowledgement semantics.
 * Native validation leaves state untouched. Callbacks must preserve storage. */
int ar_channel_complete(struct ar_channel_policy *state,
	struct ar_channel_histories *histories, const uint32_t *rows,
	const uint8_t *groups, size_t count, uint32_t success, uint32_t selector,
	uint16_t channel, uint32_t error,
	const struct ar_channel_transaction_ops *ops, void *context);
#endif
