/* SPDX-License-Identifier: MIT */
#include "ar_tx_handle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_handle_record records[3];
	struct ar_tx_handles handles;
	struct ar_tx_stream_registry registry;
	struct ar_tx_stream stream;
	struct ar_tx_stream_queue queue;
	unsigned held, locks, delays, frees, destroyed, behavior, table_unlocks;
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(mutex >= 1 && mutex <= 3 && timeout == UINT32_MAX);
	f->locks++; f->held++;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(f->held);
	f->held--;
	if (mutex == 1 && ++f->table_unlocks == 1 && f->behavior == 1)
		f->records[1].bytes[0] &= ~1u;
}
static void destroy(uint32_t token, void *context)
{
	struct fixture *f = context;
	assert(f->held == 1 && (token == 3 || token == 4 || token == 5));
	f->destroyed++;
}
static void free_queue(struct ar_tx_stream_queue *q, void *context)
{
	struct fixture *f = context;
	assert(q == &f->queue && f->held == 1);
	f->frees++;
}
static void free_stream(struct ar_tx_stream *s, void *context)
{
	struct fixture *f = context;
	assert(s == &f->stream && !s->queue && !s->references && f->held == 1);
	f->frees++;
}
static void delay(uint32_t units, void *context)
{
	struct fixture *f = context;
	assert(units == 1 && !f->held && f->stream.references && !f->frees);
	assert(f->registry.streams[1][2][0] == &f->stream);
	if (!f->behavior) assert(f->records[1].bytes[0] & 1);
	f->delays++;
	f->stream.references--;
	if (f->behavior == 2) f->records[1].bytes[0] &= ~1u;
	if (f->behavior == 3) f->handles.count = 0;
	if (f->behavior == 4) {
		f->records[1].bytes[0] = 3;
		f->records[1].bytes[4] = 7;
		f->records[1].bytes[8] = 4;
	}
}
static const struct ar_tx_stream_close_ops ops = {
	lock, unlock, destroy, free_queue, free_stream, delay
};
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->handles = (struct ar_tx_handles){1, 3, f->records, 3};
	f->records[1] = (struct ar_tx_handle_record){{0xf5, 0xa5, 0xfe, 0xca,
		1, 0, 0, 0, 2, 0xef, 0xcd, 0xab}};
	f->registry.mutex = 2;
	f->registry.present_mask = 2;
	f->registry.streams[1][2][0] = &f->stream;
	f->queue = (struct ar_tx_stream_queue){.flags = 2, .mutex = 3,
		.ready_signal = 4, .signal = 5};
	f->stream = (struct ar_tx_stream){.queue = &f->queue};
}
static int close_handle(struct fixture *f)
{
	return ar_tx_handle_close(&f->handles, 1, &f->registry, &ops, f);
}
int main(void)
{
	struct fixture f;
	struct ar_tx_handle_record output, before;
	init(&f);
	memset(&output, 0x5a, sizeof output); before = output;
	assert(ar_tx_handle_read(&f.handles, 3, &output, 0, &ops, &f) == -1);
	assert(!memcmp(&output, &before, sizeof output) && !f.locks);
	assert(!ar_tx_handle_read(&f.handles, 1, &output, 255, &ops, &f));
	assert(!memcmp(&output, &f.records[1], sizeof output));
	before = f.records[1];
	assert(!ar_tx_handle_read(&f.handles, 1, &output, 0, &ops, &f));
	assert(!memcmp(&output, &before, sizeof output));
	assert(f.records[1].bytes[0] == 0xf4);
	assert(!memcmp(f.records[1].bytes + 1, before.bytes + 1, 11));
	assert(ar_tx_handle_read(&f.handles, 1, &output, 0, &ops, &f) == -1);

	/* Return value uses the copied bit even if an unlock callback changes it. */
	init(&f); f.behavior = 1;
	assert(!ar_tx_handle_read(&f.handles, 1, &output, 1, &ops, &f));
	assert(output.bytes[0] == 0xf5 && f.records[1].bytes[0] == 0xf4);

	init(&f); f.stream.references = 2;
	assert(!close_handle(&f));
	assert(f.delays == 2 && f.frees == 2 && f.destroyed == 3);
	assert(!f.registry.streams[1][2][0] && f.records[1].bytes[0] == 0xf4);
	assert(!f.held && f.table_unlocks == 2);

	/* Stream destruction is not rolled back if the final lookup is inactive. */
	for (unsigned behavior = 1; behavior <= 3; behavior++) {
		init(&f); f.behavior = behavior; f.stream.references = 1;
		assert(close_handle(&f) == -1);
		assert(!f.registry.streams[1][2][0] && f.frees == 2 && f.delays == 1);
		if (behavior == 3) assert(f.records[1].bytes[0] & 1);
		else assert(!(f.records[1].bytes[0] & 1));
	}
	/* The final read clears the current record, not a saved record image. */
	init(&f); f.behavior = 4; f.stream.references = 1;
	assert(!close_handle(&f));
	assert(f.frees == 2 && f.records[1].bytes[0] == 2);
	assert(f.records[1].bytes[4] == 7 && f.records[1].bytes[8] == 4);

	init(&f); f.records[1].bytes[4] = 8;
	assert(close_handle(&f) == -6 && (f.records[1].bytes[0] & 1) && !f.frees);
	init(&f); f.registry.present_mask = 0;
	assert(close_handle(&f) == -2 && (f.records[1].bytes[0] & 1) && !f.frees);
	init(&f); f.records[1].bytes[0] = 1;
	assert(close_handle(&f) == -6 && f.records[1].bytes[0] == 1 && !f.frees);
	init(&f); f.records[1].bytes[0] &= ~1u;
	assert(close_handle(&f) == -1 && !f.frees && f.table_unlocks == 1);
	init(&f); f.handles.record_count = 1;
	assert(close_handle(&f) == -22 && !f.held && !f.frees);
	assert(ar_tx_handle_read(NULL, 0, &output, 1, &ops, &f) == -22);
	puts("TX handle lookup, close ordering, active-bit preservation and final-read failures passed");
	return 0;
}
