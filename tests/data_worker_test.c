/* SPDX-License-Identifier: MIT */
#include "ar_data_worker.h"
#include <assert.h>
#include <string.h>

struct owned {
	struct ar_data_packet packet;
	struct ar_data_action_buffer buffer;
	unsigned references;
	uint8_t storage[64];
};
struct fixture {
	struct owned pool[4];
	struct ar_data_worker_action actions[3];
	struct ar_data_dispatch_node nodes[3];
	struct ar_data_worker_message messages[4];
	struct ar_data_dispatch_worker queue;
	struct ar_data_worker worker;
	struct ar_data_destination destination;
	struct ar_data_action_profile profile;
	unsigned queued, read, locks, unlocks, releases, recycled, writes, allocations;
	int locked, fail_enqueue;
	uint8_t output[64], metadata;
	uint32_t length;
	uint32_t ticks;
	unsigned clock_reads;
};
static struct owned *owned(struct ar_data_packet *packet) { return (struct owned *)packet; }
static struct owned *allocate(struct fixture *f)
{
	for (unsigned i = 0; i < 4; ++i) {
		struct owned *p = &f->pool[i];
		if (p->references) { continue; }
		memset(p, 0, sizeof *p); p->references = 1;
		p->buffer = (struct ar_data_action_buffer){p->storage, sizeof p->storage, 18, 24};
		memcpy(p->storage + 18, "\xc8\4\1\2\3\0", 6);
		p->packet = (struct ar_data_packet){p->storage + 18, 6};
		f->allocations++; return p;
	}
	return NULL;
}
static struct ar_data_packet *copy(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context; struct owned *src = owned(packet), *p = allocate(f);
	if (!p) { return NULL; }
	memcpy(p->storage, src->storage, src->buffer.tail);
	p->buffer.head = src->buffer.head; p->buffer.tail = src->buffer.tail;
	p->packet = (struct ar_data_packet){p->storage + p->buffer.head, packet->length};
	return &p->packet;
}
static void retain(struct ar_data_packet *packet, void *context)
{
	(void)context; assert(owned(packet)->references); owned(packet)->references++;
}
static void release(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context; assert(!f->locked && owned(packet)->references);
	f->releases++; if (!--owned(packet)->references) { f->recycled++; }
}
static int enqueue(unsigned worker, struct ar_data_dispatch_node *node,
	struct ar_data_packet *packet, uint8_t metadata, uint16_t source, void *context)
{
	struct fixture *f = context; assert(worker == 0 && f->queued < 4);
	if (f->fail_enqueue) { return -7; }
	unsigned index = (unsigned)(node - f->nodes); assert(index < 3);
	f->messages[f->queued++] = (struct ar_data_worker_message){packet,
		&f->actions[index], metadata, source, f->ticks};
	return 0;
}
static int wake(uint8_t value, void *context)
{
	(void)context; assert(value == 255); return -3;
}
static void lock(void *context)
{
	struct fixture *f = context; assert(!f->locked); f->locked = 1; f->locks++;
}
static void unlock(void *context)
{
	struct fixture *f = context; assert(f->locked); f->locked = 0; f->unlocks++;
}
static int receive(struct ar_data_worker_message *message, void *context)
{
	struct fixture *f = context; assert(f->locked);
	if (f->read == f->queued) { return -9; }
	*message = f->messages[f->read++]; return 0;
}
static struct ar_data_action_buffer *buffer(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context; assert(!f->locked); return &owned(packet)->buffer;
}
static const struct ar_data_action_profile *source(uint16_t id, void *context)
{
	struct fixture *f = context; assert(!f->locked && id == 2); return &f->profile;
}
static struct ar_data_destination *find(unsigned id, void *context)
{
	struct fixture *f = context; assert(!f->locked && id == 3); return &f->destination;
}
static int write_packet(struct ar_data_destination *destination, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source_id, void *context)
{
	struct fixture *f = context; assert(!f->locked && destination == &f->destination);
	assert(source_id == 2 && length <= sizeof f->output);
	memcpy(f->output, data, length); f->length = length; f->metadata = metadata;
	f->writes++; return (int)length;
}
static int destination_enqueue(struct ar_data_destination *d, struct ar_data_packet *p,
	struct ar_data_route *r, uint8_t m, uint16_t s, void *context)
{
	(void)d; (void)p; (void)r; (void)m; (void)s; (void)context;
	assert(0 && "unexpected destination queue"); return -1;
}
static void diagnose(struct ar_data_destination *d, void *context)
{
	(void)d; (void)context; assert(0 && "unexpected diagnostic");
}
static uint32_t ticks(void *context)
{
	struct fixture *f = context; assert(!f->locked); f->clock_reads++; return f->ticks;
}
static const struct ar_data_worker_ops ops = {lock, receive, unlock, buffer,
	{source}, {find, retain, destination_enqueue, release, diagnose}, ticks};
