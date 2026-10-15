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

int ar_data_writer_step(struct ar_data_writer *writer,
	const struct ar_data_writer_ops *ops, void *context)
{
	if (!writer || !writer->destination || !writer->destination->write ||
	    !ops || !ops->lock || !ops->receive || !ops->unlock ||
	    !ops->release || !ops->diagnose) { return -22; }
	struct ar_data_destination *destination = writer->destination;
	struct ar_data_message message = {0};
	ops->lock(context);
	if (ops->receive(&message, context)) {
		ops->unlock(context);
		return 0;
	}
	struct ar_data_packet *packet = message.packet;
	struct ar_data_route *route = message.route;
	if (!packet || !packet->data || packet->length > INT_MAX || !route ||
	    !destination->write) {
		if (packet) { ops->release(packet, context); }
		ops->unlock(context);
		return -22;
	}
	int result = destination->write(destination, message.metadata, packet->data,
		packet->length, message.source, context);
	if ((uint32_t)result != packet->length) {
		ops->diagnose(destination, context);
		++writer->failures;
	} else {
		route->bytes += packet->length;
		destination->bytes += packet->length;
		++route->packets;
	}
	ops->release(packet, context);
	ops->unlock(context);
	return 1;
}
