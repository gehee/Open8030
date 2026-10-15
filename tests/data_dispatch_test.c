/* SPDX-License-Identifier: MIT */
#include "ar_data_dispatch.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct owned { struct ar_data_packet packet; unsigned references; uint8_t data[4]; };
struct fixture {
	struct owned original, *copies[4], *queued[4];
	unsigned allocations, queue_calls, queued_count, wakes;
	int copy_fail, queue_fail;
};
static struct owned *owned(struct ar_data_packet *packet) { return (struct owned *)packet; }
static struct ar_data_packet *copy(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context;
	unsigned index = f->allocations++;
	assert(index < 4);
	if ((int)index == f->copy_fail) { return NULL; }
	struct owned *p = calloc(1, sizeof *p);
	assert(p && packet->length == sizeof p->data);
	memcpy(p->data, packet->data, packet->length);
	p->packet = (struct ar_data_packet){p->data, packet->length};
	p->references = 1;
	f->copies[index] = p;
	return &p->packet;
}
static void retain(struct ar_data_packet *packet, void *context)
{
	(void)context;
	assert(owned(packet)->references > 0);
	owned(packet)->references++;
}
static void release(struct ar_data_packet *packet, void *context)
{
	(void)context;
	assert(owned(packet)->references > 0);
	owned(packet)->references--;
}
static int enqueue(unsigned worker, struct ar_data_dispatch_node *node,
	struct ar_data_packet *packet, uint8_t metadata, uint16_t source, void *context)
{
	struct fixture *f = context;
	assert(worker < 2 && node && metadata == 0xa5 && source == 2);
	assert(packet->length == 4 && !memcmp(packet->data, "test", 4));
	if ((int)f->queue_calls++ == f->queue_fail) { return -7; }
	assert(f->queued_count < 4);
	f->queued[f->queued_count++] = owned(packet);
	return 0;
}
static int wake(uint8_t value, void *context)
{
	struct fixture *f = context;
	assert(value == 255);
	f->wakes++;
	return -3; /* Notification failure does not undo the queue transfer. */
}
static void init(struct fixture *f)
{
	*f = (struct fixture){.copy_fail = -1, .queue_fail = -1};
	memcpy(f->original.data, "test", 4);
	f->original.packet = (struct ar_data_packet){f->original.data, 4};
	f->original.references = 1;
}
static void cleanup(struct fixture *f)
{
	for (unsigned i = 0; i < f->queued_count; ++i) { release(&f->queued[i]->packet, f); }
	assert(f->original.references == 0);
	/* The fixture owns the copy adapter's initial references. This cleanup is
	 * not a model of original downstream copying-action lifetime. */
	for (unsigned i = 0; i < f->allocations; ++i) {
		if (!f->copies[i]) { continue; }
		assert(f->copies[i]->references == 1);
		assert(!memcmp(f->copies[i]->data, "test", 4));
		release(&f->copies[i]->packet, f);
		free(f->copies[i]);
	}
}
int main(void)
{
	const struct ar_data_dispatch_ops ops = {.copy = copy, .retain = retain,
		.release = release, .enqueue = enqueue, .wake = wake};
	struct ar_data_filter_input input = {0};
	uint16_t map[] = {0, 1};
	for (unsigned scenario = 0; scenario < 4; ++scenario) {
		struct fixture f; init(&f);
		struct ar_data_dispatch_worker workers[] = {{128, 128}, {128, 128}};
		struct ar_data_dispatch_stats stats = {0}; uint32_t matches = 99;
		struct ar_data_dispatch_node nodes[3] = {0};
		for (unsigned i = 0; i < 3; ++i) { nodes[i].worker = 1; }
		nodes[0].next = &nodes[1]; nodes[1].next = &nodes[2];
		nodes[0].actions = scenario == 0 ? 8 : scenario == 1 ? 10 : 2;
		nodes[1].actions = scenario < 2 ? 8 : 4;
		if (scenario == 0) { f.queue_fail = 0; }
		if (scenario == 1) { f.copy_fail = 0; }
		if (scenario == 3) { f.queue_fail = 0; }
		assert(ar_data_dispatch(nodes, &f.original.packet, &input, 0xa5, 2,
			workers, 2, map, 2, &stats, &matches, &ops, &f) == 0);
		assert(matches == 3 && nodes[0].packets == 1 && nodes[2].packets == 1);
		assert(nodes[1].packets == (scenario >= 2));
		assert(f.queued_count == (scenario < 2 ? 1u : scenario == 2 ? 3u : 2u));
		assert(f.wakes == f.queued_count && workers[1].free == 128 - f.queued_count);
		assert(workers[1].low == workers[1].free && workers[0].free == 128);
		assert(stats.queue_failures == (scenario == 0 || scenario == 3));
		assert(stats.unmatched == 0 && stats.invalid_worker == 0);
		assert(f.original.references == 1);
		memset(f.original.data, 0, sizeof f.original.data);
		cleanup(&f);
	}
	struct fixture f; init(&f);
	struct ar_data_dispatch_node node = {.worker = 1};
	struct ar_data_dispatch_worker workers[] = {{0, 0}, {128, 128}};
	struct ar_data_dispatch_stats stats = {0}; uint32_t matches = 99;
	map[1] = UINT16_MAX;
	assert(ar_data_dispatch(&node, &f.original.packet, &input, 0xa5, 2,
		workers, 2, map, 2, &stats, &matches, &ops, &f) == -4998);
	assert(matches == 1 && stats.invalid_worker == 1 && f.original.references == 0);
	assert(node.packets == 0 && f.queue_calls == 0);
	init(&f); node.actions = 32; node.bytes = node.packets = UINT64_MAX;
	assert(ar_data_dispatch(&node, &f.original.packet, &input, 0xa5, 2,
		workers, 2, NULL, 0, &stats, &matches, &ops, &f) == 0);
	assert(workers[0].free == UINT32_MAX && workers[0].low == 0);
	assert(node.bytes == 3 && node.packets == 0); cleanup(&f);
	init(&f); node.actions = 0;
	assert(ar_data_dispatch(&node, &f.original.packet, &input, 0xa5, 2,
		workers, 2, NULL, 0, &stats, &matches, &ops, &f) == -22);
	assert(f.original.references == 0 && f.queue_calls == 0);
	init(&f); stats.unmatched = UINT64_MAX;
	assert(ar_data_dispatch(NULL, &f.original.packet, &input, 0xa5, 2,
		workers, 2, map, 2, &stats, &matches, &ops, &f) == -4996);
	assert(matches == 0 && stats.unmatched == 0 && f.original.references == 0);
	init(&f);
	assert(ar_data_dispatch(&node, &f.original.packet, &input, 0xa5, 2,
		workers, 2, map, 2, &stats, &matches, NULL, &f) == -22);
	assert(f.original.references == 1); release(&f.original.packet, &f);
	init(&f); node.filter.flags = 1;
	map[1] = 1;
	assert(ar_data_dispatch(&node, &f.original.packet, &input, 0xa5, 2,
		workers, 2, map, 2, &stats, &matches, &ops, &f) == -22);
	assert(matches == 0 && f.original.references == 0 && f.queue_calls == 0);
	return 0;
}
