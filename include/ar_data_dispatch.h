/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_DISPATCH_H
#define AR_DATA_DISPATCH_H
#include "ar_data_filter.h"
#include "ar_data_route.h"

/* Source-only submission stage; native views, not original object layouts.
 * Caller serializes the route list/worker counters. Construction, parser input
 * preparation, actual queues and downstream action execution are separate. */
struct ar_data_dispatch_node {
	struct ar_data_filter filter;
	uint64_t actions;
	uint8_t worker;
	uint64_t bytes, packets;
	struct ar_data_dispatch_node *next;
};
struct ar_data_dispatch_worker { uint32_t free, low; };
struct ar_data_dispatch_stats { uint64_t unmatched, invalid_worker, queue_failures; };
struct ar_data_dispatch_ops {
	struct ar_data_filter_ops filter;
	/* Copy adapter must preserve backing capacity, data offset and bytes using
	 * its own packet metadata. Null means allocation/copy failure. It retains
	 * ownership of the allocation's initial reference: submission adds a queue
	 * reference but does not release that initial reference. Downstream lifetime
	 * for copying actions is not established by this helper. */
	struct ar_data_packet *(*copy)(struct ar_data_packet *, void *context);
	void (*retain)(struct ar_data_packet *, void *context);
	void (*release)(struct ar_data_packet *, void *context);
	/* Copy message fields on success; transfer the added reference to the queue.
	 * On failure, submission releases that added reference. */
	int (*enqueue)(unsigned worker, struct ar_data_dispatch_node *,
		struct ar_data_packet *, uint8_t metadata, uint16_t source, void *context);
	int (*wake)(uint8_t value, void *context);
};

/* Owns one caller reference after initial argument validation. Releases it on
 * every processing exit. Returns 0 when any predicate matched (even if all
 * submissions failed), -4996 if none matched, -4998 for an invalid worker index,
 * or -22 for native adapter/filter guards. Initial guards do not take ownership.
 * matches includes suppressed matches; node counters count attempted actions,
 * including copy/queue failures, but exclude suppressed/invalid-worker cases.
 * Action bits 1/2 request copies, bits 3/5 share first-attempt suppression, and
 * bit 5 selects worker 0. Other worker choices use the logical worker map.
 * This models submission effects, not the downstream meaning of every action. */
int ar_data_dispatch(struct ar_data_dispatch_node *routes,
	struct ar_data_packet *packet, const struct ar_data_filter_input *input,
	uint8_t metadata, uint16_t source,
	struct ar_data_dispatch_worker *workers, uint32_t worker_count,
	const uint16_t *worker_map, size_t map_count,
	struct ar_data_dispatch_stats *stats, uint32_t *matches,
	const struct ar_data_dispatch_ops *ops, void *context);
#endif
