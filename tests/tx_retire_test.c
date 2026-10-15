/* SPDX-License-Identifier: MIT */
#include "ar_tx_retire.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_retire_ring ring;
	uint8_t packets[10][1028];
	unsigned calls, mutate;
	unsigned selectors[256], streams[256], tails[256], depths[256];
	uint32_t lengths[256];
};
static void word(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		p[i] = value >> (i * 8);
}
static void release(unsigned selector, unsigned stream, unsigned direction,
	uint32_t length, void *context)
{
	struct fixture *f = context;
	assert(!direction && f->calls < 256);
	unsigned i = f->calls++;
	f->selectors[i] = selector;
	f->streams[i] = stream;
	f->lengths[i] = length;
	f->tails[i] = f->ring.tail;
	f->depths[i] = f->ring.owned;
	if (!i && f->mutate) {
		f->ring.configuration ^= 0x0f000000;
		uint8_t *data = f->packets[9];
		data[7] = f->mutate == 1 ? 3 : 1;
		word(data + 12, 0xabcdefcd);
		/* The selected packet stays held even if its ring view changes. */
		f->ring.packets[9] = f->ring.packets[8];
	}
}
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->ring.configuration = 0xa5123456;
	f->ring.tail = 9;
	f->ring.head = 2;
	f->ring.owned = 3;
	for (unsigned i = 0; i < 10; ++i)
		f->ring.packets[i] = (struct ar_tx_retire_packet){f->packets[i], 1028};
	f->packets[9][7] = 2;
	word(f->packets[9] + 8, 0xdeadbeef);
	word(f->packets[9] + 12, 0x12345f7e);
	f->packets[0][7] = 1;
	word(f->packets[0] + 8, 0x00000f01);
}
int main(void)
{
	struct fixture f;
	init(&f);
	uint8_t saved[sizeof f.packets];
	memcpy(saved, f.packets, sizeof saved);
	assert(!ar_tx_retire(&f.ring, 1, release, &f));
	assert(f.calls == 3 && f.ring.tail == 1 && f.ring.owned == 1 && f.ring.head == 2);
	assert(f.tails[0] == 9 && f.tails[1] == 9 && f.tails[2] == 0);
	assert(f.depths[0] == 3 && f.depths[1] == 3 && f.depths[2] == 2);
	assert(f.selectors[0] == 5 && f.selectors[2] == 5);
	assert(f.streams[0] == 0xef && f.streams[1] == 0x7e && f.streams[2] == 1);
	assert(f.lengths[0] == 0xdeadb && f.lengths[1] == 0x12345 && !f.lengths[2]);
	assert(!memcmp(saved, f.packets, sizeof saved));
	assert(!ar_tx_retire(&f.ring, 0, release, &f)); /* Zero-descriptor packet. */
	assert(f.calls == 3 && f.ring.tail == 2 && !f.ring.owned);
	assert(!ar_tx_retire(&f.ring, 0, release, &f));
	assert(f.calls == 3);

	for (unsigned mutation = 1; mutation <= 2; ++mutation) {
		init(&f);
		f.mutate = mutation;
		assert(!ar_tx_retire(&f.ring, 2, release, &f));
		assert(f.calls == (mutation == 1 ? 3u : 1u));
		assert(f.ring.tail == 0 && f.ring.owned == 2);
		if (mutation == 1) {
			assert(f.selectors[1] == 10 && f.streams[1] == 0xcd && f.lengths[1] == 0xabcde);
			assert(f.selectors[2] == 10);
		}
	}
	init(&f);
	f.packets[9][7] = 255;
	f.ring.owned = 1;
	assert(!ar_tx_retire(&f.ring, 0, release, &f));
	assert(f.calls == 255 && !f.ring.owned && !f.ring.tail);
	init(&f);
	assert(!ar_tx_retire(&f.ring, UINT_MAX, release, &f));
	assert(!f.calls && f.ring.owned == 3 && f.ring.tail == 9);
	assert(!ar_tx_retire(NULL, 0, NULL, NULL));

	/* A malformed packet must not release its slot or touch missing bytes.
	 * Earlier descriptor releases cannot be undone: errors are not atomic. */
	init(&f);
	f.ring.packets[9].length = 12;
	assert(ar_tx_retire(&f.ring, 0, release, &f) == -22);
	assert(f.calls == 1 && f.ring.owned == 3 && f.ring.tail == 9);
	init(&f);
	f.ring.packets[9].length = 7;
	assert(ar_tx_retire(&f.ring, 0, release, &f) == -22 && !f.calls);
	init(&f);
	f.ring.packets[9].data = NULL;
	assert(ar_tx_retire(&f.ring, 0, release, &f) == -22 && !f.calls);
	init(&f);
	f.ring.tail = 10;
	assert(ar_tx_retire(&f.ring, 0, release, &f) == -22 && !f.calls);
	init(&f);
	f.ring.owned = 11;
	assert(ar_tx_retire(&f.ring, 0, release, &f) == -22 && !f.calls);
	init(&f);
	assert(ar_tx_retire(&f.ring, 0, NULL, &f) == -22 && !f.calls);
	puts("TX descriptor retirement ordering, ring wrap, mutable packet view and partial failure passed");
}
