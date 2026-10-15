/* SPDX-License-Identifier: MIT */
#ifndef AR_CHANNEL_LINK_H
#define AR_CHANNEL_LINK_H
#include "ar_channel_work.h"

/* Native state views; no original object offsets or ABI are implied. */
struct ar_channel_dev_channels { uint16_t broadcast_tx, broadcast_rx, slot_tx, slot_rx; };
struct ar_channel_ap_channels {
	uint16_t broadcast_rx, broadcast_tx, receive, slot_tx[8];
};
struct ar_channel_link {
	struct ar_channel_dev_channels dev;
	struct ar_channel_ap_channels ap;
	uint16_t auxiliary_channel, hold_mask;
	int32_t ignore_once;
	uint8_t role, mode, slot, channel_flags, current_band;
	uint8_t auxiliary_flags, auxiliary_user, auxiliary_direction;
	uint8_t monitor_flags, monitor_threshold, monitor_channel, monitor_deadline;
};
enum ar_channel_link_lock { AR_CHANNEL_LINK_NODE, AR_CHANNEL_LINK_AUXILIARY };
struct ar_channel_link_ops {
	void (*lock)(enum ar_channel_link_lock which, void *context);
	void (*unlock)(enum ar_channel_link_lock which, void *context);
	ar_channel_field field;
	int (*tune)(unsigned user, unsigned direction, uint16_t channel, void *context);
	int (*notify)(unsigned event, const uint8_t payload[4], void *context);
	ar_channel_post post;
};
/* PHY command4 callback. outgoing is the original callback direction argument;
 * any nonzero value selects outgoing handling. RX=1, TX=0 for tune operations.
 * State changes precede their corresponding effects, even if tune/notify/post
 * returns an error. A pending monitor suppresses immediate completion.
 * Cross-band preparation is included; actual tuning and completion monitoring
 * belong to the caller. rows uses the ar_scan/catalog band encoding.
 * Return0 means handled, not successful tuning. Native -22 rejects malformed
 * storage/length/header and unsafe cross-band lookup before any effect. */
int ar_channel_link_apply(struct ar_channel_link *state,
	const uint32_t *rows, size_t count, uint32_t outgoing,
	const uint8_t *payload, size_t length,
	const struct ar_channel_link_ops *ops, void *context);
#endif
