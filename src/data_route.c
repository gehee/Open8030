/* SPDX-License-Identifier: MIT */
#include "ar_data_route.h"
#include <limits.h>

int ar_data_route_deliver(struct ar_data_route *route, struct ar_data_packet *packet,
	uint8_t metadata, uint16_t source, struct ar_data_route_stats *stats,
	const struct ar_data_route_ops *ops, void *context)
{
	if (!route || !packet || !packet->data || packet->length > INT_MAX ||
	    !stats || !ops || !ops->find || !ops->retain || !ops->enqueue ||
	    !ops->release || !ops->diagnose) { return -22; }
	for (unsigned id = 0; id < 64; ++id) {
		if (!(route->destinations & (UINT64_C(1) << id))) { continue; }
		struct ar_data_destination *destination = ops->find(id, context);
		if (!destination) { ++stats->missing_destinations; continue; }
		if (!destination->enabled) { ++destination->disabled; continue; }
		if (destination->queued) {
			ops->retain(packet, context);
			if (ops->enqueue(destination, packet, route, metadata, source, context)) {
				ops->diagnose(destination, context);
				ops->release(packet, context);
				++stats->delivery_failures;
			}
		} else if (destination->write) {
			int result = destination->write(destination, metadata, packet->data,
				packet->length, source, context);
			/* The original re-reads length after the callback. */
			if ((uint32_t)result != packet->length) {
				ops->diagnose(destination, context);
				++stats->delivery_failures;
			} else {
				route->bytes += packet->length;
				destination->bytes += packet->length;
				++route->packets;
			}
		}
	}
	ops->release(packet, context);
	return 0;
}
