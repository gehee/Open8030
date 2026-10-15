/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_stream_queue queue;
	uint8_t data[80], input[200];
	unsigned locks, unlocks, wakes, waits, behavior;
	uint32_t last_wait;
	uint64_t milliseconds;
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(mutex == f->queue.mutex && timeout == UINT32_MAX);
	f->locks++;
	return -1; /* The original ignores RTOS return values. */
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(mutex == f->queue.mutex);
	f->unlocks++;
}
static void wake(uint32_t signal, void *context)
{
	struct fixture *f = context;
	assert(signal == f->queue.ready_signal && f->locks == f->unlocks + 1);
	f->wakes++;
}
static uint64_t clock_us(void *context)
{
	struct fixture *f = context;
	uint64_t now = f->milliseconds;
	f->milliseconds += 7;
	return now * 1000 + 999;
}
static int wait(uint32_t signal, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(signal == f->queue.signal && f->locks == f->unlocks);
	assert(timeout && timeout <= 100);
	f->waits++;
	f->last_wait = timeout;
	if (f->behavior == 1) {
		/* Simulate acquisition and retirement between producer passes. */
		f->queue.read = f->queue.acquire = f->queue.write;
		f->queue.available = f->queue.used = 0;
	} else if (f->behavior == 2) {
		f->queue.flags = 0;
	}
	return -1;
}
static const struct ar_tx_stream_write_ops ops = {lock, unlock, wake, clock_us, wait};
static void init(struct fixture *f, unsigned read, unsigned used, unsigned available)
{
	memset(f, 0, sizeof *f);
	f->queue = (struct ar_tx_stream_queue){.capacity = 80, .read = read,
		.write = (read + used) % 80, .acquire = (read + used - available) % 80,
		.used = used, .available = available, .flags = 2, .mutex = 2,
		.ready_signal = 3, .signal = 4, .written_bytes = UINT64_MAX - 7};
	for (unsigned i = 0; i < sizeof f->input; i++)
		f->input[i] = (uint8_t)(i * 7 + 3);
	memset(f->data, 0xa5, sizeof f->data);
}
static int write_queue(struct fixture *f, uint32_t amount, uint32_t timeout,
	unsigned require, unsigned discard)
{
	return ar_tx_queue_write(&f->queue, f->data, sizeof f->data, f->input,
		amount, timeout, require, discard, 9, &ops, f);
}
int main(void)
{
	struct fixture f;
	init(&f, 79, 0, 0);
	assert(write_queue(&f, 3, 0, 0, 0) == 3);
	assert(f.data[79] == 3 && f.data[0] == 10 && f.data[1] == 17);
	assert(f.queue.write == 2 && f.queue.used == 3 && f.queue.available == 3);
	assert(f.queue.pending == 9 && f.queue.high_water == 3);
	assert(f.queue.written_bytes == UINT64_MAX - 4 && f.wakes == 1 && !f.waits);

	init(&f, 7, 78, 11);
	assert(write_queue(&f, 20, 0, 0, 0) == 2);
	assert(f.queue.used == 80 && f.queue.available == 13 && f.queue.pending == 0);
	assert(f.queue.short_writes == 1 && f.wakes == 1 && !f.waits);
	assert(f.data[5] == 3 && f.data[6] == 10);

	/* Discard cannot free acquired bytes. Its effects survive space rejection. */
	init(&f, 7, 78, 11);
	assert(write_queue(&f, 20, 0, 1, 1) == -12);
	assert(f.queue.used == 67 && f.queue.available == 0 && f.queue.write == 74);
	assert(f.queue.acquire == 74 && f.queue.read == 7 && !f.queue.pending);
	assert(f.queue.written_bytes == UINT64_MAX - 18 && !f.wakes);
	for (unsigned i = 0; i < sizeof f.data; i++) assert(f.data[i] == 0xa5);
	assert(write_queue(&f, 13, 0, 1, 1) == 13 && f.queue.used == 80);
	assert(f.queue.available == 13 && f.queue.write == 7 && f.queue.pending == 9);

	/* Full queues still wake readers; the timeout counter wraps. */
	init(&f, 7, 80, 0);
	f.queue.short_writes = UINT32_MAX;
	assert(write_queue(&f, 3, 0, 0, 0) == 0);
	assert(f.wakes == 1 && !f.queue.short_writes);

	/* Finite waits cap at 100 ms, and elapsed deadlines preserve partial data. */
	init(&f, 7, 78, 11);
	assert(write_queue(&f, 161, 250, 0, 0) == 2);
	assert(f.waits && f.queue.short_writes == 1 && !f.queue.pending);
	init(&f, 7, 78, 11);
	f.milliseconds = UINT32_MAX - 15;
	f.behavior = 1;
	assert(write_queue(&f, 161, 30, 0, 0) == 161);
	assert(f.waits == 2 && f.queue.pending == 9 && !f.queue.short_writes);
	assert(f.queue.written_bytes == 153 && f.queue.used == 79);
	assert(f.queue.high_water == 80 && f.locks == f.unlocks);

	/* All signed-negative timeouts are indefinite, not just UINT32_MAX. */
	for (unsigned i = 0; i < 3; i++) {
		const uint32_t timeout[] = {UINT32_MAX, 0x80000000, 0x80000007};
		init(&f, 7, 78, 11);
		f.behavior = 1;
		assert(write_queue(&f, 161, timeout[i], 0, 0) == 161);
		assert(f.waits == 2 && f.last_wait == 100 && f.queue.pending == 9);
	}
	init(&f, 7, 78, 11);
	f.behavior = 2;
	assert(write_queue(&f, 20, UINT32_MAX, 0, 0) == -10);
	assert(f.queue.used == 80 && f.queue.available == 13 && !f.queue.short_writes);
	assert(f.wakes == 1 && f.waits == 1 && f.locks == f.unlocks);

	init(&f, 0, 0, 0);
	assert(write_queue(&f, 0, 0, 0, 0) == -6 && !f.locks);
	assert(ar_tx_queue_write(NULL, f.data, 80, f.input, 1, 0, 0, 0, 0, &ops, &f) == -6);
	assert(ar_tx_queue_write(&f.queue, f.data, 80, NULL, 1, 0, 0, 0, 0, &ops, &f) == -6);
	f.queue.flags = 0;
	assert(write_queue(&f, 1, 0, 0, 0) == -10 && !f.wakes);
	init(&f, 0, 0, 0);
	f.queue.capacity = 81;
	assert(write_queue(&f, 1, 0, 0, 0) == -22 && f.locks == f.unlocks);
	assert(ar_tx_queue_write(&f.queue, f.data, 80, f.input, 1, 0, 0, 0, 0, NULL, &f) == -22);
	puts("TX queue producer tests passed");
	return 0;
}
