/* SPDX-License-Identifier: MIT */
#include "ar_data_filter.h"
#include <assert.h>
#include <stddef.h>

struct fixture { unsigned identities, framed_calls; int missing, id_missing; uint8_t enabled; };
static int identity(const struct ar_data_identity *key, uint16_t *id, void *context)
{
	struct fixture *f = context;
	f->identities++;
	if (f->missing) { return -1; }
	*id = key->port;
	return 0;
}
static int framed(uint16_t id, uint8_t *enabled, void *context)
{
	struct fixture *f = context;
	assert(id == 2);
	f->framed_calls++;
	if (f->id_missing) { return -1; }
	*enabled = f->enabled;
	return 0;
}
int main(void)
{
	const struct ar_data_filter_ops ops = {identity, framed};
	struct ar_data_filter filter = {.flags = 7, .source = 2, .secondary = 4,
		.minimum = 3, .maximum = 200};
	struct ar_data_filter_input input = {.source = {3, 2, 0}, .secondary = {2, 4, 0}};
	for (unsigned enabled = 0; enabled < 2; ++enabled) {
		for (unsigned metadata = 0; metadata < 256; ++metadata) {
			struct fixture f = {.enabled = enabled ? 255 : 0};
			input.metadata = (uint8_t)metadata;
			int expected = !enabled || (metadata >= 3 && metadata <= 200);
			assert(ar_data_filter_match(&filter, &input, &ops, &f) == expected);
			/* Source ID and metadata repeat the identity lookup. The second interface follows
			 * only after metadata acceptance. No callback result is cached. */
			assert(f.identities == 2u + (unsigned)expected && f.framed_calls == 1);
		}
	}
	struct fixture f = {.missing = 1};
	assert(ar_data_filter_match(&filter, &input, &ops, &f) == 0);
	assert(f.identities == 1 && f.framed_calls == 0);
	f = (struct fixture){.id_missing = 1};
	assert(ar_data_filter_match(&filter, &input, &ops, &f) == 0);
	assert(f.identities == 2 && f.framed_calls == 1);
	filter.source = 9; f = (struct fixture){0};
	assert(ar_data_filter_match(&filter, &input, &ops, &f) == 0);
	assert(f.identities == 1 && f.framed_calls == 0);
	filter.flags = 2; filter.minimum = 200; filter.maximum = 3;
	input.metadata = 100; f = (struct fixture){.enabled = 1};
	assert(ar_data_filter_match(&filter, &input, &ops, &f) == 0);
	f.enabled = 0;
	assert(ar_data_filter_match(&filter, &input, &ops, &f) == 1);

	/* Use exact-size storage so a missing offset guard is detected by ASan. */
	const uint8_t data[] = {0x11, 0x22, 0x33, 0x44}, pattern[] = {0x33, 0x44};
	filter = (struct ar_data_filter){.flags = 16, .offset = 2,
		.pattern = pattern, .pattern_length = sizeof pattern};
	input.data = data; input.length = sizeof data;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == 1);
	filter.offset = 1;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == 0);
	filter.offset = 3;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == -22);
	filter.offset = UINT16_MAX;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == -22);
	input.length = 1; input.data = NULL; filter.pattern = NULL;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == 0);
	input.length = 4;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == -22);
	filter.pattern_length = 0;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == 1);
	filter.flags = UINT64_MAX & ~UINT64_C(23);
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == 1);
	filter.flags = 1;
	assert(ar_data_filter_match(&filter, &input, NULL, NULL) == -22);
	struct ar_data_filter_ops incomplete = {identity, NULL};
	filter.flags = 2;
	assert(ar_data_filter_match(&filter, &input, &incomplete, NULL) == -22);
	assert(ar_data_filter_match(NULL, &input, &ops, &f) == -22);
	assert(ar_data_filter_match(&filter, NULL, &ops, &f) == -22);
	return 0;
}
