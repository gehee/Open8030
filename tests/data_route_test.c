/* SPDX-License-Identifier: MIT */
#include "ar_data_route.h"
#include "ar_data_event.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

struct fixture {
	struct ar_data_destination destinations[64];
	struct ar_data_packet packet;
	struct ar_data_route route;
	struct ar_data_route_stats stats;
	unsigned references, calls, diagnostics, queued, missing;
	int queue_error, result, mutate;
	struct ar_data_event event;
	uint8_t cache[1024], captured[1024];
};
static struct ar_data_destination *find(unsigned id, void *arg)
{
	struct fixture *f = arg;
	return id == f->missing ? NULL : &f->destinations[id];
}
static void retain(struct ar_data_packet *p, void *arg)
{
	struct fixture *f = arg; assert(p == &f->packet); ++f->references;
}
static void release(struct ar_data_packet *p, void *arg)
{
	struct fixture *f = arg; assert(p == &f->packet && f->references);
	--f->references;
}
static void diagnose(struct ar_data_destination *d, void *arg)
{
	struct fixture *f = arg; assert(d); ++f->diagnostics;
}
static int enqueue(struct ar_data_destination *d, struct ar_data_packet *p,
	struct ar_data_route *r, uint8_t metadata, uint16_t source, void *arg)
{
	struct fixture *f = arg;
	assert(d && p == &f->packet && r == &f->route);
	assert(metadata == 0xa5 && source == 17 && f->references >= 2);
	++f->queued; return f->queue_error;
}
static int write_packet(struct ar_data_destination *d, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source, void *arg)
{
	struct fixture *f = arg;
	assert(d && metadata == 0xa5 && source == 17);
	assert(data == f->packet.data && length == f->packet.length);
	++f->calls;
	if (f->mutate) { f->packet.length = 2; f->route.destinations |= UINT64_C(1) << 63; }
	return f->result == INT_MAX ? (int)length : f->result;
}
static uint8_t *allocate(size_t length, void *arg)
{
	struct fixture *f = arg; assert(length == sizeof f->cache); return f->cache;
}
static int emit(unsigned id, const uint8_t *message, void *arg)
{
	struct fixture *f = arg; assert(id == 8);
	memcpy(f->captured, message, sizeof f->captured); return 0;
}
static int event_write(struct ar_data_destination *d, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source, void *arg)
{
	(void)d; (void)metadata; (void)source;
	struct fixture *f = arg;
	const struct ar_data_event_interface interface = {0};
	const struct ar_data_event_ops ops = {allocate, emit};
	return ar_data_event_forward(&f->event, &interface, data, length, &ops, f);
}
static const struct ar_data_route_ops ops = {find, retain, enqueue, release, diagnose};
static void init(struct fixture *f)
{
	static const uint8_t frame[] = {0xc8, 2, 0x01, 0xd5};
	memset(f, 0, sizeof *f);
	f->packet = (struct ar_data_packet){frame, sizeof frame};
	f->references = 1; f->missing = 64; f->result = INT_MAX;
	for (unsigned i = 0; i < 64; ++i) {
		f->destinations[i].enabled = 1; f->destinations[i].write = write_packet;
	}
}
static void deliver(struct fixture *f)
{
	assert(ar_data_route_deliver(&f->route, &f->packet, 0xa5, 17,
		&f->stats, &ops, f) == 0);
}
int main(void)
{
	struct fixture f;
	init(&f); f.route.destinations = UINT64_MAX; deliver(&f);
	assert(f.calls == 64 && f.route.packets == 64 && f.route.bytes == 256);
	assert(!f.references && !f.stats.delivery_failures);
	init(&f); f.route.destinations = 0xff;
	f.missing = 0; f.destinations[1].enabled = 0; f.destinations[2].write = NULL;
	f.destinations[3].queued = 1; f.destinations[4].queued = 1;
	f.result = -1; deliver(&f);
	assert(f.stats.missing_destinations == 1 && f.destinations[1].disabled == 1);
	assert(f.queued == 2 && f.references == 2 && f.calls == 3);
	assert(f.stats.delivery_failures == 3 && f.diagnostics == 3 && !f.route.packets);
	/* Queued recipients own their retained references after the sender exits. */
	release(&f.packet, &f); release(&f.packet, &f); assert(!f.references);
	init(&f); f.route.destinations = 1; f.destinations[0].queued = 1;
	f.queue_error = -1; deliver(&f);
	assert(!f.references && f.stats.delivery_failures == 1 && f.diagnostics == 1);
	init(&f); f.route.destinations = 1; f.mutate = 1; f.result = 2; deliver(&f);
	assert(f.calls == 2 && f.route.bytes == 4 && f.route.packets == 2);
	init(&f); f.route.destinations = 1;
	f.route.bytes = f.destinations[0].bytes = UINT64_MAX - 1;
	f.route.packets = UINT64_MAX; deliver(&f);
	assert(f.route.bytes == 2 && f.destinations[0].bytes == 2 && !f.route.packets);
	init(&f); f.route.destinations = 1; f.destinations[0].write = event_write;
	deliver(&f); assert(f.captured[0] == 4 && f.captured[1] == 0);
	assert(!memcmp(f.captured + 2, f.packet.data, 4));
	for (unsigned i = 6; i < sizeof f.captured; ++i) { assert(!f.captured[i]); }
	init(&f); f.packet.length = UINT32_MAX;
	assert(ar_data_route_deliver(&f.route, &f.packet, 0, 0, &f.stats, &ops, &f) == -22);
	assert(f.references == 1);
	init(&f);
	struct ar_data_route_ops broken = ops;
	broken.release = NULL;
	assert(ar_data_route_deliver(&f.route, &f.packet, 0, 0, &f.stats, &broken, &f) == -22);
	assert(f.references == 1 && !f.calls && !f.queued);
	return 0;
}
