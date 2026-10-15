/* SPDX-License-Identifier: MIT */
#include "ar_socket_shutdown.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_socket_shutdown_host host;
	struct ar_socket_shutdown_state state;
	struct ar_socket_shutdown_message message, events[2], reply;
	unsigned closes, posts, takes, releases, destroys, deletes, buffers;
	unsigned count, failure, mutate;
	uint32_t order[32];
	unsigned ordered;
};
static void record(struct fixture *f, uint32_t v) { assert(f->ordered < 32); f->order[f->ordered++] = v; }
static int close_handle(uint32_t handle, void *context)
{
	struct fixture *f = context;
	assert(handle == 9 && f->state.tx_running && f->state.rx_running);
	f->closes++; record(f, 1);
	return -95; /* Host dispatch ignores this failure. */
}
static int post(uint32_t queue, struct ar_socket_shutdown_message *m,
	uint32_t front, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(!front && m->format == f->host.empty_format && !m->storage[0] && !m->storage[1]);
	f->posts++; record(f, 10 + queue);
	if (queue == f->state.tx_queue) assert(!timeout);
	else if (queue == f->state.event_queue) {
		assert(timeout == UINT32_MAX);
		unsigned direction = (m->reqid >> 16) & 1;
		f->events[direction] = *m;
		if (f->mutate == 1) f->state.flags = 0;
	} else {
		assert(queue == f->host.reply_queue && !timeout);
		f->reply = *m;
	}
	return f->failure ? -1 : 0;
}
static int take(uint32_t queue, struct ar_socket_shutdown_message *m,
	uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(queue == f->state.tx_queue && !timeout);
	record(f, 20);
	if (f->takes++ == f->count) return -7;
	m->storage[0] = f->takes == 1 ? 0 : 8;
	return 0;
}
static uint32_t length(struct ar_socket_shutdown_message *m, void *context)
{
	struct fixture *f = context;
	record(f, 21);
	return m->storage[0];
}
static void release(uint32_t pool, struct ar_socket_shutdown_message *m, void *context)
{
	struct fixture *f = context;
	assert(pool == f->state.pool || pool == f->host.pool);
	f->releases++; record(f, 22);
	if (f->mutate == 2) { f->state.flags = 0; m->reqid = 0xdeadbeef; m->msgid = 17; }
}
static void release_buffer(uint32_t pool, uint32_t buffer, void *context)
{
	struct fixture *f = context;
	assert(pool == f->state.pool && buffer == 0x1234);
	f->buffers++; record(f, 23);
}
static void destroy_queue(uint32_t queue, void *context)
{
	struct fixture *f = context;
	assert(queue == f->state.tx_queue && f->takes == f->count + 1);
	f->destroys++; record(f, 24);
	if (f->mutate == 3) { f->state.tx_queue = 99; f->host.current_task = 77; }
}
static void delete_task(uint32_t task, void *context)
{
	struct fixture *f = context;
	assert(task == f->host.current_task);
	f->deletes++; record(f, 25);
}
static const struct ar_socket_shutdown_ops ops = {
	close_handle, post, take, length, release, release_buffer, destroy_queue, delete_task
};
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->host = (struct ar_socket_shutdown_host){1, 2, 3, 0x268530};
	f->state = (struct ar_socket_shutdown_state){.slot = 7, .port = 3, .flags = 0x23,
		.handle = 9, .tx_queue = 4, .event_queue = 5, .pool = 6,
		.pending_retained = 0, .drained_retained = UINT32_MAX,
		.completed_retained = UINT32_MAX};
	memset(&f->message, 0xa5, sizeof f->message);
	f->message.reqid = 0x04030703;
	f->message.msgid = 13;
	f->message.status = -42;
}
int main(void)
{
	struct fixture f;
	init(&f); f.state.tx_running = f.state.rx_running = 1; f.failure = 1;
	assert(!ar_socket_shutdown_request(&f.host, &f.state, &f.message, &ops, &f));
	assert(f.closes == 1 && f.posts == 1 && !f.state.tx_running && !f.state.rx_running);
	assert(f.state.flags == 0x23 && f.message.msgid == 13 && f.message.status == -42);
	assert(f.message.storage[2] == 0xa5);
	init(&f);
	assert(!ar_socket_shutdown_request(&f.host, NULL, &f.message, &ops, &f));
	assert(f.releases == 1 && !f.closes && f.posts == 1 && f.reply.status == 0x103);

	init(&f); f.count = 3; f.failure = 1;
	assert(!ar_socket_shutdown_tx(&f.host, &f.state, &f.message, &ops, &f));
	assert(f.order[0] == 15 && f.order[1] == 20); /* Notify, then drain. */
	assert(f.events[0].reqid == 0x04100703 && f.events[0].status == -42);
	assert(f.takes == 4 && f.releases == 3 && f.destroys == 1 && f.deletes == 1);
	assert(f.state.pending_retained == UINT32_MAX - 1 && f.state.drained_retained == 1);
	assert(f.state.completed_retained == 1 && !f.state.tx_queue);
	init(&f); f.count = 3; f.mutate = 1;
	assert(!ar_socket_shutdown_tx(&f.host, &f.state, &f.message, &ops, &f));
	assert(f.state.pending_retained == 0 && f.state.drained_retained == UINT32_MAX);
	assert(f.releases == 3);
	init(&f); f.mutate = 3;
	assert(!ar_socket_shutdown_tx(&f.host, &f.state, &f.message, &ops, &f));
	assert(!f.state.tx_queue && f.host.current_task == 77);

	init(&f);
	assert(!ar_socket_shutdown_rx(&f.host, &f.state, 0x1234, &f.message, &ops, &f));
	assert(f.order[0] == 23 && f.order[1] == 15 && f.order[2] == 25);
	assert(f.buffers == 1 && f.events[1].reqid == 0x04110703 && f.deletes == 1);

	/* Both notification orders result in one final reply, preserving metadata. */
	for (unsigned first = 0; first < 2; first++) {
		init(&f);
		f.message.reqid = 0x04100703 | first << 16;
		assert(!ar_socket_shutdown_event(&f.host, &f.state, &f.message, &ops, &f));
		assert(!f.posts && (f.state.flags & 3));
		f.message.reqid = 0x04100703 | (!first) << 16;
		assert(!ar_socket_shutdown_event(&f.host, &f.state, &f.message, &ops, &f));
		assert(f.posts == 1 && !f.state.flags && f.reply.reqid == 0x04030703);
		assert(f.reply.msgid == 13 && f.reply.status == -42 && f.releases == 2);
	}
	init(&f); f.mutate = 2; f.message.reqid = 0x04100703;
	assert(!ar_socket_shutdown_event(&f.host, &f.state, &f.message, &ops, &f));
	assert(f.reply.reqid == 0x04030703 && f.reply.msgid == 17 && !f.state.flags);
	init(&f); f.message.reqid = 0x04100703;
	assert(!ar_socket_shutdown_event(&f.host, NULL, &f.message, &ops, &f));
	assert(f.releases == 1 && !f.posts);
	assert(ar_socket_shutdown_tx(&f.host, NULL, &f.message, &ops, &f) == -22);
	f.message.reqid = 0x04120703;
	assert(ar_socket_shutdown_event(&f.host, &f.state, &f.message, &ops, &f) == -22);
	puts("Socket shutdown request, worker drain, exit notification ordering and cleanup tests passed");
	return 0;
}
