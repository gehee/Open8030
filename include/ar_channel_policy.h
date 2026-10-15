/* SPDX-License-Identifier: MIT */
#ifndef AR_CHANNEL_POLICY_H
#define AR_CHANNEL_POLICY_H
#include "ar_channel_work.h"

/* Native state for selector1 of the original channel-policy entry point.
 * Catalog and score storage are supplied by the caller. Base channels only;
 * current/auxiliary channel must address allocated entries. */
struct ar_channel_policy {
	struct ar_channel_request_state request;
	/* transmit_channel is the DEV slot TX channel or AP broadcast TX channel. */
	uint16_t channel, auxiliary_channel, transmit_channel, backups[4];
	uint8_t mode, band, group, auxiliary_flags, role, slots;
};
enum ar_channel_policy_lock { AR_CHANNEL_POLICY_LOCK, AR_CHANNEL_AUX_LOCK };
struct ar_channel_policy_ops {
	void (*lock)(enum ar_channel_policy_lock which, void *context);
	void (*unlock)(enum ar_channel_policy_lock which, void *context);
	/* PHY user8/port8, four bytes. Native clears the original unspecified
	 * high nibble of byte3. Return value controls leaving hopping mode. */
	int (*phy)(unsigned user, unsigned port, const uint8_t *payload,
		size_t length, void *context);
	/* Internal port0 command10, eight bytes. DEV sends to slot0; AP sends
	 * to each enabled slot. Sending failure does not suppress the timer. */
	int (*control)(unsigned slot, unsigned command, const uint8_t *payload,
		size_t length, void *context);
	void (*timer)(uint32_t milliseconds, void *context);
	ar_channel_post post;
};
/* Optional mode/channel pointers preserve the original absent-argument
 * distinction. A full 32-bit channel request is compared with the current
 * channel before truncation to16 bits for posting. Return0 is policy acceptance,
 * not completed tuning; the original ignores the queue's posting failure.
 * Native -22 rejects invalid storage/callbacks before locking or mutation.
 * Caller owns synchronization outside the specified lock/unlock operations;
 * callbacks must preserve valid state and storage. */
int ar_channel_policy_set(struct ar_channel_policy *state,
	const uint32_t *rows, const uint8_t *groups, const int32_t *scores,
	size_t count, const uint8_t *mode, const uint32_t *channel,
	const struct ar_channel_policy_ops *ops, void *context);
#endif