static void init(struct fixture *f)
{
	static const uint8_t prefix = 0xc8;
	*f = (struct fixture){.queue = {128, 128}, .profile = {1, 2, 1, 1, &prefix}};
	f->worker.queue = &f->queue;
	f->destination.enabled = 1; f->destination.write = write_packet;
}
static void submit(struct fixture *f, const uint64_t *flags, unsigned count)
{
	assert(count <= 3); struct owned *p = allocate(f); assert(p);
	f->queued = f->read = 0;
	for (unsigned i = 0; i < count; ++i) {
		f->nodes[i] = (struct ar_data_dispatch_node){.actions = flags[i],
			.next = i + 1 < count ? &f->nodes[i + 1] : NULL};
		f->actions[i] = (struct ar_data_worker_action){flags[i], 0x56, {1u << 3, 0, 0}};
	}
	const struct ar_data_dispatch_ops dispatch = {.copy = copy, .retain = retain,
		.release = release, .enqueue = enqueue, .wake = wake};
	struct ar_data_filter_input input = {0}; struct ar_data_dispatch_stats stats = {0};
	uint32_t matches = 0; uint16_t map = 0;
	assert(ar_data_dispatch(f->nodes, &p->packet, &input, 0xa5, 2, &f->queue, 1,
		&map, 1, &stats, &matches, &dispatch, f) == 0 && matches == count);
}
int main(void)
{
	struct fixture f;
	/* Sharing a packet across actions makes later workers observe earlier trims. */
	init(&f); const uint64_t shared[] = {1, 8}; submit(&f, shared, 2);
	assert(f.pool[0].references == 2 && f.queue.free == 126);
	assert(ar_data_worker_step(&f.worker, &ops, &f) == 0 && !f.writes);
	assert(ar_data_worker_step(&f.worker, &ops, &f) == 0);
	assert(f.writes == 1 && f.length == 3 && !memcmp(f.output, "\1\2\3", 3));
	assert(f.worker.bytes == 9 && f.worker.packets == 2 && f.queue.free == 128);
	assert(f.pool[0].references == 0 && f.recycled == 1 && f.locks == f.unlocks);
	assert(ar_data_worker_step(&f.worker, &ops, &f) == -9995);
	assert(f.queue.free == 128 && f.worker.packets == 2 && f.releases == 3);

	/* A copied action retains the initial allocation reference after delivery.
	 * Repeating it consumes all three copy slots; the input slot still recycles.
	 * This pool is a test adapter, not a production allocator/lifetime policy. */
	for (unsigned fail = 0; fail < 2; ++fail) {
		init(&f); f.fail_enqueue = (int)fail; const uint64_t copied[] = {0x10c};
		for (unsigned i = 0; i < 6; ++i) {
			submit(&f, copied, 1);
			while (f.read < f.queued) { assert(ar_data_worker_step(&f.worker, &ops, &f) == 0); }
			assert(f.pool[0].references == 0 && f.queue.free == 128);
		}
		assert(f.allocations == 9 && f.recycled == 6 && f.writes == (fail ? 0u : 3u));
		for (unsigned i = 1; i < 4; ++i) { assert(f.pool[i].references == 1); }
		if (!fail) {
			assert(f.length == 18 && f.metadata == 0x56);
			assert(f.output[5] == 3 && f.output[7] == 0x56 && f.output[13] == 6);
			assert(!memcmp(f.output + 15, "\1\2\3", 3));
		}
		/* Explicit fixture cleanup is not attributed to the original worker. */
		for (unsigned i = 1; i < 4; ++i) { release(&f.pool[i].packet, &f); }
		assert(f.recycled == 9);
	}

	/* Every consumed malformed message releases its owned reference after unlock. */
	for (unsigned guard = 0; guard < 5; ++guard) {
		init(&f); const uint64_t action[] = {8}; submit(&f, action, 1);
		struct owned *p = &f.pool[0];
		if (guard == 0) { f.messages[0].action = NULL; }
		if (guard == 1) { p->buffer.tail++; }
		if (guard == 2) { p->buffer.capacity = 2; }
		if (guard == 3) {
			p->buffer.head = 0; p->buffer.tail = 6; p->packet.data = p->storage;
			f.actions[0].flags = 0x10a;
		}
		if (guard == 4) { p->buffer.storage = NULL; }
		assert(ar_data_worker_step(&f.worker, &ops, &f) == (guard == 0 ? -4999 : -22));
		assert(!p->references && !f.writes && f.queue.free == 128 && !f.locked);
	}
	/* Missing adapters do not receive a queued reference. */
	init(&f); const uint64_t action[] = {8}; submit(&f, action, 1);
	struct ar_data_worker_ops missing = ops; missing.delivery.release = NULL;
	assert(ar_data_worker_step(&f.worker, &missing, &f) == -22);
	assert(!f.read && !f.locks && f.pool[0].references == 1);
	f.worker.bytes = f.worker.packets = UINT64_MAX; f.queue.free = UINT32_MAX;
	assert(ar_data_worker_step(&f.worker, &ops, &f) == 0);
	assert(f.worker.bytes == 5 && f.worker.packets == 0 && f.queue.free == 0);
	assert(!f.pool[0].references && !f.locked);

	/* Queue age is modular and strictly greater than 50. A future timestamp
	 * also yields a large unsigned delta; no signed-age special case exists. */
	const uint32_t ages[][3] = {
		{50, 0, 0}, {51, 0, 1}, {0, UINT32_MAX, 0},
		{0, UINT32_MAX - 49, 0}, {0, UINT32_MAX - 50, 1}, {0, 1, 1},
	};
	for (unsigned i = 0; i < sizeof ages / sizeof ages[0]; ++i) {
		init(&f); f.ticks = ages[i][1]; submit(&f, action, 1);
		f.ticks = ages[i][0];
		assert(ar_data_worker_step(&f.worker, &ops, &f) == 0);
		assert(f.worker.aged == ages[i][2] && f.clock_reads == 1);
		assert(ar_data_worker_step(&f.worker, &ops, &f) == -9995);
		assert(f.clock_reads == 1 && f.worker.packets == 1);
	}
	/* Discard precedes action validation, but a missing packet is still an
	 * error. All successful receives count and sample age before these checks. */
	for (unsigned have_packet = 0; have_packet < 2; ++have_packet) {
		for (unsigned have_action = 0; have_action < 2; ++have_action) {
			for (unsigned discard = 0; discard < 2; ++discard) {
				init(&f); f.ticks = 51; f.worker.discard = discard ? 255 : 0;
				struct owned *p = have_packet ? allocate(&f) : NULL;
				f.actions[0] = (struct ar_data_worker_action){8, 0, {1u << 3, 0, 0}};
				f.messages[0] = (struct ar_data_worker_message){p ? &p->packet : NULL,
					have_action ? &f.actions[0] : NULL, 0xa5, 2, 0};
				f.queued = 1; f.queue.free = 127;
				f.worker.aged = f.worker.missing_packets = f.worker.missing_actions = UINT64_MAX;
				int expected = !have_packet ? -9998 : !discard && !have_action ? -4999 : 0;
				assert(ar_data_worker_step(&f.worker, &ops, &f) == expected);
				assert(f.worker.packets == 1 && f.worker.bytes == (have_packet ? 6u : 0u));
				assert(f.worker.aged == 0 && f.clock_reads == 1 && f.queue.free == 128);
				assert(f.worker.missing_packets == (have_packet ? UINT64_MAX : 0));
				assert(f.worker.missing_actions == (have_packet && !discard && !have_action ? 0 : UINT64_MAX));
				assert(f.writes == (have_packet && have_action && !discard));
				assert(f.releases == have_packet && !f.locked);
			}
		}
	}
	/* Discard never accesses the mutable packet buffer. It still owns and
	 * releases the message reference even when that adapter view is invalid. */
	init(&f); submit(&f, action, 1); f.pool[0].buffer.storage = NULL;
	f.worker.discard = 1;
	assert(ar_data_worker_step(&f.worker, &ops, &f) == 0);
	assert(!f.pool[0].references && !f.writes && f.worker.bytes == 6);
	init(&f); submit(&f, action, 1); missing = ops; missing.ticks = NULL;
	assert(ar_data_worker_step(&f.worker, &missing, &f) == -22);
	assert(!f.read && !f.locks && f.pool[0].references == 1);
	assert(ar_data_worker_step(&f.worker, &ops, &f) == 0);
	return 0;
}
