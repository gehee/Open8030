/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"

static void drop(struct ar_tx_stream_registry *registry,
	struct ar_tx_stream *stream, const struct ar_tx_stream_ops *ops, void *context)
{
	(void)ops->lock(registry->mutex, UINT32_MAX, context);
	if (stream->references)
		stream->references--;
	ops->unlock(registry->mutex, context);
}

static int consume(struct ar_tx_stream_queue *queue, uint32_t amount,
	const struct ar_tx_stream_ops *ops, void *context)
{
	if (!queue || !amount)
		return 0;
	(void)ops->lock(queue->mutex, UINT32_MAX, context);
	int result = 0;
	if (queue->used >= amount) {
		if (!queue->capacity) {
			result = -22;
		} else {
			queue->read = (queue->read + amount) % queue->capacity;
			queue->used -= amount;
			ops->wake(queue->signal, context);
		}
	}
	ops->unlock(queue->mutex, context);
	return result;
}

static struct ar_tx_stream_entry *entry(struct ar_tx_stream_queue *queue,
	uint32_t offset)
{
	if (!queue->entries || !queue->capacity || queue->capacity % 20 ||
	    offset % 20 || offset >= queue->capacity || offset / 20 >= queue->entry_count)
		return NULL;
	return &queue->entries[offset / 20];
}

int ar_tx_stream_release(struct ar_tx_stream_registry *registry,
	unsigned selector, unsigned port, unsigned direction, uint32_t amount,
	const struct ar_tx_stream_ops *ops, void *context)
{
	if (!registry || !ops || !ops->lock || !ops->unlock || !ops->wake || !ops->buffer)
		return -22;
	if (selector > 7 || port > 4)
		return 0;
	(void)ops->lock(registry->mutex, UINT32_MAX, context);
	struct ar_tx_stream *stream = registry->streams[selector][port][!!direction];
	if (stream)
		stream->references++;
	ops->unlock(registry->mutex, context);
	if (!stream)
		return 0;
	struct ar_tx_stream_queue *queue = stream->queue;
	int result = 0;
	if (!(stream->flags & 4)) {
		result = consume(queue, amount, ops, context);
	} else if (queue && amount) {
		(void)ops->lock(queue->mutex, UINT32_MAX, context);
		uint32_t done = 0, completed = 0;
		uint32_t offset = queue->read < queue->capacity ? queue->read : 0;
		int present = !!queue->used;
		while (present && done < amount) {
			struct ar_tx_stream_entry *current = entry(queue, offset);
			if (!current || completed >= queue->entry_count) {
				result = -22;
				break;
			}
			uint32_t available = (uint32_t)current->produced - current->consumed;
			uint32_t take = amount - done;
			if (take > available)
				take = available;
			if (!take)
				break;
			current->consumed = (uint16_t)(current->consumed + take);
			done += take;
			if (current->consumed != current->produced ||
			    current->produced != current->length)
				break;
			if (current->callback)
				ops->buffer(current->callback, current->argument, context);
			offset += 20;
			if (offset >= queue->capacity)
				offset = 0;
			present = offset != queue->write;
			completed++;
		}
		if (!result)
			queue->pending -= amount;
		ops->unlock(queue->mutex, context);
		if (!result && completed)
			result = consume(queue, completed * 20, ops, context);
	}
	drop(registry, stream, ops, context);
	drop(registry, stream, ops, context);
	return result;
}
