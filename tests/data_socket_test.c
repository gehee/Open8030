/* SPDX-License-Identifier: MIT */
#include "ar_data_socket.h"
#include "ar_data_route.h"
#include <assert.h>
#include <string.h>

struct fixture {
	unsigned type, mode, role, used, writes, releases, diagnoses;
	int result, query_result, mutate_mode;
	char order[16]; unsigned steps;
	struct ar_data_socket socket;
	struct ar_data_destination destination;
};
static void step(struct fixture *f, char c) { assert(f->steps < 15); f->order[f->steps++] = c; }
static unsigned selector(unsigned selector, void *context)
{
	struct fixture *f = context; assert(selector == f->socket.selector);
	step(f, 's'); return f->type;
}
static unsigned mode(void *context) { struct fixture *f = context; step(f, 'm'); return f->mode; }
static int query(unsigned selector, unsigned port, uint32_t *used, void *context)
{
	struct fixture *f = context; assert(selector == 0 && port == 1);
	step(f, 'q'); *used = f->used;
	if (f->mutate_mode) { f->mode = 1; }
	return f->query_result;
}
static unsigned role(void *context) { struct fixture *f = context; step(f, 'r'); return f->role; }
static int write_bytes(int32_t handle, const uint8_t *data, uint32_t length,
	uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(handle == *f->socket.handle && data && length <= 257 && timeout == 100);
	step(f, 'w'); f->writes++; return f->result;
}
static const struct ar_data_socket_ops ops = {selector, mode, query, role, write_bytes};
static struct ar_data_destination *find(unsigned id, void *context)
{
	struct fixture *f = context; assert(id == 1); return &f->destination;
}
static void retain(struct ar_data_packet *packet, void *context) { (void)packet; (void)context; assert(0); }
static int enqueue(struct ar_data_destination *d, struct ar_data_packet *p,
	struct ar_data_route *r, uint8_t metadata, uint16_t source, void *context)
{
	(void)d; (void)p; (void)r; (void)metadata; (void)source; (void)context;
	assert(0); return -1;
}
static void release(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context; assert(packet); f->releases++;
}
static void diagnose(struct ar_data_destination *d, void *context)
{
	struct fixture *f = context; assert(d == &f->destination); f->diagnoses++;
}
static int routed_write(struct ar_data_destination *d, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source, void *context)
{
	struct fixture *f = context; assert(d == &f->destination && metadata == 0 && source == 2);
	return ar_data_socket_write(&f->socket, data, length, &ops, f);
}
int main(void)
{
	int32_t handle = 7; const uint8_t data[257] = {0xc8, 0xff};
	for (unsigned port = 0; port < 3; ++port) {
		for (unsigned m = 0; m < 3; ++m) {
			for (unsigned r = 0; r < 2; ++r) {
				for (unsigned used = 0; used <= 524; ++used) {
					struct fixture f = {.type = 2, .mode = m, .role = r, .used = used,
						.result = 257, .query_result = -4, .socket = {port, 5, &handle, 100}};
					int blocked = port == 1 && r && used > (m == 1 ? 262 : 32);
					assert(ar_data_socket_write(&f.socket, data, 257, &ops, &f) == (blocked ? 0 : 257));
					assert(f.writes == !blocked);
					assert(!strcmp(f.order, port != 1 ? "sw" : blocked ? "smqr" : "smqrw"));
				}
			}
		}
	}
	struct fixture f = {.type = 2, .role = 1, .used = 33, .mutate_mode = 1,
		.socket = {1, 0, &handle, 100}};
	assert(ar_data_socket_write(&f.socket, data, 4, &ops, &f) == 0);
	assert(f.writes == 0 && f.mode == 1); /* Threshold read before query. */
	assert(ar_data_socket_write(NULL, data, 4, &ops, &f) == -1);
	assert(ar_data_socket_write(&f.socket, NULL, 4, &ops, &f) == -2);
	f.socket.handle = NULL; assert(ar_data_socket_write(&f.socket, data, 4, &ops, &f) == -3);
	f.socket.handle = &handle; handle = -1;
	assert(ar_data_socket_write(&f.socket, data, 4, &ops, &f) == 0);
	handle = 7; assert(ar_data_socket_write(&f.socket, data, 4, NULL, &f) == -22);
	for (unsigned type = 0; type < 4; ++type) {
		if (type == 2) { continue; }
		f = (struct fixture){.type = type, .socket = {1, 0, &handle, 100}};
		assert(ar_data_socket_write(&f.socket, data, 4, &ops, &f) == 0);
		assert(!strcmp(f.order, "s"));
	}
	/* Composition: suppression and short writes count as failed delivery,
	 * release the packet once and do not retry. This is a synchronous adapter. */
	const struct ar_data_route_ops route_ops = {find, retain, enqueue, release, diagnose};
	for (unsigned used = 32; used <= 33; ++used) {
		for (int result = -1; result <= 4; ++result) {
			f = (struct fixture){.type = 2, .role = 1, .used = used, .result = result,
				.socket = {1, 0, &handle, 100}, .destination = {.enabled = 1, .write = routed_write}};
			struct ar_data_route route = {.destinations = 2};
			struct ar_data_packet packet = {data, 4}; struct ar_data_route_stats stats = {0};
			assert(ar_data_route_deliver(&route, &packet, 0, 2, &stats, &route_ops, &f) == 0);
			int success = used == 32 && result == 4;
			assert(route.packets == (unsigned)success && route.bytes == (unsigned)success * 4);
			assert(f.destination.bytes == route.bytes && stats.delivery_failures == (unsigned)!success);
			assert(f.releases == 1 && f.diagnoses == (unsigned)!success && f.writes == (used == 32));
		}
	}
	return 0;
}
