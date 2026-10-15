/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"
#include <string.h>

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

int ar_tx_stream_acquire(struct ar_tx_stream_registry *registry,
	unsigned selector, unsigned port, unsigned direction, uint32_t amount,
	struct ar_tx_stream_span output[5], uint32_t timeout_ms,
	const struct ar_tx_stream_acquire_ops *ops, void *context)
{
	if (!registry || !output || !ops || !ops->lock || !ops->unlock ||
	    !ops->clock_us || !ops->wait)
		return -22;
	if (!amount)
		return -1;
	memset(output, 0, 2 * sizeof *output);
	if (selector > 7 || port > 4)
		return -1;
	(void)ops->lock(registry->mutex, UINT32_MAX, context);
	struct ar_tx_stream *stream = registry->streams[selector][port][!!direction];
	if (stream)
		stream->references++;
	ops->unlock(registry->mutex, context);
	if (!stream)
		return -1;
	uint32_t budget = (uint32_t)(amount * stream->percentage) / 100;
	if (!budget)
		budget = 1;
	struct ar_tx_stream_queue *queue = stream->queue;
	struct ar_tx_stream_span spans[5] = {{0}};
	unsigned count = (stream->flags & 4) ? 5 : 2;
	uint32_t total = 0;
	int result = 0;
	if (queue && count == 5) {
		(void)ops->lock(queue->mutex, UINT32_MAX, context);
		uint32_t offset = queue->read < queue->capacity ? queue->read : 0;
		size_t visited = 0;
		unsigned index = 0;
		int present = !!queue->used;
		while (present && index < 5 && total < budget) {
			struct ar_tx_stream_entry *current = entry(queue, offset);
			if (!current || visited++ >= queue->entry_count) {
				result = -22;
				break;
			}
			if (current->produced < current->length) {
				uint32_t take = current->length - current->produced;
				if (take > budget - total)
					take = budget - total;
				spans[index++] = (struct ar_tx_stream_span){
					current->address + current->produced, take};
				current->produced = (uint16_t)(current->produced + take);
				total += take;
			}
			offset += 20;
			if (offset >= queue->capacity)
				offset = 0;
			present = offset != queue->write;
		}
		ops->unlock(queue->mutex, context);
	} else if (queue) {
		uint32_t deadline = timeout_ms + (uint32_t)(ops->clock_us(context) / 1000);
		for (;;) {
			(void)ops->lock(queue->mutex, UINT32_MAX, context);
			if (!(queue->flags & 2)) {
				ops->unlock(queue->mutex, context);
				break;
			}
			uint32_t cursor = queue->acquire;
			memset(spans, 0, 2 * sizeof *spans);
			if (queue->available) {
				if (!queue->capacity || cursor >= queue->capacity ||
				    queue->write > queue->capacity) {
					result = -22;
				} else {
					spans[0].address = queue->data_address + cursor;
					if (cursor < queue->write) {
						spans[0].length = queue->write - cursor;
					} else {
						spans[0].length = queue->capacity - cursor;
						spans[1].address = queue->data_address;
						spans[1].length = queue->write;
					}
					if (spans[0].length > budget)
						spans[0].length = budget;
					if (spans[1].length > budget - spans[0].length)
						spans[1].length = budget - spans[0].length;
					total = spans[0].length + spans[1].length;
					if (total) {
						queue->acquire = (cursor + total) % queue->capacity;
						queue->available -= total;
						queue->acquired_bytes += total;
					}
				}
			}
			ops->unlock(queue->mutex, context);
			if (result || total || !timeout_ms)
				break;
			uint32_t remaining = 100;
			if (timeout_ms != UINT32_MAX) {
				remaining = deadline - (uint32_t)(ops->clock_us(context) / 1000);
				if (!remaining || remaining > INT32_MAX)
					break;
			}
			(void)ops->wait(queue->ready_signal, remaining, context);
		}
	}
	if (!total || result) {
		const struct ar_tx_stream_ops common = {.lock = ops->lock, .unlock = ops->unlock};
		drop(registry, stream, &common, context);
	}
	if (result)
		memset(spans, 0, sizeof spans);
	memcpy(output, spans, count * sizeof *output);
	return result;
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
