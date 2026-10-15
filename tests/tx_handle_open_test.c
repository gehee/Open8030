/* SPDX-License-Identifier: MIT */
#include "ar_tx_handle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_tx_handles handles;
	struct ar_tx_handle_record records[3];
	struct ar_tx_stream_registry registry;
	unsigned held, allocations, fail, destroyed, freed, mutation, reserved;
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert((mutex == 11 || mutex == 44) && timeout == UINT32_MAX && !f->held);
	f->held = mutex;
	if (mutex == 44 && f->mutation == 1) f->records[0].bytes[0] |= 1;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(mutex == f->held);
	f->held = 0;
	if (mutex == 44 && f->mutation == 2) {
		/* Reservation is visible, but selector/port have not been published. */
		assert(f->records[0].bytes[0] & 1);
		assert(f->records[0].bytes[4] == 0xa5 && f->records[0].bytes[8] == 0xa5);
		f->reserved++;
		f->records[0].bytes[0] = 17u << 3; /* Clear active, replace encoded ID. */
	}
}
static void destroy_sync(uint32_t token, void *context)
{
	struct fixture *f = context;
	assert(f->held == 11 && token >= 1 && token <= 3);
	f->destroyed++;
}
static void free_queue(struct ar_tx_stream_queue *queue, void *context)
{
	struct fixture *f = context;
	assert(f->held == 11);
	f->freed++;
	free(queue);
}
static void free_stream(struct ar_tx_stream *stream, void *context)
{
	struct fixture *f = context;
	assert(f->held == 11 && !stream->queue && !stream->references);
	f->freed++;
	free(stream);
}
static void delay(uint32_t units, void *context)
{
	(void)units; (void)context;
	assert(!"unexpected held reference");
}
static void destroy_unpublished(struct ar_tx_stream *s, void *context)
{
	destroy_sync(s->queue->ready_signal, context);
	destroy_sync(s->queue->signal, context);
	destroy_sync(s->queue->mutex, context);
	free_queue(s->queue, context);
	s->queue = NULL;
	free_stream(s, context);
}
static struct ar_tx_stream *create(uint32_t capacity, unsigned retained, void *context)
{
	struct fixture *f = context;
	assert(f->held == 11 && capacity);
	if (++f->allocations == f->fail) return NULL;
	struct ar_tx_stream *s = calloc(1, sizeof *s);
	assert(s);
	s->queue = calloc(1, sizeof *s->queue);
	assert(s->queue);
	s->percentage = 100; s->flags = retained ? 4 : 0;
	*s->queue = (struct ar_tx_stream_queue){.flags = 2, .capacity = capacity,
		.mutex = 1, .ready_signal = 2, .signal = 3};
	return s;
}
static const struct ar_tx_stream_open_ops open_ops = {lock, unlock, create, destroy_unpublished};
static const struct ar_tx_stream_close_ops close_ops = {
	lock, unlock, destroy_sync, free_queue, free_stream, delay
};
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->handles = (struct ar_tx_handles){44, 3, f->records, 3};
	f->registry.mutex = 11; f->registry.present_mask = 0xff;
	for (unsigned i = 0; i < 3; i++) {
		memset(f->records[i].bytes, 0xa5, 12);
		memset(f->records[i].bytes, 0, 4);
		f->records[i].bytes[0] = i << 3;
	}
}
static int open_handle(struct fixture *f, unsigned role, unsigned selector,
	unsigned port, unsigned flags)
{
	return ar_tx_handle_open(&f->handles, role, selector, port, flags, 64, 128,
		&f->registry, &open_ops, &close_ops, f);
}
int main(void)
{
	struct fixture f;
	for (unsigned flags = 1; flags <= 3; flags++) {
		init(&f);
		assert(open_handle(&f, 0, 7, 3, flags) == 0);
		assert(f.records[0].bytes[0] == (1 | flags << 1));
		assert(f.records[0].bytes[4] == 7 && f.records[0].bytes[8] == 4);
		assert(f.records[0].bytes[9] == 0xa5 && !f.held);
		assert(!ar_tx_handle_close(&f.handles, 0, &f.registry, &close_ops, &f));
		assert(!(f.records[0].bytes[0] & 1) && !f.held);
		assert(f.destroyed == 3 * f.allocations && f.freed == 2 * f.allocations);
		assert(!f.registry.streams[7][4][0] && !f.registry.streams[7][4][1]);
		assert(open_handle(&f, 0, 7, 3, flags) == 0);
		assert(!ar_tx_handle_close(&f.handles, 0, &f.registry, &close_ops, &f));
	}
	init(&f); f.mutation = 1;
	assert(open_handle(&f, 1, 0, 0, 3) == 1);
	assert(!ar_tx_handle_close(&f.handles, 1, &f.registry, &close_ops, &f));
	/* Return the encoded 13-bit ID, ignoring upper-word residue and slot index. */
	init(&f);
	f.records[0].bytes[0] = 0xf8; f.records[0].bytes[1] = 0xff;
	f.records[0].bytes[2] = 0xff; f.records[0].bytes[3] = 0xff;
	assert(open_handle(&f, 1, 0, 0, 3) == 8191);
	assert(!ar_tx_stream_close(&f.registry, 0, 1, 3, &close_ops, &f));
	init(&f); f.mutation = 2;
	assert(open_handle(&f, 1, 0, 0, 3) == 17 && f.reserved == 1);
	assert(!(f.records[0].bytes[0] & 1));
	/* Original publication does not restore the active bit after unlock. */
	f.mutation = 0;
	assert(!ar_tx_stream_close(&f.registry, 0, 1, 3, &close_ops, &f));
	for (unsigned count = 0; count <= 3; count++) {
		init(&f); f.handles.count = count;
		for (unsigned i = 0; i < count; i++) f.records[i].bytes[0] |= 1;
		assert(open_handle(&f, 1, 0, 2, 3) == -11);
		assert(f.allocations == 2 && f.freed == 4 && f.destroyed == 6 && !f.held);
		assert(!f.registry.streams[0][3][0] && !f.registry.streams[0][3][1]);
	}
	for (unsigned fail = 1; fail <= 2; fail++) {
		init(&f); f.fail = fail;
		assert(open_handle(&f, 1, 0, 0, 3) == -3);
		assert(f.freed == (fail - 1) * 2 && !(f.records[0].bytes[0] & 1));
	}
	init(&f);
	assert(open_handle(&f, 1, 1, 0, 3) == -6);
	assert(open_handle(&f, 0, 8, 0, 3) == -6);
	assert(open_handle(&f, 0, 0, 4, 3) == -6 && !f.allocations);
	/* Native bounds failure cleans up streams; it is not original -11 parity. */
	f.handles.count = 4;
	assert(open_handle(&f, 1, 0, 0, 3) == -22);
	assert(f.freed == 4 && f.destroyed == 6 && !f.held);
	init(&f); f.handles.records = NULL;
	assert(open_handle(&f, 1, 0, 0, 3) == -22 && f.freed == 4);
	puts("tx_handle_open_test: passed");
	return 0;
}
