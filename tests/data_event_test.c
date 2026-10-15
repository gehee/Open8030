/* SPDX-License-Identifier: MIT */
#include "ar_data_event.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_data_event_interface *interface;
	unsigned allocations, events, id;
	int fail_allocate, fail_emit, mutate_port;
	uint8_t copied[AR_DATA_EVENT_BUFFER_SIZE];
};
static uint8_t *allocate(size_t n, void *context)
{
	struct fixture *f = context;
	assert(n == AR_DATA_EVENT_BUFFER_SIZE); f->allocations++;
	if (f->mutate_port) { f->interface->port = 2; }
	return f->fail_allocate ? NULL : malloc(n);
}
static int emit(unsigned id, const uint8_t *message, void *context)
{
	struct fixture *f = context; f->events++; f->id = id;
	memcpy(f->copied, message, sizeof f->copied);
	return f->fail_emit;
}
int main(void)
{
	struct ar_data_event_interface interface = {0};
	struct ar_data_event state = {0};
	struct fixture f = {.interface = &interface};
	const struct ar_data_event_ops ops = {allocate, emit};
	uint8_t payload[AR_DATA_EVENT_MAX_PAYLOAD + 1];
	for (size_t i = 0; i < sizeof payload; i++) { payload[i] = (uint8_t)(i * 37 + 5); }
	assert(ar_data_event_forward(&state, NULL, payload, 1, &ops, &f) == -1);
	assert(ar_data_event_forward(&state, &interface, NULL, 0, &ops, &f) == -2);
	assert(ar_data_event_forward(&state, &interface, payload, sizeof payload, &ops, &f) == -22);
	assert(f.allocations == 0 && f.events == 0);
	f.fail_allocate = 1;
	assert(ar_data_event_forward(&state, &interface, payload, 19, &ops, &f) == 19);
	assert(state.buffer == NULL && f.allocations == 1 && f.events == 0);
	f.fail_allocate = 0; f.mutate_port = 1;
	assert(ar_data_event_forward(&state, &interface, payload, 1022, &ops, &f) == 1022);
	assert(f.id == 11 && f.copied[0] == 0xfe && f.copied[1] == 3);
	assert(!memcmp(f.copied + 2, payload, 1022));
	assert(f.allocations == 2 && f.events == 1);
	uint8_t *cached = state.buffer;
	for (unsigned port = 0; port < 3; port++) {
		interface.port = port;
		assert(ar_data_event_forward(&state, &interface, payload, 13, &ops, &f) == 13);
		assert(f.id == (port ? 9 + port : 8));
		assert(f.copied[0] == 13 && f.copied[1] == 0);
		assert(!memcmp(f.copied + 2, payload, 13));
		for (unsigned j = 15; j < sizeof f.copied; j++) { assert(f.copied[j] == 0); }
	}
	assert(state.buffer == cached && f.allocations == 2);
	assert(ar_data_event_forward(&state, &interface, payload, 0, &ops, &f) == 0);
	for (unsigned j = 0; j < sizeof f.copied; j++) { assert(f.copied[j] == 0); }
	f.fail_emit = -3;
	assert(ar_data_event_forward(&state, &interface, payload, 13, &ops, &f) == 0);
	interface.port = 3; unsigned events = f.events;
	assert(ar_data_event_forward(&state, &interface, payload, 13, &ops, &f) == 0);
	assert(f.events == events && state.buffer[0] == 13);
	assert(ar_data_event_forward(&state, &interface, payload, (size_t)-1, &ops, &f) == -22);
	assert(f.events == events && f.allocations == 2);
	free(state.buffer);
	return 0;
}
