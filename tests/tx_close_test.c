/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"
#include "ar_tx_retire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_tx_stream_registry registry;
	struct ar_tx_stream stream[2];
	struct ar_tx_stream_queue queue[2];
	struct ar_tx_retire_ring ring;
	uint8_t packet[12];
	unsigned held_locks, delays, freed_queues, freed_streams, destroyed;
	unsigned behavior, dynamic, releases;
	uint32_t order[16];
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(timeout == UINT32_MAX && (mutex == 1 || mutex == 2 || mutex == 3));
	f->held_locks++;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(f->held_locks && (mutex == 1 || mutex == 2 || mutex == 3));
	f->held_locks--;
}
static void destroy_sync(uint32_t token, void *context)
{
	struct fixture *f = context;
	assert(f->held_locks == 1 && f->destroyed < 16);
	f->order[f->destroyed++] = token;
}
static void free_queue(struct ar_tx_stream_queue *queue, void *context)
{
	struct fixture *f = context;
	assert(f->held_locks == 1);
	f->freed_queues++;
	if (f->dynamic) free(queue);
}
static void free_stream(struct ar_tx_stream *stream, void *context)
{
	struct fixture *f = context;
	assert(!stream->references && !stream->queue && f->held_locks == 1);
	f->freed_streams++;
	if (f->dynamic) free(stream);
}
static void wake(uint32_t token, void *context)
{
	struct fixture *f = context;
	assert(token == 20 && f->held_locks == 1);
}
static void buffer(uint32_t callback, uint32_t argument, void *context)
{
	(void)callback; (void)argument; (void)context;
	assert(0);
}
static const struct ar_tx_stream_ops release_ops = {lock, unlock, wake, buffer};
static void release(unsigned selector, unsigned port, unsigned direction,
	uint32_t amount, void *context)
{
	struct fixture *f = context;
	assert(!ar_tx_stream_release(&f->registry, selector, port, direction,
		amount, &release_ops, f));
	f->releases++;
}
static void delay(uint32_t units, void *context)
{
	struct fixture *f = context;
	assert(units == 1 && !f->held_locks);
	f->delays++;
	if (f->behavior == 1) {
		assert(f->queue[0].flags == 0 && f->queue[0].disables == 1);
		assert(f->registry.streams[1][2][0] == &f->stream[0]);
		assert(!f->freed_streams);
		assert(!ar_tx_retire(&f->ring, 0, release, f));
	} else if (f->behavior == 2) {
		/* Reuse/publish a TX stream while close waits for RX references. */
		assert(f->delays == 1);
		f->stream[0].queue = &f->queue[0];
		f->registry.streams[1][2][0] = &f->stream[0];
		f->stream[1].references = 0;
	} else {
		for (unsigned i = 0; i < 2; i++)
			if (f->stream[i].references) f->stream[i].references--;
	}
}
static const struct ar_tx_stream_close_ops ops = {
	lock, unlock, destroy_sync, free_queue, free_stream, delay
};
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->registry.mutex = 1;
	f->registry.present_mask = 2;
	for (unsigned i = 0; i < 2; i++) {
		f->queue[i] = (struct ar_tx_stream_queue){.flags = 2, .capacity = 80,
			.used = 8, .write = 8, .available = 8, .data_address = 0x1000,
			.mutex = 2 + i, .ready_signal = 10 + i, .signal = 20 + i};
		f->stream[i] = (struct ar_tx_stream){.queue = &f->queue[i], .percentage = 100};
		f->registry.streams[1][2][i] = &f->stream[i];
	}
}
static uint64_t clock_us(void *context) { (void)context; return 1000; }
static int wait(uint32_t signal, uint32_t timeout, void *context)
{
	(void)signal; (void)timeout; (void)context; assert(0); return 0;
}
int main(void)
{
	struct fixture f;
	init(&f);
	f.queue[0].disables = UINT32_MAX;
	assert(!ar_tx_queue_disable(&f.queue[0], &ops, &f));
	assert(f.queue[0].flags == 0 && f.queue[0].disables == 0);
	assert(!ar_tx_queue_disable(&f.queue[0], &ops, &f) && !f.queue[0].disables);
	assert(!f.destroyed && !f.delays && !f.held_locks);
	assert(!ar_tx_queue_disable(NULL, NULL, NULL));
	assert(ar_tx_queue_disable(&f.queue[0], NULL, &f) == -22);

	/* Queued bytes do not substitute for held references. TX is destroyed first. */
	init(&f);
	f.queue[1].flags |= 1;
	assert(!ar_tx_stream_close(&f.registry, 1, 2, 7, &ops, &f));
	const uint32_t order[] = {10, 20, 2, 11, 21, 3};
	assert(f.destroyed == 6 && !memcmp(f.order, order, sizeof order));
	assert(f.freed_queues == 1 && f.freed_streams == 2 && !f.delays);
	assert(!f.registry.streams[1][2][0] && !f.registry.streams[1][2][1]);

	init(&f);
	f.stream[0].references = 2;
	f.stream[1].references = 3;
	assert(!ar_tx_stream_close(&f.registry, 1, 2, 3, &ops, &f));
	assert(f.delays == 3 && f.queue[0].disables == 1 && f.queue[1].disables == 1);
	assert(f.freed_streams == 2 && !f.held_locks);
	/* Once TX was destroyed, close does not revisit a replacement TX stream.
	 * If TX was merely absent, its mask bit remains set and it is revisited. */
	for (unsigned absent = 0; absent < 2; absent++) {
		init(&f);
		if (absent) f.registry.streams[1][2][0] = NULL;
		f.stream[1].references = 1;
		f.behavior = 2;
		assert(!ar_tx_stream_close(&f.registry, 1, 2, 3, &ops, &f));
		assert(f.delays == 1 && f.freed_streams == 2);
		assert(!!f.registry.streams[1][2][0] == !absent);
		assert(!f.registry.streams[1][2][1]);
	}

	/* A packet holds the acquired reference until retirement during close. */
	init(&f);
	f.registry.streams[1][2][1] = NULL;
	struct ar_tx_stream_span spans[5];
	const struct ar_tx_stream_acquire_ops acquire_ops = {lock, unlock, clock_us, wait};
	assert(!ar_tx_stream_acquire(&f.registry, 1, 2, 0, 8, spans, 0, &acquire_ops, &f));
	assert(f.stream[0].references == 1 && !f.queue[0].available && spans[0].length == 8);
	f.packet[7] = 1; f.packet[8] = 2; f.packet[9] = 0x80;
	f.ring = (struct ar_tx_retire_ring){.configuration = 1u << 24, .head = 1,
		.owned = 1, .packets = {{f.packet, sizeof f.packet}}};
	f.behavior = 1;
	assert(!ar_tx_stream_close(&f.registry, 1, 2, 2, &ops, &f));
	assert(f.delays == 1 && f.releases == 1 && !f.ring.owned && f.ring.tail == 1);
	assert(!f.queue[0].used && f.queue[0].read == 8 && f.freed_streams == 1);

	/* Exercise real frees under ASan: no reads of freed stream/queue afterward. */
	init(&f);
	f.dynamic = 1;
	struct ar_tx_stream *allocated = malloc(sizeof *allocated);
	struct ar_tx_stream_queue *queue = malloc(sizeof *queue);
	assert(allocated && queue);
	*allocated = f.stream[0]; *queue = f.queue[0]; allocated->queue = queue;
	f.registry.streams[1][2][0] = allocated;
	assert(!ar_tx_stream_close(&f.registry, 1, 2, 2, &ops, &f));
	assert(f.freed_streams == 1 && f.freed_queues == 1 && !f.registry.streams[1][2][0]);

	init(&f);
	assert(ar_tx_stream_close(&f.registry, 8, 2, 3, &ops, &f) == -6);
	assert(ar_tx_stream_close(&f.registry, 1, 5, 3, &ops, &f) == -6);
	assert(ar_tx_stream_close(&f.registry, 1, 2, 4, &ops, &f) == -6);
	assert(ar_tx_stream_close(&f.registry, 0, 2, 3, &ops, &f) == -2);
	assert(ar_tx_stream_close(&f.registry, 1, 2, 3, NULL, &f) == -22);
	assert(!f.held_locks && !f.destroyed);
	puts("TX stream disable, close ordering, held-reference retirement and lifetime tests passed");
	return 0;
}
