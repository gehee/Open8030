/* SPDX-License-Identifier: MIT */
#include "ar_data_worker.h"
#include <limits.h>

int ar_data_worker_step(struct ar_data_worker *worker,
	const struct ar_data_worker_ops *ops, void *context)
{
	if (!worker || !worker->queue || !ops || !ops->lock || !ops->receive ||
		!ops->unlock || !ops->buffer || !ops->delivery.find ||
		!ops->delivery.retain || !ops->delivery.enqueue ||
		!ops->delivery.release || !ops->delivery.diagnose) { return -22; }
	struct ar_data_worker_message message = {0};
	ops->lock(context);
	int result = ops->receive(&message, context);
	if (!result) { ++worker->queue->free; }
	ops->unlock(context);
	if (result) { return -9995; }
	struct ar_data_packet *packet = message.packet;
	if (!packet) { return -22; }
	result = -22;
	if (!message.action || !packet->data || packet->length > INT_MAX) { goto release; }
	++worker->packets;
	worker->bytes += packet->length;
	struct ar_data_action_buffer *buffer = ops->buffer(packet, context);
	if (!buffer || !buffer->storage || buffer->head > buffer->tail ||
		buffer->tail > buffer->capacity || buffer->capacity > INT_MAX ||
		buffer->storage + buffer->head != packet->data ||
		buffer->tail - buffer->head != packet->length) { goto release; }
	result = ar_data_action_apply(buffer, message.action->flags, message.source,
		&message.metadata, message.action->metadata_override, &ops->action, context);
	packet->data = buffer->storage + buffer->head;
	packet->length = (uint32_t)(buffer->tail - buffer->head);
	if (result) { goto release; }
	if (message.action->flags & 0xc8) {
		return ar_data_route_deliver(&message.action->route, packet, message.metadata,
			message.source, &worker->delivery, &ops->delivery, context);
	}
release:
	ops->delivery.release(packet, context);
	return result;
}
