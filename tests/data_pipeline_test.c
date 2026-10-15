/* SPDX-License-Identifier: MIT */
#include "ar_data_framing.h"
#include "data_mode3_fixture.h"
#include "ar_data_route.h"
#include "ar_data_event.h"
#include <assert.h>
#include <string.h>

/* Native construction, parsing, filtering, dispatch and worker delivery are
 * composed here. Registry, packet/queue storage and scheduling are adapters. */
struct fixture {
	unsigned source, role, writes, emissions, allocations, segments;
	int destination, queued, references;
	uint8_t packet_bytes[257], written[257], event_bytes[1024];
	struct ar_data_packet packet;
	struct ar_data_route *route;
	struct ar_data_built_route nodes[6], *head;
	struct ar_data_worker_action actions[6];
	unsigned node_count, action_count, selected_worker;
	struct ar_data_dispatch_worker queues[2];
	struct ar_data_worker workers[2];
	struct ar_data_worker_message message;
	struct ar_data_action_buffer buffer;
	int locked, received;
	struct ar_data_destination output;
	struct ar_data_event event;
};
static uint8_t *allocate(size_t size, void *context)
{
	struct fixture *f = context;
	assert(size == sizeof f->event_bytes);
	f->allocations++;
	return f->event_bytes;
}
static int emit(unsigned id, const uint8_t *data, void *context)
{
	struct fixture *f = context;
	assert(id == 8 && data == f->event_bytes);
	f->emissions++;
	return 0;
}
static int write_packet(struct ar_data_destination *d, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source, void *context)
{
	struct fixture *f = context;
	assert(d == &f->output && metadata == 0 && source == f->source);
	assert(length <= sizeof f->written);
	if (f->destination == 2) {
		const struct ar_data_event_interface interface = {0};
		const struct ar_data_event_ops ops = {allocate, emit};
		return ar_data_event_forward(&f->event, &interface, data, length,
			&ops, f);
	}
	f->writes++;
	memcpy(f->written, data, length);
	return (int)length;
}
static struct ar_data_destination *find(unsigned id, void *context)
{
	struct fixture *f = context;
	assert(id == (unsigned)f->destination);
	return &f->output;
}
static void release(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context;
	assert(packet == &f->packet && f->references > 0);
	f->references--;
}
static void unexpected_retain(struct ar_data_packet *packet, void *context)
{
	(void)packet; (void)context;
	assert(0 && "unexpected destination retain");
}
static int unexpected_enqueue(struct ar_data_destination *d,
	struct ar_data_packet *packet, struct ar_data_route *route, uint8_t metadata,
	uint16_t source, void *context)
{
	(void)d; (void)packet; (void)route; (void)metadata; (void)source; (void)context;
	assert(0 && "unexpected destination queue");
	return -1;
}
static void unexpected_diagnostic(struct ar_data_destination *d, void *context)
{
	(void)d; (void)context;
	assert(0 && "unexpected delivery failure");
}
static int identity(const struct ar_data_identity *key, uint16_t *id, void *context)
{
	(void)context;
	const struct ar_data_identity registry[] = {{0, 2, 0}, {0, 1, 0},
		{3, 0, 0}, {2, 0, 0}, {2, 1, 0}};
	for (unsigned i = 0; i < 5; ++i) {
		if (key->type == registry[i].type && key->port == registry[i].port &&
			key->selector == registry[i].selector) { *id = (uint16_t)i; return 0; }
	}
	return -1;
}
static int framed(uint16_t id, uint8_t *value, void *context)
{
	(void)context; assert(id < 5); *value = 1; return 0;
}
static struct ar_data_built_route *node(void *context)
{
	struct fixture *f = context; assert(f->node_count < 6); return &f->nodes[f->node_count++];
}
static struct ar_data_worker_action *action(void *context)
{
	struct fixture *f = context; assert(f->action_count < 6); return &f->actions[f->action_count++];
}
static struct ar_data_packet *copy_packet(struct ar_data_packet *packet, void *context)
{
	(void)packet; (void)context; assert(0 && "unexpected copying route"); return NULL;
}
static void retain_packet(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context; assert(packet == &f->packet && f->references > 0); f->references++;
}
static int enqueue_packet(unsigned worker, struct ar_data_dispatch_node *dispatch,
	struct ar_data_packet *packet, uint8_t metadata, uint16_t source_id, void *context)
{
	struct fixture *f = context; struct ar_data_built_route *built = (struct ar_data_built_route *)dispatch;
	assert(worker < 2 && !f->queued && built->action);
	f->selected_worker = worker; f->route = &built->action->route;
	f->destination = -1;
	for (unsigned id = 0; id < 5; ++id) {
		if (f->route->destinations == (UINT64_C(1) << id)) { f->destination = (int)id; }
	}
	assert(f->destination >= 0);
	f->message = (struct ar_data_worker_message){packet, built->action, metadata, source_id, 0};
	f->queued = 1; return 0;
}
static int wake(uint8_t value, void *context)
{
	(void)context; assert(value == 255); return 0;
}
static void lock(void *context) { struct fixture *f = context; assert(!f->locked); f->locked = 1; }
static void unlock(void *context) { struct fixture *f = context; assert(f->locked); f->locked = 0; }
static int receive(struct ar_data_worker_message *message, void *context)
{
	struct fixture *f = context; assert(f->locked);
	if (!f->queued || f->received) { return -9; }
	*message = f->message; f->received = 1; return 0;
}
static struct ar_data_action_buffer *buffer(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context; assert(!f->locked && packet == &f->packet); return &f->buffer;
}
static uint32_t ticks(void *context) { (void)context; return 0; }
static int segment(enum ar_data_segment_kind kind, const uint8_t *a, size_t n,
	const uint8_t *b, size_t m, void *context)
{
	struct fixture *f = context;
	f->segments++;
	assert(kind == AR_DATA_FRAME && n + m <= sizeof f->packet_bytes);
	assert(!f->queued && f->references == 0);
	memcpy(f->packet_bytes, a, n);
	if (m) { memcpy(f->packet_bytes + n, b, m); }
	f->packet = (struct ar_data_packet){f->packet_bytes, (uint32_t)(n + m)};
	f->references = 1;
	f->buffer = (struct ar_data_action_buffer){f->packet_bytes, sizeof f->packet_bytes, 0, n + m};
	const struct ar_data_filter_input input = {.source = f->source == 2 ?
		(struct ar_data_identity){3, 0, 0} : (struct ar_data_identity){2, 1, 0},
		.data = f->packet_bytes, .length = (uint32_t)(n + m)};
	const struct ar_data_dispatch_ops ops = {{identity, framed}, copy_packet,
		retain_packet, release, enqueue_packet, wake};
	const uint16_t map[] = {0, 1}; uint32_t matches = 0;
	struct ar_data_dispatch_stats stats = {0};
	int result = ar_data_dispatch(f->head ? &f->head->dispatch : NULL, &f->packet,
		&input, 0, (uint16_t)f->source, f->queues, 2, map, 2, &stats, &matches, &ops, f);
	int expected = f->role == 1 ? (f->source == 2 ? 4 : 2) :
		f->role == 0 && f->source == 4 ? 0 : -1;
	assert(f->destination == expected);
	assert(result == (expected < 0 ? -4996 : 0));
	assert(matches == (expected >= 0) && stats.unmatched == (expected < 0));

	return 0;
}
static uint8_t crc(const uint8_t *data, size_t length)
{
	uint8_t v = 0;
	for (size_t i = 0; i < length; ++i) {
		v ^= data[i];
		for (unsigned bit = 0; bit < 8; ++bit) {
			v = (uint8_t)((v << 1) ^ ((v & 0x80) ? 0xd5 : 0));
		}
	}
	return v;
}
int main(void)
{
	const size_t capacities[] = {AR_DATA_MODE3_EVENT_RING_SIZE,
		AR_DATA_MODE3_SOCKET1_RING_SIZE};
	const size_t sizes[] = {1, 22, 254};
	for (unsigned type = 0; type < 2; ++type) {
		for (unsigned role = 0; role < 3; ++role) {
			for (unsigned s = 0; s < 3; ++s) {
				const size_t length = sizes[s] + 3;
				for (size_t split = 1; split < length; ++split) {
					uint8_t ring[1043] = {0}, frame[257] = {0xc8};
					struct fixture f = {.source = type ? 4 : 2, .role = role, .destination = -1};
					const struct ar_data_builder_ops build = {{identity, framed}, node, action};
					assert(ar_data_routes_build(&f.head, mode3_routes, 6, (uint8_t)role, &build, &f) == 0);
					assert(f.node_count == (role < 2 ? 4u : 2u));
					for (unsigned w = 0; w < 2; ++w) {
						f.queues[w] = (struct ar_data_dispatch_worker){128, 128};
						f.workers[w].queue = &f.queues[w];
					}
					struct ar_data_framing parser;
					frame[1] = (uint8_t)(sizes[s] + 1);
					for (size_t i = 0; i < sizes[s]; ++i) { frame[i + 2] = (uint8_t)(5 + 37 * i); }
					frame[length - 1] = crc(frame + 2, sizes[s]);
					assert(ar_data_framing_init(&parser, ring, capacities[type]) == 0);
					parser.read = parser.write = parser.scan = capacities[type] - 1;
					assert(ar_data_framing_feed(&parser, frame, split, segment, &f) == 0);
					assert(f.segments == 0 && f.references == 0);
					assert(ar_data_framing_feed(&parser, frame + split,
						length - split, segment, &f) == 0);
					assert(f.segments == 1 && parser.used == 0);
					assert(parser.stats.packets == 1 && parser.stats.packet_bytes == length);
					assert(f.writes == 0 && f.emissions == 0);
					/* Deferred delivery must own a copy, independent of the ring. */
					memset(ring, 0xa5, sizeof ring);
					if (f.queued) {
						assert(f.references == 1 && f.selected_worker == (role == 0 ? 1u : 0u));
						f.output.enabled = 1;
						f.output.write = write_packet;
						const struct ar_data_worker_ops ops = {lock, receive, unlock, buffer,
							{0}, {find, unexpected_retain, unexpected_enqueue, release, unexpected_diagnostic}, ticks};
						struct ar_data_worker *worker = &f.workers[f.selected_worker];
						assert(ar_data_worker_step(worker, &ops, &f) == 0);
						assert(worker->bytes == length && worker->packets == 1 && !worker->aged);
						assert(worker->queue->free == 128 && worker->queue->low == 127);

						assert(f.route->packets == 1 && f.route->bytes == length);
						assert(f.output.bytes == length && f.output.disabled == 0);
						if (f.destination == 2) {
							assert(f.emissions == 1 && f.allocations == 1 && f.writes == 0);
							assert(f.event_bytes[0] == (uint8_t)length && f.event_bytes[1] == length >> 8);
							assert(!memcmp(f.event_bytes + 2, frame, length));
							for (size_t i = length + 2; i < sizeof f.event_bytes; ++i) { assert(f.event_bytes[i] == 0); }
						} else {
							assert(f.writes == 1 && f.emissions == 0 && f.allocations == 0);
							assert(!memcmp(f.written, frame, length));
						}
					} else {
						assert(!f.route);
						assert(f.writes == 0 && f.emissions == 0 && f.allocations == 0);
					}
					assert(f.references == 0 && !f.locked);
					for (unsigned w = 0; w < 2; ++w) {
						assert(!f.workers[w].delivery.delivery_failures && !f.workers[w].delivery.missing_destinations);
					}
				}
			}
		}
	}
	return 0;
}
