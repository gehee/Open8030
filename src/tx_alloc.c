/* SPDX-License-Identifier: MIT */
#include "ar_tx_alloc.h"

static int valid(const struct ar_tx_alloc_ops *ops)
{
	return ops && ops->stream_alloc && ops->queue_alloc && ops->stream_free &&
		ops->mutex_create && ops->signal_create;
}

struct ar_tx_stream_queue *ar_tx_queue_create(uint32_t capacity,
	const struct ar_tx_alloc_ops *ops, void *context)
{
	if (!valid(ops) || capacity > UINT32_MAX - 80 ||
	    capacity + 80 >= AR_TX_ALLOC_LIMIT)
		return NULL;
	struct ar_tx_stream_queue *queue = ops->queue_alloc(capacity + 80, context);
	if (!queue)
		return NULL;
	struct ar_tx_stream_entry *entries = queue->entries;
	size_t entry_count = queue->entry_count;
	uint32_t data_address = queue->data_address;
	*queue = (struct ar_tx_stream_queue){.capacity = capacity, .flags = 2,
		.entries = entries, .entry_count = entry_count, .data_address = data_address};
	queue->mutex = ops->mutex_create(context);
	queue->ready_signal = ops->signal_create(1, 0, context);
	queue->signal = ops->signal_create(1, 0, context);
	return queue;
}

struct ar_tx_stream *ar_tx_stream_create(uint32_t capacity, unsigned retained,
	const struct ar_tx_alloc_ops *ops, void *context)
{
	if (!valid(ops))
		return NULL;
	struct ar_tx_stream *stream = ops->stream_alloc(12, context);
	if (!stream)
		return NULL;
	stream->references = 0;
	stream->percentage = 100;
	stream->flags = 0;
	struct ar_tx_stream_queue *queue = ar_tx_queue_create(retained ? 100 : capacity,
		ops, context);
	stream->queue = queue;
	if (retained)
		stream->flags |= 4;
	if (!queue) {
		ops->stream_free(stream, context);
		return NULL;
	}
	return stream;
}
