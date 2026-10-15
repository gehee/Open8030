/* SPDX-License-Identifier: MIT */
#include "data_mode3_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	void *allocations[16]; unsigned allocated, attempts, identities, frames;
	int fail, framed, bad_id;
};
static int identity(const struct ar_data_identity *key, uint16_t *id, void *context)
{
	struct fixture *f = context; f->identities++;
	const struct ar_data_identity registry[] = {{0, 2, 0}, {0, 1, 0}, {3, 0, 0}, {2, 0, 0}, {2, 1, 0}};
	for (unsigned i = 0; i < 5; ++i) {
		if (key->type == registry[i].type && key->port == registry[i].port &&
			key->selector == registry[i].selector) { *id = f->bad_id ? 64 : (uint16_t)i; return 0; }
	}
	return -9997;
}
static int framed(uint16_t id, uint8_t *value, void *context)
{
	struct fixture *f = context; assert(id < 5); f->frames++; *value = (uint8_t)f->framed; return 0;
}
static void *allocate(struct fixture *f, size_t size)
{
	if ((int)f->attempts++ == f->fail) { return NULL; }
	assert(f->allocated < 16); void *p = malloc(size); assert(p);
	memset(p, 0xa5, size); f->allocations[f->allocated++] = p; return p;
}
static struct ar_data_built_route *node(void *context) { return allocate(context, sizeof(struct ar_data_built_route)); }
static struct ar_data_worker_action *action(void *context) { return allocate(context, sizeof(struct ar_data_worker_action)); }
static const struct ar_data_builder_ops ops = {{identity, framed}, node, action};
static void cleanup(struct fixture *f)
{
	/* Adapter-owned storage includes unpublished nodes on partial failure. */
	for (unsigned i = 0; i < f->allocated; ++i) { free(f->allocations[i]); }
}
static unsigned nodes(struct ar_data_built_route *head)
{
	unsigned n = 0; for (; head; head = (struct ar_data_built_route *)head->dispatch.next) { assert(++n < 8); } return n;
}
int main(void)
{
	for (uint8_t role = 0; role < 3; ++role) {
		struct fixture f = {.fail = -1, .framed = 1}; struct ar_data_built_route *head = NULL;
		assert(ar_data_routes_build(&head, mode3_routes, 6, role, &ops, &f) == 0);
		assert(nodes(head) == (role < 2 ? 4u : 2u));
		assert(head->dispatch.filter.source == 3 && head->action->route.destinations == 2);
		assert(head->dispatch.worker == 1 && head->dispatch.actions == 8);
		struct ar_data_built_route *second = (struct ar_data_built_route *)head->dispatch.next;
		assert(second->dispatch.filter.source == 1 && second->action->route.destinations == 8);
		if (role < 2) {
			struct ar_data_built_route *third = (struct ar_data_built_route *)second->dispatch.next;
			assert(third->dispatch.filter.source == (role ? 4 : 0));
			assert(third->action->route.destinations == (role ? 4u : 16u));
		}
		cleanup(&f);
	}
	const uint8_t first[] = {0xaa}, different[] = {0xbb};
	struct ar_data_route_spec records[2] = {
		{.flags = 17, .source = {3, 0, 0}, .destination = {2, 1, 0},
		 .pattern_length = 1, .pattern = first, .actions = 8},
		{.flags = 17, .source = {3, 0, 0}, .destination = {2, 0, 0},
		 .pattern_length = 1, .pattern = different, .actions = 8, .worker = 257, .metadata_override = 0x55},
	};
	struct fixture f = {.fail = -1, .framed = 1}; struct ar_data_built_route *head = NULL;
	assert(ar_data_routes_build(&head, records, 1, 0, &ops, &f) == 0);
	head->dispatch.bytes = 7; head->action->route.bytes = 9;
	assert(ar_data_routes_build(&head, records + 1, 1, 0, &ops, &f) == 0);
	assert(nodes(head) == 1 && f.allocated == 2 && f.identities == 4);
	assert(head->pattern[0] == 0xaa && head->dispatch.filter.pattern == head->pattern);
	assert(head->action->route.destinations == 24 && head->action->metadata_override == 0x55);
	assert(head->dispatch.worker == 1 && head->dispatch.bytes == 7 && head->action->route.bytes == 9);
	cleanup(&f);

	/* Metadata-range merging follows the newly resolved source's framing field. */
	for (unsigned is_framed = 0; is_framed < 2; ++is_framed) {
		f = (struct fixture){.fail = -1, .framed = (int)is_framed}; head = NULL;
		records[0].flags = records[1].flags = 2;
		records[1].source = (struct ar_data_identity){0, 1, 0}; records[1].minimum = 12;
		assert(ar_data_routes_build(&head, records, 2, 0, &ops, &f) == 0);
		assert(nodes(head) == 1 + is_framed && f.frames == 1);
		cleanup(&f);
	}
	/* Earlier nodes remain published; a failed action allocation retains its
	 * unlinked node in the allocator adapter, never in the published list. */
	records[0] = mode3_routes[4]; records[1] = mode3_routes[5];
	for (int fail = 0; fail < 4; ++fail) {
		f = (struct fixture){.fail = fail}; head = NULL;
		assert(ar_data_routes_build(&head, records, 2, 0, &ops, &f) == -9999);
		assert(nodes(head) == (fail >= 2) && f.allocated == (unsigned)fail);
		if (fail & 1) {
			struct ar_data_built_route *orphan = f.allocations[f.allocated - 1];
			assert(!orphan->action && !orphan->dispatch.next && orphan != head);
		}
		cleanup(&f);
	}
	f = (struct fixture){.fail = -1}; head = NULL;
	struct ar_data_route_spec spec = mode3_routes[0]; spec.disabled = 255;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == 0);
	spec.disabled = 0; spec.role = 256;
	assert(ar_data_routes_build(&head, &spec, 1, 0, &ops, &f) == 0);
	assert(!head && !f.identities && !f.attempts);
	spec.role = 1; spec.actions = 40;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == -10000);
	spec.actions = 8; spec.source.type = 99;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == -9997);
	spec.source.type = 3; f.bad_id = 1;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == -10000);
	assert(!head && !f.attempts); f.bad_id = 0;

	uint8_t pattern[39] = {0}; spec.flags = 16; spec.pattern = pattern; spec.pattern_length = 38; spec.offset = 4076;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == 0);
	assert(head->dispatch.filter.pattern_length == 38 && !memcmp(head->pattern, pattern, 38));
	unsigned attempts = f.attempts; spec.pattern_length = 39; spec.offset = 0;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == -22);
	spec.pattern_length = 1; spec.pattern = NULL;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == -22);
	spec.pattern = pattern; spec.offset = 4114;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &ops, &f) == -10000);
	assert(f.attempts == attempts && nodes(head) == 1);
	assert(ar_data_routes_build(&head, &spec, (size_t)UINT16_MAX + 1, 1, &ops, &f) == -22);
	assert(ar_data_routes_build(&head, NULL, 0, 1, &ops, &f) == 0);
	struct ar_data_builder_ops missing = ops; missing.action = NULL;
	assert(ar_data_routes_build(&head, &spec, 1, 1, &missing, &f) == -22);
	cleanup(&f);
	return 0;
}
