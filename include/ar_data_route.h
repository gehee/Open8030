/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_ROUTE_H
#define AR_DATA_ROUTE_H
#include <stddef.h>
#include <stdint.h>

/* Source-only delivery stage for an already selected, untransformed packet.
 * These are native views, not original object layouts. Mode-3 routes use this
 * action. Parsing, route selection, transforms and worker scheduling are separate.
 * The caller serializes access and supplies one owned packet reference. */
struct ar_data_packet { const uint8_t *data; uint32_t length; };
struct ar_data_route { uint64_t destinations, bytes, packets; };
struct ar_data_route_stats { uint64_t missing_destinations, delivery_failures; };
struct ar_data_destination {
	int enabled, queued;
	uint64_t bytes, disabled;
	int (*write)(struct ar_data_destination *, uint8_t metadata,
		const uint8_t *data, uint32_t length, uint16_t source, void *context);
};
struct ar_data_route_ops {
	struct ar_data_destination *(*find)(unsigned id, void *context);
	void (*retain)(struct ar_data_packet *, void *context);
	/* Success transfers the extra reference to the destination worker. The
	 * adapter must copy the message fields; route/packet must outlive that work. */
	int (*enqueue)(struct ar_data_destination *, struct ar_data_packet *,
		struct ar_data_route *, uint8_t metadata, uint16_t source, void *context);
	void (*release)(struct ar_data_packet *, void *context);
	void (*diagnose)(struct ar_data_destination *, void *context);
};

/* Returns zero after attempting every selected destination, even when delivery
 * fails. Always releases the caller's reference on valid input. Invalid adapter
 * inputs return -22 without taking ownership. Callback success is exact-length
 * equality, not proof that an eventual subscriber consumed the packet. */
int ar_data_route_deliver(struct ar_data_route *, struct ar_data_packet *,
	uint8_t metadata, uint16_t source, struct ar_data_route_stats *,
	const struct ar_data_route_ops *, void *context);

struct ar_data_message {
	struct ar_data_packet *packet;
	struct ar_data_route *route;
	uint8_t metadata;
	uint16_t source;
};
struct ar_data_writer {
	struct ar_data_destination *destination;
	uint64_t failures;
};
struct ar_data_writer_ops {
	void (*lock)(void *context);
	/* A successful receive transfers one packet reference into message. The
	 * original waits with its interface lock held; scheduling is the adapter's
	 * responsibility. Nonzero means no message was acquired. */
	int (*receive)(struct ar_data_message *, void *context);
	void (*unlock)(void *context);
	void (*release)(struct ar_data_packet *, void *context);
	void (*diagnose)(struct ar_data_destination *, void *context);
};
/* One iteration of a constructed destination writer. Returns 1 after consuming
 * a message, including a failed write; 0 after a receive failure; -22 for native
 * adapter/message guards. A received nonnull packet is released before unlock,
 * including on guard failure. This does not recheck destination enabled/queued:
 * the original processes work already transferred to its writer. */
int ar_data_writer_step(struct ar_data_writer *, const struct ar_data_writer_ops *,
	void *context);
#endif
