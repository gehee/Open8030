/* SPDX-License-Identifier: MIT */
#include "ar_data_ingress.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	unsigned port, finds, feeds, controls;
	int missing, result;
	const uint8_t *expected;
	size_t count;
};
static void *find(unsigned type, unsigned port, void *context)
{
	struct fixture *f = context;
	assert(type == 3 && port == f->port);
	f->finds++;
	return f->missing ? NULL : f;
}
static int feed(void *parser, const uint8_t *data, size_t count, void *context)
{
	struct fixture *f = context;
	assert(parser == f && data == f->expected && count == f->count);
	assert(f->finds == 1 && f->controls == 0);
	f->feeds++;
	return f->result;
}
static int control(const uint8_t *request, size_t count, void *context)
{
	struct fixture *f = context;
	assert(request == f->expected && count == f->count);
	assert(f->finds == 0 && f->feeds == 0);
	f->controls++;
	return f->result;
}
static const struct ar_data_ingress_ops ops = {find, feed, control};

int main(void)
{
	uint8_t *q = calloc(65539, 1);
	assert(q);
	/* Exhaustive embedded length values, including all 15 wrap values, with
	 * independent wide arithmetic for expected feed/no-feed/guard outcomes. */
	for (unsigned port = 0; port < 3; ++port) {
		for (unsigned length = 0; length <= UINT16_MAX; ++length) {
			q[0] = 6; q[2] = length; q[3] = length >> 8;
			memcpy(q + 4, "\xff\xa5\xaa\x5a\xff", 5);
			q[9] = length; q[10] = length >> 8;
			struct fixture f = {.port = port, .expected = q + 4,
				.count = port ? length + 15 : length, .result = -9999};
			int result = ar_data_ingress(port, q, 65539, &ops, &f);
			assert(result == (port && length > 65520 ? -22 : 0));
			assert(f.finds == 1 && f.controls == 0);
			assert(f.feeds == (!port || length <= 4114));
		}
		/* Every other subtype belongs to the supplied common control handler. */
		for (unsigned subtype = 0; subtype < 256; ++subtype) {
			if (subtype == 6) { continue; }
			q[0] = subtype;
			struct fixture f = {.port = port, .expected = q,
				.count = 1, .result = subtype % 2 ? -4 : 23};
			assert(ar_data_ingress(port, q, 1, &ops, &f) == f.result);
			assert(f.controls == 1);
		}
	}
	/* Short allocations exercise bounds with ASan; absent interfaces must
	 * return before attempting to read the embedded header. */
	for (unsigned port = 0; port < 3; ++port) {
		for (size_t size = 1; size < 20; ++size) {
			uint8_t *short_q = calloc(size, 1); assert(short_q);
			short_q[0] = 6;
			for (size_t i = 0; i < 5 && i + 4 < size; ++i) {
				short_q[i + 4] = (const uint8_t[]){255,165,170,90,255}[i];
			}
			if (size > 2) { short_q[2] = 15; }
			for (int missing = 0; missing < 2; ++missing) {
				struct fixture f = {.port = port, .missing = missing,
					.expected = size >= 4 ? short_q + 4 : NULL, .count = 15};
				int want = size < 4 || (!missing && size < 19) ? -22 : 0;
				assert(ar_data_ingress(port, short_q, size, &ops, &f) == want);
				assert(f.finds == (size >= 4));
				assert(f.feeds == (!missing && size >= 19));
			}
			free(short_q);
		}
	}
	q[0] = 6;
	/* Each rejected prefix byte prevents a feed regardless of outer length. */
	for (unsigned port = 1; port <= 2; ++port) {
		for (unsigned i = 0; i < 5; ++i) {
			memcpy(q + 4, "\xff\xa5\xaa\x5a\xff", 5); q[4 + i] ^= 1;
			struct fixture f = {.port = port};
			assert(ar_data_ingress(port, q, 9, &ops, &f) == 0);
			assert(f.finds == 1 && f.feeds == 0);
		}
	}
	struct fixture f = {0};
	assert(ar_data_ingress(3, q, 20, &ops, &f) == -22);
	assert(ar_data_ingress(0, NULL, 20, &ops, &f) == -22);
	assert(ar_data_ingress(0, q, 0, &ops, &f) == -22);
	assert(ar_data_ingress(0, q, 20, NULL, &f) == -22);
	for (int i = 0; i < 3; ++i) {
		struct ar_data_ingress_ops bad = ops;
		if (i == 0) { bad.find = NULL; }
		if (i == 1) { bad.feed = NULL; }
		if (i == 2) { bad.control = NULL; }
		assert(ar_data_ingress(0, q, 20, &bad, &f) == -22);
	}
	assert(f.finds == 0 && f.feeds == 0 && f.controls == 0);
	free(q);
}
