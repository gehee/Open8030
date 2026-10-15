/* SPDX-License-Identifier: MIT */
#include "ar_tx_alloc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_tx_stream *stream;
	struct ar_tx_stream_queue *queue;
	uint8_t *data;
	unsigned allocations, fail, syncfail, signals, frees, destroys, held;
	uint32_t allocation_bytes;
};
static struct ar_tx_stream *stream_alloc(uint32_t bytes, void *context)
{
	struct fixture *f = context;
	assert(bytes == 12);
	if (++f->allocations == f->fail) return NULL;
	f->stream = malloc(sizeof *f->stream);
	assert(f->stream);
	*f->stream = (struct ar_tx_stream){.references = UINT32_MAX, .flags = 0xa5,
		.percentage = 0xa5};
	return f->stream;
}
static struct ar_tx_stream_queue *queue_alloc(uint32_t bytes, void *context)
{
	struct fixture *f = context;
	assert(bytes >= 80 && bytes < AR_TX_ALLOC_LIMIT);
	f->allocation_bytes = bytes;
	if (++f->allocations == f->fail) return NULL;
	f->queue = malloc(sizeof *f->queue);
	f->data = malloc(bytes - 80 ? bytes - 80 : 1);
	assert(f->queue && f->data);
	memset(f->queue, 0xa5, sizeof *f->queue);
	memset(f->data, 0xa5, bytes - 80 ? bytes - 80 : 1);
	f->queue->entries = (struct ar_tx_stream_entry *)f->data;
	f->queue->entry_count = (bytes - 80) / 20;
	f->queue->data_address = 0x1000;
	return f->queue;
}
static void stream_free(struct ar_tx_stream *s, void *context)
{
	struct fixture *f = context;
	assert(s == f->stream && !s->queue && !s->references);
	f->frees++; free(s); f->stream = NULL;
}
static uint32_t mutex_create(void *context)
{
	struct fixture *f = context;
	assert(f->queue->flags == 2 && f->queue->capacity == f->allocation_bytes - 80);
	assert(!f->queue->used && !f->queue->mutex && !f->queue->ready_signal);
	assert(!f->queue->written_bytes && !f->queue->acquired_bytes);
	assert(f->queue->entries == (struct ar_tx_stream_entry *)f->data);
	assert(f->queue->entry_count == f->queue->capacity / 20 && f->queue->data_address == 0x1000);
	assert(f->data[0] == 0xa5);
	return f->syncfail & 1 ? 0 : 0x101;
}
static uint32_t signal_create(uint32_t maximum, uint32_t initial, void *context)
{
	struct fixture *f = context;
	assert(maximum == 1 && !initial && f->signals < 2);
	assert(f->queue->mutex == (f->syncfail & 1 ? 0 : 0x101));
	if (f->signals) assert(f->queue->ready_signal == (f->syncfail & 2 ? 0 : 0x102));
	f->signals++;
	return f->syncfail & (1u << f->signals) ? 0 : 0x101 + f->signals;
}
static const struct ar_tx_alloc_ops factory = {
	stream_alloc, queue_alloc, stream_free, mutex_create, signal_create
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert((mutex == 11 || mutex == 0x101) && timeout == UINT32_MAX);
	f->held++;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	(void)mutex; assert(f->held); f->held--;
}
static void destroy(uint32_t token, void *context)
{
	struct fixture *f = context;
	assert(!token || (token >= 0x101 && token <= 0x103));
	f->destroys++;
}
static void free_queue(struct ar_tx_stream_queue *q, void *context)
{
	struct fixture *f = context;
	assert(q == f->queue);
	free(f->data); free(q); f->data = NULL; f->queue = NULL; f->frees++;
}
static void delay(uint32_t units, void *context)
{
	(void)units; (void)context; assert(!"unexpected delay");
}
static void wake(uint32_t token, void *context) { (void)context; assert(token == 0x102 || token == 0x103); }
static uint64_t clock_us(void *context) { (void)context; return 1000; }
static int wait_signal(uint32_t token, uint32_t timeout, void *context)
{
	(void)token; (void)timeout; (void)context; assert(!"unexpected wait"); return 0;
}
static void buffer(uint32_t callback, uint32_t argument, void *context)
{
	(void)callback; (void)argument; (void)context; assert(!"unexpected retained callback");
}
static const struct ar_tx_stream_close_ops close_ops = {
	lock, unlock, destroy, free_queue, stream_free, delay
};
int main(void)
{
	struct fixture f;
	for (unsigned retained = 0; retained <= 1; retained++) {
		for (unsigned syncfail = 0; syncfail < 8; syncfail++) {
			memset(&f, 0, sizeof f); f.syncfail = syncfail;
			struct ar_tx_stream *s = ar_tx_stream_create(retained ? UINT32_MAX : 64, retained, &factory, &f);
			assert(s && s->percentage == 100 && !s->references && s->flags == retained * 4);
			assert(s->queue->capacity == (retained ? 100 : 64) && f.signals == 2 && !f.frees);
			struct ar_tx_stream_registry registry = {.mutex = 11, .present_mask = 1};
			registry.streams[0][1][0] = s;
			assert(!ar_tx_stream_close(&registry, 0, 1, 2, &close_ops, &f));
			assert(f.frees == 2 && f.destroys == 3 && !f.held);
		}
	}
	for (unsigned fail = 1; fail <= 2; fail++) {
		memset(&f, 0, sizeof f); f.fail = fail;
		assert(!ar_tx_stream_create(64, 0, &factory, &f));
		assert(f.allocations == fail && f.frees == fail - 1 && !f.signals);
	}
	memset(&f, 0, sizeof f);
	assert(!ar_tx_queue_create(AR_TX_ALLOC_LIMIT - 80, &factory, &f));
	assert(!ar_tx_queue_create(UINT32_MAX - 79, &factory, &f));
	assert(!ar_tx_queue_create(UINT32_MAX, &factory, &f) && !f.allocations);
	assert(ar_tx_queue_create(AR_TX_ALLOC_LIMIT - 81, &factory, &f));
	assert(f.allocation_bytes == AR_TX_ALLOC_LIMIT - 1); free_queue(f.queue, &f);
	memset(&f, 0, sizeof f);
	assert(ar_tx_queue_create(0, &factory, &f) && !f.queue->capacity); free_queue(f.queue, &f);
	assert(!ar_tx_stream_create(64, 0, NULL, &f));
	/* Compose real factory storage with the compared producer/ownership path. */
	memset(&f, 0, sizeof f);
	struct ar_tx_stream *s = ar_tx_stream_create(64, 0, &factory, &f);
	assert(s);
	struct ar_tx_stream_registry registry = {.mutex = 11, .present_mask = 1};
	registry.streams[0][1][0] = s;
	struct ar_tx_stream_write_ops write_ops = {lock, unlock, wake, clock_us, wait_signal};
	struct ar_tx_stream_acquire_ops acquire_ops = {lock, unlock, clock_us, wait_signal};
	struct ar_tx_stream_ops release_ops = {lock, unlock, wake, buffer};
	const uint8_t input[16] = "factory stream";
	assert(ar_tx_queue_write(s->queue, f.data, 64, input, 16, 0, 0, 0, 16, &write_ops, &f) == 16);
	assert(!memcmp(f.data, input, 16));
	struct ar_tx_stream_span spans[5];
	assert(!ar_tx_stream_acquire(&registry, 0, 1, 0, 16, spans, 0, &acquire_ops, &f));
	assert(spans[0].address == 0x1000 && spans[0].length == 16 && s->references == 1);
	assert(!ar_tx_stream_release(&registry, 0, 1, 0, 16, &release_ops, &f));
	assert(!s->references && !s->queue->used);
	assert(!ar_tx_stream_close(&registry, 0, 1, 2, &close_ops, &f));
	assert(f.frees == 2 && f.destroys == 3 && !registry.streams[0][1][0] && !f.held);
	puts("tx_alloc_test: passed");
	return 0;
}
