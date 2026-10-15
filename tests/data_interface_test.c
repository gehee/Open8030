/* SPDX-License-Identifier: MIT */
#include "ar_data_interface.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_data_services services;
	void *storage[12];
	unsigned allocations, releases, opens;
	int fail, open_result;
	struct ar_data_interface_spec expected;
};
static void *allocate(enum ar_data_service_allocation kind, size_t size, void *context)
{
	struct fixture *f = context; unsigned index = f->allocations++;
	assert(index < 12);
	assert(kind == AR_DATA_SERVICE_INTERFACE || kind == AR_DATA_SERVICE_SOCKET_HANDLE || kind == AR_DATA_SERVICE_READER);
	if ((int)index == f->fail) { return NULL; }
	void *p = malloc(size); assert(p); memset(p, 0xa5, size); f->storage[index] = p; return p;
}
static void release(void *p, void *context)
{
	struct fixture *f = context;
	for (unsigned i = 0; i < f->allocations; ++i) {
		if (f->storage[i] == p) { free(p); f->storage[i] = NULL; f->releases++; return; }
	}
	assert(0 && "unknown allocation");
}
static void *mutex(void *context) { (void)context; assert(0); return NULL; }
static int lock(void *m, int32_t timeout, void *context) { (void)m; (void)timeout; (void)context; assert(0); return -1; }
static int unlock(void *m, void *context) { (void)m; (void)context; assert(0); return -1; }
static void *queue(uint16_t count, uint32_t size, void *context) { (void)count; (void)size; (void)context; assert(0); return NULL; }
static int task(enum ar_data_service_task kind, void *arg, uint32_t priority, uint32_t stack, void **handle, void *context)
{ (void)kind; (void)arg; (void)priority; (void)stack; (void)handle; (void)context; assert(0); return -1; }
static int32_t open_socket(uint32_t selector, uint16_t port, uint32_t flags, uint32_t tx, uint32_t rx, void *context)
{
	struct fixture *f = context; f->opens++;
	assert(selector == f->expected.selector && port == f->expected.port);
	assert(flags == (port ? 3u : 19u));
	assert(tx == f->expected.socket_tx_capacity && rx == f->expected.socket_rx_capacity);
	assert(!f->services.interfaces); return f->open_result;
}
static const struct ar_data_interface_ops ops = {{allocate, release, mutex, lock, unlock, queue, task}, open_socket};
static void cleanup(struct fixture *f) { for (unsigned i = 0; i < f->allocations; ++i) { free(f->storage[i]); } }
static void profiles(void)
{
	for (unsigned type = 1; type <= 9; ++type) {
		for (unsigned port = 0; port < 3; ++port) {
			struct ar_data_interface_spec spec = {.type = type, .port = port, .selector = 65537,
				.framed = 255, .read_timeout = UINT32_MAX, .write_timeout = 100,
				.socket_tx_capacity = 524, .socket_rx_capacity = 1024};
			struct fixture f = {.fail = -1, .open_result = 7, .expected = spec};
			f.services.next_id = 65535;
			assert(!ar_data_interface_create(&f.services, &spec, 1, &ops, &f));
			struct ar_data_interface *i = (struct ar_data_interface *)f.services.interfaces;
			assert(i && i->service.id == 65535 && !f.services.next_id && i->service.selector == 65537);
			assert(i->read_timeout == UINT32_MAX && i->write_timeout == 100);
			int profile = type == 2 && port == 0 ? AR_DATA_PROFILE_SBUS :
				(type == 2 && port == 1) || (type == 3 && port == 0) ? AR_DATA_PROFILE_C8 : AR_DATA_PROFILE_NONE;
			assert((int)i->profile == profile && i->framed == (profile ? 1 : 255));
			if (profile) {
				assert(i->action.custom == 1 && i->action.prefix == &i->prefix && i->action.prefix_length == 1);
				assert(i->action.head_bytes == (profile == AR_DATA_PROFILE_SBUS ? 1u : 2u));
				assert(i->action.tail_bytes == 1 && i->prefix == (profile == AR_DATA_PROFILE_SBUS ? 15 : 200));
			} else { assert(!i->action.custom && !i->action.prefix_length && !i->action.prefix); }
			unsigned writers[] = {0, 1, 2, 3, 4, 0, 0, 5, 0, 0};
			assert((unsigned)i->writer == writers[type] && i->socket_reader == (type == 2));
			assert(f.opens == (type == 2) && f.allocations == (type == 2 ? 3u : 2u));
			assert(i->service.read_enabled && i->service.write_enabled && f.services.readers->interface == &i->service);
			if (type == 2) { assert(*i->socket_handle == 7); } else { assert(!i->socket_handle); }
			cleanup(&f);
		}
	}
}
static void admission(void)
{
	for (unsigned role = 0; role < 256; ++role) {
		struct fixture f = {.fail = -1};
		struct ar_data_interface_spec spec = {.type = 3, .port = 1, .role_mask = 1};
		assert(!ar_data_interface_create(&f.services, &spec, role, &ops, &f));
		assert((f.services.interfaces != NULL) == ((role & 31) == 0)); cleanup(&f);
	}
	for (unsigned selector = 0; selector <= 65536; selector += 65536) {
		struct fixture f = {.fail = -1};
		struct ar_data_interface_spec spec = {.type = 3, .port = 1, .selector = selector};
		assert(!ar_data_interface_create(&f.services, &spec, 1, &ops, &f));
		struct ar_data_service_interface *first = f.services.interfaces;
		spec.selector = 65536;
		assert(ar_data_interface_create(&f.services, &spec, 1, &ops, &f) == (selector ? 0 : -9996));
		assert(f.services.next_id == (selector ? 2 : 1));
		if (selector) { assert(f.services.interfaces->next == first); }
		else { assert(f.services.interfaces == first && f.allocations == 2); }
		cleanup(&f);
	}
	struct fixture f = {.fail = -1}; struct ar_data_interface_spec spec = {0};
	assert(ar_data_interface_create(&f.services, &spec, 1, &ops, &f) == -95);
	spec.type = 3;
	assert(ar_data_interface_create(NULL, &spec, 1, &ops, &f) == -22);
	assert(ar_data_interface_create(&f.services, NULL, 1, &ops, &f) == -22);
	assert(ar_data_interface_create(&f.services, &spec, 1, NULL, &f) == -22);
	struct ar_data_interface_ops invalid = ops; invalid.socket_open = NULL;
	assert(ar_data_interface_create(&f.services, &spec, 1, &invalid, &f) == -22);
	assert(!f.allocations && !f.opens && !f.services.next_id);
}
static void failures(void)
{
	for (int fail = -1; fail < 2; ++fail) {
		struct ar_data_interface_spec spec = {.type = 2, .port = 1};
		struct fixture f = {.fail = fail, .open_result = -3, .expected = spec};
		assert(ar_data_interface_create(&f.services, &spec, 1, &ops, &f) == (fail == 0 ? -9999 : -9998));
		assert(!f.services.interfaces && !f.services.next_id && f.releases == (fail != 0)); cleanup(&f);
	}
	struct ar_data_interface_spec spec = {.type = 2};
	struct fixture f = {.fail = 1, .open_result = 3, .expected = spec};
	assert(!ar_data_interface_create(&f.services, &spec, 1, &ops, &f));
	assert(f.opens == 1 && f.allocations == 2 && f.releases == 1 && !f.services.interfaces && !f.services.next_id);
	cleanup(&f);
	int32_t handle = -9; spec.socket_handle = &handle;
	f = (struct fixture){.fail = -1, .expected = spec};
	assert(!ar_data_interface_create(&f.services, &spec, 1, &ops, &f));
	assert(!f.opens && f.allocations == 2);
	assert(((struct ar_data_interface *)f.services.interfaces)->socket_handle == &handle);
	cleanup(&f); assert(handle == -9);
}
int main(void) { profiles(); admission(); failures(); return 0; }
