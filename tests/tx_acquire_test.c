/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_stream_registry registry;
	struct ar_tx_stream stream;
	struct ar_tx_stream_queue queue;
	struct ar_tx_stream_entry entries[7];
	struct ar_tx_stream_span spans[5];
	unsigned locks, unlocks, buffers, wakes, waits, clocks, behavior;
	uint32_t wait_ms;
	uint64_t now_ms;
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(mutex == f->registry.mutex || mutex == f->queue.mutex);
	assert(timeout == UINT32_MAX);
	f->locks++;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(mutex == f->registry.mutex || mutex == f->queue.mutex);
	f->unlocks++;
}
static uint64_t clock_us(void *context)
{
	struct fixture *f = context;
	f->clocks++;
	uint64_t now = f->now_ms;
	f->now_ms += 7;
	return now * 1000 + 999;
}
static int wait(uint32_t signal, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(signal == 0x6666 && f->locks == f->unlocks);
	f->waits++;
	f->wait_ms = timeout;
	if (f->behavior == 1) {
		f->queue.write = f->queue.available = f->queue.used = 30;
	} else if (f->behavior == 2) {
		f->queue.flags = 0;
	}
	return -1;
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
	assert(callback == 1 && argument < 7);
	assert(f->entries[argument].produced == 8 && f->entries[argument].consumed == 8);
	f->buffers++;
}
static const struct ar_tx_stream_acquire_ops acquire_ops = {lock, unlock, clock_us, wait};
static const struct ar_tx_stream_ops release_ops = {lock, unlock, wake, buffer};
static void init(struct fixture *f, unsigned retained)
{
	memset(f, 0, sizeof *f);
	f->registry.mutex = 0x1111;
	f->registry.streams[1][2][0] = &f->stream;
	f->stream = (struct ar_tx_stream){.references = 2, .flags = retained ? 4 : 0,
		.queue = &f->queue, .percentage = 100};
	f->queue = (struct ar_tx_stream_queue){.capacity = retained ? 140 : 80,
		.used = retained ? 140 : 0, .pending = 56, .mutex = 0x2222,
		.signal = 0x3333, .entries = f->entries, .entry_count = 7,
		.flags = 2, .data_address = 0x281050, .ready_signal = 0x6666};
	for (unsigned i = 0; i < 7; ++i)
		f->entries[i] = (struct ar_tx_stream_entry){.address = 0xfffffffc + i * 100,
			.length = 8, .argument = i, .callback = 1};
	memset(f->spans, 0xa5, sizeof f->spans);
}
static int acquire(struct fixture *f, uint32_t amount, uint32_t timeout)
{
	return ar_tx_stream_acquire(&f->registry, 1, 2, 0, amount, f->spans,
		timeout, &acquire_ops, f);
}
int main(void)
{
	struct fixture f;
	init(&f, 1);
	assert(acquire(&f, 0, 0) == -1 && !f.locks && f.spans[0].address == 0xa5a5a5a5);
	assert(!acquire(&f, 44, 0));
	assert(f.stream.references == 3 && f.queue.pending == 56 && f.queue.used == 140);
	for (unsigned i = 0; i < 5; ++i)
		assert(f.spans[i].length == 8 && f.entries[i].produced == 8);
	assert(!f.entries[5].produced);
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 40, &release_ops, &f));
	assert(f.stream.references == 2 && f.queue.pending == 16 && f.queue.used == 40);
	assert(f.queue.read == 100 && f.buffers == 5 && f.wakes == 1 && f.locks == f.unlocks);

	init(&f, 1);
	f.entries[0].produced = 4;
	f.stream.percentage = 0;
	assert(!acquire(&f, UINT32_MAX, 0));
	assert(!f.spans[0].address && f.spans[0].length == 1 && f.entries[0].produced == 5);
	init(&f, 1);
	f.entries[0].length = 65544;
	f.entries[0].produced = 65530;
	f.stream.percentage = 255;
	assert(!acquire(&f, 0x01010102, 0)); /* Product wraps to 254; budget is two. */
	assert(f.spans[0].length == 2 && f.entries[0].produced == 65532);
	assert(!acquire(&f, 10, 0));
	assert(f.spans[0].length == 12 && f.entries[0].produced == 8);
	init(&f, 1);
	for (unsigned i = 0; i < 7; ++i)
		f.entries[i].produced = 8;
	assert(!acquire(&f, 80, 0) && f.stream.references == 2 && !f.spans[0].length);
	assert(f.locks == f.unlocks && f.locks == 3);

	init(&f, 0);
	f.queue.acquire = 70;
	f.queue.write = 20;
	f.queue.available = f.queue.used = 30;
	f.queue.acquired_bytes = UINT64_MAX - 7;
	assert(!acquire(&f, 25, 0));
	assert(f.spans[0].address == 0x281096 && f.spans[0].length == 10);
	assert(f.spans[1].address == 0x281050 && f.spans[1].length == 15);
	assert(f.spans[2].address == 0xa5a5a5a5 && f.queue.acquire == 15);
	assert(f.queue.available == 5 && f.queue.acquired_bytes == 17 && f.queue.used == 30);
	assert(!ar_tx_stream_release(&f.registry, 1, 2, 0, 25, &release_ops, &f));
	assert(f.stream.references == 2 && f.queue.read == 25 && f.queue.used == 5);
	assert(f.queue.pending == 56 && !f.buffers && f.wakes == 1);

	init(&f, 0);
	f.behavior = 1;
	f.now_ms = UINT32_MAX - 15;
	assert(!acquire(&f, 80, 30));
	assert(f.waits == 1 && f.wait_ms == 23 && f.spans[0].length == 30);
	assert(f.stream.references == 3 && f.queue.acquired_bytes == 30);
	init(&f, 0);
	assert(!acquire(&f, 80, 8));
	assert(f.waits == 1 && f.wait_ms == 1 && f.clocks == 3 && f.stream.references == 2);
	init(&f, 0);
	f.behavior = 2;
	assert(!acquire(&f, 80, UINT32_MAX));
	assert(f.waits == 1 && f.wait_ms == 100 && f.clocks == 1 && f.stream.references == 2);
	init(&f, 0);
	f.behavior = 1;
	assert(!acquire(&f, 80, 0x80000000));
	assert(f.waits == 1 && f.wait_ms == 0x7ffffff9);
	init(&f, 0);
	assert(!acquire(&f, 80, 0x80000007) && !f.waits);

	init(&f, 1);
	f.registry.streams[1][2][0] = NULL;
	assert(acquire(&f, 8, 0) == -1 && f.locks == 1 && !f.spans[0].length);
	assert(f.spans[2].address == 0xa5a5a5a5);
	init(&f, 1);
	f.queue.entry_count = 1;
	assert(acquire(&f, 9, 0) == -22 && f.entries[0].produced == 8);
	assert(f.stream.references == 2 && f.locks == f.unlocks && !f.spans[0].length);
	init(&f, 0);
	f.queue.available = 1;
	f.queue.capacity = 0;
	assert(acquire(&f, 1, 0) == -22 && f.locks == f.unlocks && f.stream.references == 2);
	puts("TX acquisition budgets, span bounds, ownership composition, ring wrap and timeout cleanup passed");
}
