/* SPDX-License-Identifier: MIT */
#include "ar_data_route.h"
#include "ar_data_event.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

struct fixture {
	struct ar_data_packet packet;
	struct ar_data_route route;
	struct ar_data_destination destination[2];
	struct ar_data_message messages[2];
	struct ar_data_writer writer[2];
	unsigned head, count, references, locked, calls;
	int result, mutate, invalidate, allocation_fails;
	char trace[64]; size_t trace_used;
	struct ar_data_event event;
	uint8_t cache[1024], emitted[1024];
};
static void mark(struct fixture *f, char c)
{
	assert(f->trace_used + 1 < sizeof f->trace);
	f->trace[f->trace_used++] = c; f->trace[f->trace_used] = 0;
}
static void lock(void *arg)
{
	struct fixture *f = arg; assert(!f->locked); f->locked = 1; mark(f, 'L');
}
static void unlock(void *arg)
{
	struct fixture *f = arg; assert(f->locked); f->locked = 0; mark(f, 'U');
}
static int receive(struct ar_data_message *message, void *arg)
{
	struct fixture *f = arg; assert(f->locked); mark(f, 'Q');
	if (!f->count) { return -1; }
	*message = f->messages[f->head++]; --f->count;
	if (f->invalidate) { f->destination[0].write = NULL; }
	return 0;
}
static void release(struct ar_data_packet *packet, void *arg)
{
	struct fixture *f = arg; assert(packet == &f->packet && f->references);
	--f->references; mark(f, 'R');
}
static void diagnose(struct ar_data_destination *destination, void *arg)
{
	struct fixture *f = arg; assert(destination && f->locked); mark(f, 'D');
}
static int write_packet(struct ar_data_destination *destination, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source, void *arg)
{
	struct fixture *f = arg;
	assert(destination && f->locked && metadata == 255 && source == 65535);
	assert(data == f->packet.data && length == f->packet.length);
	mark(f, 'W'); ++f->calls;
	if (f->mutate) { f->packet.length = 2; }
	return f->result == INT_MAX ? (int)length : f->result;
}
static struct ar_data_destination *find(unsigned id, void *arg)
{
	struct fixture *f = arg; assert(id < 2); return &f->destination[id];
}
static void retain(struct ar_data_packet *packet, void *arg)
{
	struct fixture *f = arg; assert(packet == &f->packet); ++f->references;
}
static int enqueue(struct ar_data_destination *destination, struct ar_data_packet *packet,
	struct ar_data_route *route, uint8_t metadata, uint16_t source, void *arg)
{
	struct fixture *f = arg; assert(destination && f->count < 2);
	f->messages[f->count++] = (struct ar_data_message){packet, route, metadata, source};
	return 0;
}
static uint8_t *allocate(size_t bytes, void *arg)
{
	struct fixture *f = arg; assert(f->locked && bytes == sizeof f->cache);
	return f->allocation_fails ? NULL : f->cache;
}
static int emit(unsigned id, const uint8_t *data, void *arg)
{
	struct fixture *f = arg; assert(f->locked && id == 8);
	memcpy(f->emitted, data, sizeof f->emitted); return 0;
}
static int event_write(struct ar_data_destination *destination, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source, void *arg)
{
	struct fixture *f = arg; (void)destination; (void)metadata; (void)source;
	const struct ar_data_event_ops ops = {allocate, emit};
	const struct ar_data_event_interface interface = {0};
	return ar_data_event_forward(&f->event, &interface, data, length, &ops, f);
}
static const struct ar_data_writer_ops writer_ops = {lock, receive, unlock, release, diagnose};
static void init(struct fixture *f)
{
	static const uint8_t bytes[] = {0xc8, 2, 1, 0xd5};
	memset(f, 0, sizeof *f);
	f->packet = (struct ar_data_packet){bytes, sizeof bytes};
	f->result = INT_MAX; f->references = 1;
	for (unsigned i = 0; i < 2; ++i) {
		f->destination[i].enabled = f->destination[i].queued = 1;
		f->destination[i].write = write_packet;
		f->writer[i].destination = &f->destination[i];
	}
}
static void one_message(struct fixture *f)
{
	f->count = 1;
	f->messages[0] = (struct ar_data_message){&f->packet, &f->route, 255, 65535};
}
int main(void)
{
	struct fixture f; init(&f);
	assert(ar_data_writer_step(&f.writer[0], &writer_ops, &f) == 0);
	assert(!strcmp(f.trace, "LQU") && f.references == 1 && !f.calls);
	init(&f); f.route.destinations = 3;
	const struct ar_data_route_ops route_ops = {find, retain, enqueue, release, diagnose};
	struct ar_data_route_stats stats = {0};
	assert(ar_data_route_deliver(&f.route, &f.packet, 255, 65535, &stats, &route_ops, &f) == 0);
	assert(f.count == 2 && f.references == 2 && !f.route.bytes && !f.route.packets);
	/* Disabling the destination after enqueue does not cancel its owned work. */
	f.destination[0].enabled = 0; f.trace_used = 0;
	assert(ar_data_writer_step(&f.writer[0], &writer_ops, &f) == 1);
	assert(!strcmp(f.trace, "LQWRU") && f.references == 1);
	assert(f.route.bytes == 4 && f.route.packets == 1 && f.destination[0].bytes == 4);
	f.trace_used = 0; f.result = -1;
	assert(ar_data_writer_step(&f.writer[1], &writer_ops, &f) == 1);
	assert(!strcmp(f.trace, "LQWDRU") && !f.references && f.writer[1].failures == 1);
	assert(f.route.bytes == 4 && f.route.packets == 1 && !f.destination[1].bytes);
	init(&f); one_message(&f); f.mutate = 1; f.result = 2;
	f.route.bytes = f.destination[0].bytes = UINT64_MAX; f.route.packets = UINT64_MAX;
	assert(ar_data_writer_step(&f.writer[0], &writer_ops, &f) == 1);
	assert(f.route.bytes == 1 && f.destination[0].bytes == 1 && !f.route.packets);
	init(&f); one_message(&f); f.writer[0].failures = UINT64_MAX; f.result = 0;
	assert(ar_data_writer_step(&f.writer[0], &writer_ops, &f) == 1);
	assert(!f.writer[0].failures && !f.references);
	for (unsigned fail = 0; fail < 2; ++fail) {
		init(&f); one_message(&f); f.destination[0].write = event_write;
		f.allocation_fails = fail;
		assert(ar_data_writer_step(&f.writer[0], &writer_ops, &f) == 1);
		assert(!f.references && f.route.packets == 1 && f.route.bytes == 4);
		if (fail) { assert(!f.event.buffer && !f.emitted[0]); }
		else { assert(f.emitted[0] == 4 && !memcmp(f.emitted + 2, f.packet.data, 4)); }
	}
	/* Native guards must release acquired ownership, but take none on invalid adapters. */
	for (unsigned invalid = 0; invalid < 3; ++invalid) {
		init(&f); one_message(&f);
		if (invalid == 0) { f.messages[0].route = NULL; }
		if (invalid == 1) { f.packet.length = UINT32_MAX; }
		if (invalid == 2) { f.invalidate = 1; }
		assert(ar_data_writer_step(&f.writer[0], &writer_ops, &f) == -22);
		assert(!f.references && !f.locked && !strcmp(f.trace, "LQRU") && !f.calls);
	}
	init(&f); one_message(&f);
	struct ar_data_writer_ops invalid_ops = writer_ops; invalid_ops.release = NULL;
	assert(ar_data_writer_step(&f.writer[0], &invalid_ops, &f) == -22);
	assert(f.references == 1 && f.count == 1 && !f.trace_used);
	return 0;
}
