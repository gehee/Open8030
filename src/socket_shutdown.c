/* SPDX-License-Identifier: MIT */
#include "ar_socket_shutdown.h"
#include <stddef.h>

static void empty(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_message *message)
{
	message->format = host->empty_format;
	message->storage[0] = message->storage[1] = 0;
}

int ar_socket_shutdown_request(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, struct ar_socket_shutdown_message *message,
	const struct ar_socket_shutdown_ops *ops, void *context)
{
	if (!host || !message || !ops || !ops->close_handle || !ops->post || !ops->release)
		return -22;
	if (!state) {
		ops->release(host->pool, message, context);
		message->status = 0x103;
		empty(host, message);
		(void)ops->post(host->reply_queue, message, 0, 0, context);
		return 0;
	}
	(void)ops->close_handle(state->handle, context);
	empty(host, message);
	(void)ops->post(state->tx_queue, message, 0, 0, context);
	state->tx_running = state->rx_running = 0;
	return 0;
}

int ar_socket_shutdown_tx(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, struct ar_socket_shutdown_message *scratch,
	const struct ar_socket_shutdown_ops *ops, void *context)
{
	if (!host || !state || !scratch || !ops || !ops->post || !ops->take ||
	    !ops->length || !ops->release || !ops->destroy_queue || !ops->delete_task)
		return -22;
	scratch->reqid = 0x04100000u | (uint32_t)state->slot << 8 | state->port;
	empty(host, scratch);
	(void)ops->post(state->event_queue, scratch, 0, UINT32_MAX, context);
	while (!ops->take(state->tx_queue, scratch, 0, context)) {
		if (ops->length(scratch, context) && (state->flags & 0x20)) {
			state->pending_retained--;
			state->drained_retained++;
			state->completed_retained++;
		}
		ops->release(state->pool, scratch, context);
	}
	ops->destroy_queue(state->tx_queue, context);
	state->tx_queue = 0;
	ops->delete_task(host->current_task, context);
	return 0;
}

int ar_socket_shutdown_rx(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, uint32_t held_buffer,
	struct ar_socket_shutdown_message *scratch,
	const struct ar_socket_shutdown_ops *ops, void *context)
{
	if (!host || !state || !scratch || !ops || !ops->post ||
	    !ops->release_buffer || !ops->delete_task)
		return -22;
	scratch->reqid = 0x04110000u | (uint32_t)state->slot << 8 | state->port;
	empty(host, scratch);
	if (held_buffer)
		ops->release_buffer(state->pool, held_buffer, context);
	(void)ops->post(state->event_queue, scratch, 0, UINT32_MAX, context);
	ops->delete_task(host->current_task, context);
	return 0;
}

int ar_socket_shutdown_event(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, struct ar_socket_shutdown_message *message,
	const struct ar_socket_shutdown_ops *ops, void *context)
{
	if (!host || !message || !ops || !ops->post || !ops->release)
		return -22;
	unsigned command = (message->reqid >> 16) & 255;
	if (command != 0x10 && command != 0x11)
		return -22;
	uint32_t address = message->reqid & 65535;
	if (!state) {
		ops->release(host->pool, message, context);
		return 0;
	}
	state->flags &= command == 0x10 ? ~2u : ~1u;
	ops->release(host->pool, message, context);
	if (!(state->flags & 3)) {
		state->flags = 0;
		message->reqid = 0x04030000u | address;
		empty(host, message);
		(void)ops->post(host->reply_queue, message, 0, 0, context);
	}
	return 0;
}
