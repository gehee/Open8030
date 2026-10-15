/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_WORKER_H
#define AR_DATA_WORKER_H
#include "ar_data_action.h"
#include "ar_data_dispatch.h"

/* Native queue/action views, not original object layouts. Routes, actions,
 * packets and their storage must outlive every queued reference. The caller
 * serializes workers that share mutable packet or route state. */
struct ar_data_worker_action {
	uint64_t flags;
	uint8_t metadata_override;
	struct ar_data_route route;
};
struct ar_data_worker_message {
	struct ar_data_packet *packet;
	struct ar_data_worker_action *action;
	uint8_t metadata;
	uint16_t source;
};
struct ar_data_worker {
	struct ar_data_dispatch_worker *queue;
	uint64_t bytes, packets; /* Received length, before transformations. */
	struct ar_data_route_stats delivery;
};
struct ar_data_worker_ops {
	void (*lock)(void *context);
	/* Success transfers one packet reference. Copy all message fields. */
	int (*receive)(struct ar_data_worker_message *, void *context);
	void (*unlock)(void *context);
	/* Return persistent mutable cursor/storage metadata for this packet.
	 * Its logical span must agree with packet->data/length on entry. */
	struct ar_data_action_buffer *(*buffer)(struct ar_data_packet *, void *context);
	struct ar_data_action_ops action;
	struct ar_data_route_ops delivery;
};

/* Source-only single worker iteration: receive under lock, restore queue free
 * count, unlock, account input, transform, then deliver for flags & 0xc8.
 * Return -9995 after a failed receive, 0 after normal processing, or -22 on a
 * native message/storage/adapter guard. Initial guards acquire no ownership.
 * Storage capacity must fit INT_MAX, keeping transformed lengths representable
 * by the destination writer's signed result.
 * After successful receive, every exit releases the received packet reference
 * once (if nonnull), independently of any allocation reference owned elsewhere.
 * Partial transformations survive a later guard, as in ar_data_action_apply.
 * Original discard control, queue-age diagnostics and additional error counters
 * are not modeled here. Real scheduling, pools and runtime ownership are separate. */
int ar_data_worker_step(struct ar_data_worker *, const struct ar_data_worker_ops *,
	void *context);
#endif
