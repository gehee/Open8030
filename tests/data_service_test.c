/* SPDX-License-Identifier: MIT */
#include "ar_data_service.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_data_services services;
	struct ar_data_service_interface interface;
	void *allocated[8];
	unsigned allocations, releases, mutexes, locks, unlocks, queues, tasks[3];
	int fail_alloc, fail_task, fail_queue, fail_mutex;
};
static void *allocate(enum ar_data_service_allocation kind, size_t size, void *context)
{
	struct fixture *f = context;
	assert(f->services.interfaces == &f->interface);
	unsigned index = f->allocations++;
	assert(index < 8 && kind == (enum ar_data_service_allocation)(index % 3));
	if ((int)index == f->fail_alloc) { return NULL; }
	void *p = malloc(size); assert(p); memset(p, 0xa5, size);
	f->allocated[index] = p; return p;
}
static void release(void *p, void *context)
{
	struct fixture *f = context;
	for (unsigned i = 0; i < f->allocations; ++i) {
		if (f->allocated[i] == p) { f->allocated[i] = NULL; f->releases++; free(p); return; }
	}
	assert(0 && "release of unknown allocation");
}
static void *mutex(void *context)
{
	struct fixture *f = context; f->mutexes++; return f->fail_mutex ? NULL : f;
}
static int lock(void *m, int32_t timeout, void *context)
{
	struct fixture *f = context; assert(m == (f->fail_mutex ? NULL : f));
	assert(timeout == -1); f->locks++; return -3;
}
static int unlock(void *m, void *context)
{
	struct fixture *f = context; assert(m == (f->fail_mutex ? NULL : f));
	f->unlocks++; return -4;
}
static void *queue(uint16_t capacity, uint32_t size, void *context)
{
	struct fixture *f = context; assert(capacity == 7 && size == 32);
	f->queues++; return f->fail_queue ? NULL : &f->queues;
}
static int task(enum ar_data_service_task kind, void *arg, uint32_t priority,
	uint32_t stack, void **handle, void *context)
{
	struct fixture *f = context; assert(f->services.interfaces == &f->interface);
	f->tasks[kind]++;
	if (kind == AR_DATA_SERVICE_READ) {
		struct ar_data_service_reader *reader = arg;
		assert(!f->services.readers && reader->interface == &f->interface);
		assert(priority == 18 && stack == 512);
		assert(reader->capacity == 13 && reader->ring->capacity == 13);
		assert(!reader->ring->read && !reader->ring->write && !reader->ring->used);
		assert(!reader->pending && !reader->consumed && !reader->busy);
		assert(reader->ring->bytes[0] == 0xa5); /* Payload storage is not cleared. */
	} else {
		assert(arg == &f->interface);
		assert(priority == (kind == AR_DATA_SERVICE_AT ? 3u : 19u));
		assert(stack == (kind == AR_DATA_SERVICE_AT ? 4096u : 1024u));
	}
	*handle = &f->tasks[kind];
	return (int)kind == f->fail_task ? -7 : 0;
}
static const struct ar_data_service_ops ops = {allocate, release, mutex, lock, unlock, queue, task};
static const struct ar_data_service_config config = {13, 512, 7, 1024, 0};
static void cleanup(struct fixture *f)
{
	/* Fixture owns resources even after a partially failed construction. */
	for (unsigned i = 0; i < f->allocations; ++i) { free(f->allocated[i]); }
}
static void failures(void)
{
	for (int allocation = -1; allocation < 3; ++allocation) {
		for (int writer = 0; writer < 3; ++writer) {
			struct fixture f = {.interface = {.type = 2}, .fail_alloc = allocation,
				.fail_task = writer == 2 ? AR_DATA_SERVICE_WRITE : -1, .fail_queue = writer == 1};
			int result = ar_data_service_publish(&f.services, &f.interface, &config, &ops, &f);
			assert(result == (writer == 2 ? -9998 : writer == 1 || allocation >= 0 ? -9999 : 0));
			assert(f.services.interfaces == &f.interface && f.services.next_id == 1);
			assert((f.services.readers != NULL) == (allocation < 0));
			assert(f.releases == (allocation > 0 ? (unsigned)allocation : 0));
			assert(f.queues == 1 && f.tasks[AR_DATA_SERVICE_WRITE] == (writer != 1));
			assert(f.interface.read_enabled == (writer == 0) && f.interface.write_enabled == (writer == 0));
			unsigned before = f.allocations;
			assert(ar_data_service_publish(&f.services, &f.interface, &config, &ops, &f) == -22);
			assert(f.allocations == before && f.services.next_id == 1);
			cleanup(&f);
		}
	}
	struct fixture f = {.interface = {.type = 2}, .fail_alloc = -1,
		.fail_task = AR_DATA_SERVICE_READ, .fail_mutex = 1};
	assert(!ar_data_service_publish(&f.services, &f.interface, &config, &ops, &f));
	assert(f.services.readers && f.interface.read_enabled && f.locks == 1 && f.unlocks == 1);
	assert(f.mutexes == 2 && !f.services.readers->mutex && !f.interface.writer_mutex);
	cleanup(&f);
}
static void publication(void)
{
	struct fixture f = {.interface = {.type = 0, .read_enabled = 2, .write_enabled = 255},
		.fail_alloc = -1, .fail_task = AR_DATA_SERVICE_AT};
	struct ar_data_service_interface old = {0}; struct ar_data_service_reader reader = {0};
	f.services = (struct ar_data_services){&old, &reader, 65535};
	struct ar_data_service_config at = config; at.uart_at = 255;
	assert(!ar_data_service_publish(&f.services, &f.interface, &at, &ops, &f));
	assert(f.interface.id == 65535 && !f.services.next_id && f.interface.next == &old);
	assert(f.services.readers == &reader && !f.allocations && !f.queues);
	assert(f.tasks[AR_DATA_SERVICE_AT] == 1 && f.interface.read_enabled == 2 && f.interface.write_enabled == 255);
	struct ar_data_service_interface second = {.type = 3};
	struct ar_data_service_config zero = {0};
	/* No callbacks may run for invalid initial inputs. */
	assert(ar_data_service_publish(NULL, &second, &zero, &ops, &f) == -22);
	assert(ar_data_service_publish(&f.services, NULL, &zero, &ops, &f) == -22);
	assert(ar_data_service_publish(&f.services, &second, NULL, &ops, &f) == -22);
	assert(ar_data_service_publish(&f.services, &second, &zero, NULL, &f) == -22);
	struct ar_data_service_ops invalid = ops; invalid.task = NULL;
	assert(ar_data_service_publish(&f.services, &second, &zero, &invalid, &f) == -22);
	assert(!f.allocations && !f.services.next_id && f.services.interfaces == &f.interface);
	/* A zero-capacity reader is still allocated and published. */
	f.interface = (struct ar_data_service_interface){.type = 2};
	f.services.interfaces = &old; f.services.next_id = 0;
	assert(!ar_data_service_publish(&f.services, &f.interface, &zero, &ops, &f));
	assert(!f.interface.id && f.services.next_id == 1 && f.allocations == 1);
	assert(f.services.readers->next == &reader && !f.services.readers->ring);
	assert(f.interface.read_enabled && f.interface.write_enabled && !f.mutexes);
	cleanup(&f);
}
int main(void) { failures(); publication(); return 0; }
