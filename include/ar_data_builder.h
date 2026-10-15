/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_BUILDER_H
#define AR_DATA_BUILDER_H
#include "ar_data_worker.h"

/* Semantic configuration, not a binary-table or original object layout. */
struct ar_data_route_spec {
	uint8_t disabled;
	uint64_t flags;
	struct ar_data_identity source, secondary, destination;
	uint8_t minimum, maximum;
	uint16_t offset, pattern_length;
	const uint8_t *pattern;
	uint32_t role;
	uint16_t worker;
	uint8_t metadata_override;
	uint64_t actions;
};
struct ar_data_built_route {
	/* First member lets a built route participate in the dispatch list. */
	struct ar_data_dispatch_node dispatch;
	struct ar_data_worker_action *action;
	uint8_t pattern[38];
};
struct ar_data_builder_ops {
	struct ar_data_filter_ops interfaces;
	struct ar_data_built_route *(*node)(void *context);
	struct ar_data_worker_action *(*action)(void *context);
};

/* Source-only construction/merge, with caller-serialized list and registry.
 * The list must contain only ar_data_built_route objects. Allocator adapters own
 * all returned storage, including an unlinked node if its action allocation
 * fails. No rollback/free is performed: earlier records remain applied on error.
 * Nodes prepend; matching nodes retain their filter and accumulate destinations.
 * A merge overwrites worker/metadata but ignores pattern-byte differences when
 * flags, relevant IDs/range, pattern length/offset and action flags match.
 * Source and secondary identities selected by flags must resolve to IDs <64;
 * destination resolution is required only for actions & 0xc8. Actions containing
 * both 0x08 and 0x20 are rejected. Worker is stored as its low byte.
 * Return 0, interface lookup error, -10000 for original admission bounds,
 * -9999 for allocation failure, or -22 for native adapter/storage guards.
 * Native guards limit pattern storage to 38 bytes and count to UINT16_MAX;
 * oversized original copies/counter wrap are not emulated. */
int ar_data_routes_build(struct ar_data_built_route **head,
	const struct ar_data_route_spec *, size_t count, uint8_t role,
	const struct ar_data_builder_ops *, void *context);
#endif
