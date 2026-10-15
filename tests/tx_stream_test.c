/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"
#include "ar_tx_retire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_stream_registry registry;
	struct ar_tx_stream stream;
	struct ar_tx_stream_queue queue;
	struct ar_tx_stream_entry entries[4];
	unsigned locks, unlocks, wakes, buffers, mutate;
	uint32_t last_unlock;
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(timeout == UINT32_MAX);
	assert(mutex == f->registry.mutex || mutex == f->queue.mutex);
	f->locks++;
	return -1; /* The original consumer continues despite this return. */
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	f->last_unlock = mutex;
	f->unlocks++;
}
static void wake(uint32_t signal, void *context)
{
	struct fixture *f = context;
	assert(signal == 0x3333);
	f->wakes++;
}
static void buffer(uint32_t callback, uint32_t argument, void *context)
{
	struct fixture *f = context;
	assert(callback == 0x300100 && argument == 0xcafe0000);
	assert(f->entries[0].consumed == 8 && f->queue.read == 0 && f->queue.used == 60);
	f->buffers++;
	if (f->mutate) {
		f->stream.references = 7;
		f->queue.pending = 0x1234;
		f->queue.mutex = 0x4444;
		f->registry.mutex = 0x5555;
	}
}
static const struct ar_tx_stream_ops ops = {lock, unlock, wake, buffer};
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->registry.mutex = 0x1111;
	f->registry.streams[1][2][0] = &f->stream;
	f->stream.references = 2;
	f->stream.flags = 4;
	f->stream.queue = &f->queue;
	f->queue = (struct ar_tx_stream_queue){.capacity = 80, .write = 60,
		.used = 60, .pending = 20, .mutex = 0x2222, .signal = 0x3333,
		.entries = f->entries, .entry_count = 4};
	for (unsigned i = 0; i < 4; ++i)
		f->entries[i] = (struct ar_tx_stream_entry){.length = 8, .produced = 8};
	f->entries[0].callback = 0x300100;
	f->entries[0].argument = 0xcafe0000;
}
static void retire(unsigned selector, unsigned port, unsigned direction,
	uint32_t amount, void *context)
{
	struct fixture *f = context;
	assert(!ar_tx_stream_release(&f->registry, selector, port, direction, amount, &ops, f));
}
int main(void)
{
	struct fixture f;
	init(&f);
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 11, &ops, &f));
	assert(f.entries[0].consumed == 8 && f.entries[1].consumed == 3);
	assert(f.queue.read == 20 && f.queue.used == 40 && f.queue.pending == 9);
	assert(f.buffers == 1 && f.wakes == 1 && f.locks == 5 && f.unlocks == 5);
	assert(f.stream.references == 1);

	/* Two packet releases compose through the actual stream consumer. */
	init(&f);
	uint8_t packets[2][12] = {{0,0,0,0,0,0,0,1,2,0x80,0,0},
		{0,0,0,0,0,0,0,1,2,0x30,0,0}};
	struct ar_tx_retire_ring ring = {.configuration = 1u << 24, .head = 2, .owned = 2};
	for (unsigned i = 0; i < 2; ++i)
		ring.packets[i] = (struct ar_tx_retire_packet){packets[i], 12};
	assert(!ar_tx_retire(&ring, 0, retire, &f));
	assert(!ring.owned && ring.tail == 2 && ring.head == 2);
	assert(!f.stream.references && f.queue.pending == 9 && f.queue.used == 40);
	assert(f.entries[0].consumed == 8 && f.entries[1].consumed == 3 && f.buffers == 1);

	init(&f);
	f.mutate = 1;
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 8, &ops, &f));
	assert(f.stream.references == 5 && f.queue.pending == 0x122c && f.last_unlock == 0x5555);
	assert(f.queue.mutex == 0x4444 && f.locks == f.unlocks);
	init(&f);
	f.stream.flags = 0;
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 21, &ops, &f));
	assert(f.queue.read == 21 && f.queue.used == 39 && f.queue.pending == 20);
	assert(!f.buffers && f.wakes == 1 && !f.entries[0].consumed);
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 100, &ops, &f));
	assert(f.queue.read == 21 && f.queue.used == 39 && f.wakes == 1);
	init(&f);
	f.entries[0].produced = 5;
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 100, &ops, &f));
	assert(f.entries[0].consumed == 5 && f.queue.pending == UINT32_MAX - 79);
	assert(f.queue.used == 60 && !f.queue.read && !f.buffers && !f.wakes);
	init(&f);
	f.entries[0].produced = 1;
	f.entries[0].consumed = 2;
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, UINT32_MAX, &ops, &f));
	assert(f.entries[0].consumed == 1 && f.queue.pending == 21);
	init(&f);
	f.stream.references = UINT32_MAX;
	f.stream.queue = NULL;
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 8, &ops, &f));
	assert(!f.stream.references && f.locks == 3 && f.unlocks == 3);
	init(&f);
	f.registry.streams[1][2][1] = &f.stream;
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 255, 0, &ops, &f));
	assert(f.stream.references == 1 && !f.entries[0].consumed && !f.buffers);
	init(&f);
	assert(!ar_tx_stream_release(&f.registry, 8, 2, 0, 8, &ops, &f));
	assert(!ar_tx_stream_release(&f.registry, 1, 5, 0, 8, &ops, &f));
	assert(!f.locks && f.stream.references == 2);
	assert(!ar_tx_stream_release(&f.registry, 1, 3, 0, 8, &ops, &f));
	assert(f.locks == 1 && f.unlocks == 1 && f.stream.references == 2);
	init(&f);
	f.queue.entries = NULL;
	assert(ar_tx_stream_release(&f.registry, 1, 2, 0, 8, &ops, &f) == -22);
	assert(f.locks == f.unlocks && f.stream.references == 1 && f.queue.pending == 20);
	init(&f);
	f.queue.entry_count = 1;
	assert(ar_tx_stream_release(&f.registry, 1, 2, 0, 11, &ops, &f) == -22);
	assert(f.buffers == 1 && f.entries[0].consumed == 8 && f.queue.used == 60);
	assert(f.locks == f.unlocks && f.stream.references == 1);
	init(&f);
	assert(ar_tx_stream_release(&f.registry, 1, 2, 0, 8, NULL, &f) == -22 && !f.locks);
	puts("TX stream partial/composed release, callback ordering, accounting wrap and guarded cleanup passed");
}
