/* SPDX-License-Identifier: MIT */
#include "ar_tx_irq.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_irq irq;
	struct ar_interleaver state;
	unsigned first, second, mode, bank, reads, posts, notices, mutate;
	unsigned order[16], count;
	uint8_t payload[12];
};
static void effect(struct fixture *f, unsigned kind)
{
	assert(f->count < 16);
	f->order[f->count++] = kind;
}
static uint64_t clock_read(void *context)
{
	struct fixture *f = context;
	assert(f->irq.busy == 1);
	effect(f, 1);
	return UINT64_C(0x7654321089abcdef);
}
static uint32_t word(unsigned offset, void *context)
{
	struct fixture *f = context;
	effect(f, offset);
	if (offset == 0xf70) {
		if (!f->reads++) {
			assert(f->irq.timestamp == 42);
			return f->first << 12;
		}
		return f->second << 12;
	}
	assert(f->irq.timestamp == UINT64_C(0x7654321089abcdef));
	assert(f->irq.user == f->first);
	assert(offset == 0xf5c || offset == 0xf58);
	return offset == 0xf5c ? 0xabcd1234 : 0xb0 | f->bank;
}
static uint32_t read_field(uint32_t field, void *context)
{
	struct fixture *f = context;
	assert(field == ((f->second + 0x2c0) << 18 | 0x401));
	effect(f, 2);
	if (f->mutate)
		f->state.users[f->second].retries = 0; /* Reload, then byte wrap. */
	return f->mode;
}
static int post(unsigned message, const uint8_t *payload, unsigned length,
	void *context)
{
	struct fixture *f = context;
	effect(f, message);
	f->posts++;
	assert(length == (message == 0x301 ? 12u : 4u));
	memcpy(f->payload, payload, length);
	if (f->mutate) {
		f->state.users[f->second].retries = 2;
		f->irq.busy = 3;
	}
	return -95; /* Both paths must continue despite failed queue delivery. */
}
static void notify(unsigned source, const uint8_t *payload, void *context)
{
	struct fixture *f = context;
	effect(f, source);
	f->notices++;
	if (source == 19)
		assert(payload && !memcmp(payload, f->payload, 12));
	else
		assert(source == 11 && !payload);
}
static const struct ar_tx_irq_ops ops = {clock_read, word, read_field, post, notify};
static struct fixture setup(void)
{
	struct fixture f = {.irq = {.timestamp = 42, .busy = 2, .user = 9},
		.first = 8, .second = 5, .mode = 1, .bank = 1};
	f.state.users[5].active = 1;
	f.state.users[5].retries = 17;
	return f;
}
int main(void)
{
	struct fixture f = setup();
	assert(!ar_tx_irq_preencode(&f.irq, &f.state, &ops, &f));
	assert(f.irq.user == 8 && f.irq.busy == 1);
	assert(f.state.users[5].history == 2 && !f.state.users[8].history);
	const unsigned expected[] = {1, 0xf70, 0xf5c, 0xf58, 0x301,
		0xf70, 2, 0xf58, 19};
	assert(f.count == sizeof expected / sizeof *expected);
	assert(!memcmp(f.order, expected, sizeof expected));
	assert(!memcmp(f.payload, (uint8_t[]){8,0,0,0,0xcd,0xab,0,0,11,0,0,0}, 12));
	uint32_t history = f.state.users[5].history;
	f.count = 0;
	assert(!ar_tx_irq_end(&f.irq, &ops, &f));
	assert(!f.irq.busy && f.payload[0] == 8);
	assert(f.count == 2 && f.order[0] == 0x309 && f.order[1] == 11);
	assert(f.state.users[5].history == history); /* End adds no history. */
	assert(!ar_tx_irq_end(&f.irq, &ops, &f)); /* Duplicate still posts. */
	assert(f.posts == 3 && f.notices == 3);
	f.irq.busy = 2;
	f.irq.user = 255;
	assert(!ar_tx_irq_end(&f.irq, &ops, &f));
	assert(f.irq.busy == 2 && f.payload[0] == 255);

	f = setup();
	f.mutate = 1;
	assert(!ar_tx_irq_preencode(&f.irq, &f.state, &ops, &f));
	assert(f.irq.busy == 3 && f.state.users[5].history == 0x80000000);
	f = setup();
	f.mode = 2; /* Only mode exactly 1 uses paired bank bits. */
	f.state.users[5].retries = 33;
	assert(!ar_tx_irq_preencode(&f.irq, &f.state, &ops, &f));
	assert(f.state.users[5].history == 1);

	for (unsigned user = 10; user <= 15; ++user) {
		f = setup();
		f.second = user;
		struct ar_interleaver saved = f.state;
		assert(ar_tx_irq_preencode(&f.irq, &f.state, &ops, &f) == -22);
		assert(f.posts == 1 && f.notices == 1 && !memcmp(&saved, &f.state, sizeof saved));
	}
	f = setup();
	struct fixture original = f;
	struct ar_tx_irq_ops invalid = ops;
	invalid.read = NULL;
	assert(ar_tx_irq_preencode(&f.irq, &f.state, &invalid, &f) == -22);
	assert(ar_tx_irq_preencode(NULL, &f.state, &ops, &f) == -22);
	assert(ar_tx_irq_end(&f.irq, NULL, &f) == -22);
	assert(!memcmp(&f, &original, sizeof f));
	puts("TX pre-encode history/order, mutation/wrap, duplicate end and queue-failure paths passed");
}
