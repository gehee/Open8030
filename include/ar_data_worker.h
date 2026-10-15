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
	uint32_t submitted_at; /* Queue adapter stamps this using the ticks clock. */
};
struct ar_data_worker {
	struct ar_data_dispatch_worker *queue;
	uint64_t bytes, packets; /* Received length, before transformations. */
	struct ar_data_route_stats delivery;
	uint64_t aged, missing_packets, missing_actions;
	uint8_t discard; /* Any nonzero value consumes without action processing. */
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
	uint32_t (*ticks)(void *context);
};

/* Source-only single worker iteration: receive under lock, restore queue free
 * count, unlock, account input and age, apply discard policy, transform, then
 * deliver for flags & 0xc8. Age is unsigned (ticks - submitted_at) > 50; units
 * are supplied by the clock adapter, not assumed to be milliseconds.
 * Return -9995 after a failed receive, -9998 for a missing packet, -4999 for a
 * missing action, 0 after processing/discard, or -22 for native storage/adapter
 * guards. Initial guards acquire no ownership. A missing packet counts as one
 * received message with zero bytes. Discard takes precedence over missing action.
 * Storage capacity must fit INT_MAX, keeping transformed lengths representable
 * by the destination writer's signed result.
 * After successful receive, every exit releases the received packet reference
 * once (if nonnull), independently of any allocation reference owned elsewhere.
 * Partial transformations survive a later guard, as in ar_data_action_apply.
 * Original diagnostic text is not reproduced. Real scheduling, pool lifecycle,
 * configuration of the discard control and runtime ownership remain separate. */
int ar_data_worker_step(struct ar_data_worker *, const struct ar_data_worker_ops *,
	void *context);
#endif
