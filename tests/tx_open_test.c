/* SPDX-License-Identifier: MIT */
#include "ar_tx_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_tx_stream_registry registry;
	struct ar_tx_stream *created[2];
	unsigned held, allocations, destroyed, fail, flags;
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(mutex == 7 && timeout == UINT32_MAX && !f->held);
	f->held = 1;
	f->registry.mutex = 9;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(mutex == 9 && f->held);
	f->held = 0;
}
static struct ar_tx_stream *create(uint32_t capacity, unsigned retained,
	void *context)
{
	struct fixture *f = context;
	unsigned n = f->allocations++;
	assert(f->held && n < 2);
	assert(!f->registry.streams[2][3][0] && !f->registry.streams[2][3][1]);
	assert(capacity == (n == 0 && (f->flags & 2) ? 64 : 128));
	assert(retained == (n == 0 && (f->flags & 2) ? f->flags & 32 : 0));
	if (f->fail == n + 1)
		return NULL;
	struct ar_tx_stream *s = calloc(1, sizeof *s);
	assert(s);
	s->flags = 0x80 | (retained ? 4 : 0);
	s->percentage = 100;
	f->created[n] = s;
	return s;
}
static void destroy(struct ar_tx_stream *stream, void *context)
{
	struct fixture *f = context;
	assert(f->held && stream == f->created[0] && !f->destroyed);
	assert(!f->registry.streams[2][3][0] && !f->registry.streams[2][3][1]);
	f->destroyed++;
	free(stream);
	f->created[0] = NULL;
}
static const struct ar_tx_stream_open_ops ops = {lock, unlock, create, destroy};
static void init(struct fixture *f, unsigned flags)
{
	memset(f, 0, sizeof *f);
	f->registry.mutex = 7;
	f->registry.present_mask = 1u << 2;
	f->flags = flags;
}
static int open_stream(struct fixture *f)
{
	return ar_tx_stream_open(&f->registry, 2, 3, f->flags, 64, 128, &ops, f);
}
int main(void)
{
	struct fixture f;
	for (unsigned flags = 0; flags < 256; flags++) {
		init(&f, flags);
		int result = open_stream(&f);
		assert(result == (flags & 3 ? 0 : -6));
		assert(f.allocations == (unsigned)(!!(flags & 1) + !!(flags & 2)) && !f.held);
		struct ar_tx_stream *tx = f.registry.streams[2][3][0];
		struct ar_tx_stream *rx = f.registry.streams[2][3][1];
		if (tx) assert(tx->flags == (0x80 | (flags & 32 ? 4 : 0) |
			(flags & 4 ? 1 : 0) | (flags & 16 ? 2 : 0)));
		if (rx) assert(rx->flags == 0x80);
		free(tx); free(rx);
	}
	for (unsigned fail = 1; fail <= 2; fail++) {
		init(&f, 3); f.fail = fail;
		assert(open_stream(&f) == -3 && f.allocations == fail);
		assert(f.destroyed == fail - 1 && !f.held);
		assert(!f.registry.streams[2][3][0] && !f.registry.streams[2][3][1]);
	}
	struct ar_tx_stream occupied = {0};
	for (unsigned direction = 0; direction < 2; direction++) {
		init(&f, 3); f.registry.streams[2][3][direction] = &occupied;
		assert(open_stream(&f) == -9 && !f.allocations && !f.destroyed && !f.held);
		assert(f.registry.streams[2][3][direction] == &occupied);
	}
	init(&f, 3); f.registry.present_mask = 0;
	assert(open_stream(&f) == -2 && !f.allocations && !f.held);
	init(&f, 3);
	assert(ar_tx_stream_open(&f.registry, 8, 3, 3, 64, 128, &ops, &f) == -6);
	assert(ar_tx_stream_open(&f.registry, 2, 3, 3, 0, 128, &ops, &f) == -6);
	assert(ar_tx_stream_open(&f.registry, 2, 3, 3, 64, 0, &ops, &f) == -6);
	assert(!f.allocations && !f.held && f.registry.mutex == 7);
	/* These native guards are additional checks, not original error parity. */
	assert(ar_tx_stream_open(&f.registry, 2, 5, 3, 64, 128, &ops, &f) == -22);
	assert(ar_tx_stream_open(NULL, 2, 3, 3, 64, 128, &ops, &f) == -22);
	assert(ar_tx_stream_open(&f.registry, 2, 3, 3, 64, 128, NULL, &f) == -22);
	puts("tx_open_test: passed");
	return 0;
}
