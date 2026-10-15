/* SPDX-License-Identifier: MIT */
#ifndef AR_SOCKET_SHUTDOWN_H
#define AR_SOCKET_SHUTDOWN_H
#include <stdint.h>

/* Source-only shutdown views. Tokens and message representation are analysis
 * adapters, not original firmware declarations or the SDIO wire format. */
struct ar_socket_shutdown_message {
	uint32_t reqid, msgid;
	int32_t status;
	uint32_t format;
	uint8_t storage[72];
};
struct ar_socket_shutdown_state {
	uint8_t slot, port, flags, tx_running, rx_running;
	uint32_t handle, tx_queue, event_queue, pool;
	uint32_t pending_retained, drained_retained, completed_retained;
};
struct ar_socket_shutdown_host {
	uint32_t reply_queue, pool, current_task, empty_format;
};
struct ar_socket_shutdown_ops {
	int (*close_handle)(uint32_t handle, void *context);
	int (*post)(uint32_t queue, struct ar_socket_shutdown_message *message,
		uint32_t front, uint32_t timeout, void *context);
	int (*take)(uint32_t queue, struct ar_socket_shutdown_message *message,
		uint32_t timeout, void *context);
	uint32_t (*length)(struct ar_socket_shutdown_message *message, void *context);
	void (*release)(uint32_t pool, struct ar_socket_shutdown_message *message,
		void *context);
	void (*release_buffer)(uint32_t pool, uint32_t buffer, void *context);
	void (*destroy_queue)(uint32_t queue, void *context);
	void (*delete_task)(uint32_t task, void *context);
};
/* These steps require serialized, live state and callback storage. Empty-message
 * construction replaces format and the first two storage bytes only; callers
 * must initialize scratch (including msgid/status) before creating notifications.
 * Original uninitialized stack bytes are not a stable protocol requirement.
 * Queue, close and release results are ignored where the original ignores them.
 * Missing native arguments/adapters return -22. A return of zero means this
 * local step completed, not that a post succeeded or host close completed.
 * No real RTOS scheduling, payload reclamation or DMA completion is established. */
int ar_socket_shutdown_request(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, struct ar_socket_shutdown_message *message,
	const struct ar_socket_shutdown_ops *ops, void *context);
/* Enter these worker shutdown phases only after their running flag is clear.
 * TX announces exit before draining. Every successfully taken message is
 * released; nonempty retained messages decrement pending and increment both
 * drain/completion counters. Any take error ends draining. Then destroy the
 * current TX queue, clear its token, and delete the current task. */
int ar_socket_shutdown_tx(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, struct ar_socket_shutdown_message *scratch,
	const struct ar_socket_shutdown_ops *ops, void *context);
/* RX releases a held copy buffer before posting its exit notification. */
int ar_socket_shutdown_rx(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, uint32_t held_buffer,
	struct ar_socket_shutdown_message *scratch,
	const struct ar_socket_shutdown_ops *ops, void *context);
/* Process a looked-up worker-exit notification (command 0x10 TX or 0x11 RX).
 * Clear that direction, release the notification, then recheck live flags.
 * Once both directions are clear, clear all flags and post a close reply using
 * the notification's saved low request-ID halfword. An absent state only
 * releases the message. Socket lookup and scheduling remain adapter work. */
int ar_socket_shutdown_event(struct ar_socket_shutdown_host *host,
	struct ar_socket_shutdown_state *state, struct ar_socket_shutdown_message *message,
	const struct ar_socket_shutdown_ops *ops, void *context);
#endif
