/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_SOCKET_H
#define AR_DATA_SOCKET_H
#include <stddef.h>
#include <stdint.h>

/* Source-only internal interface writer. These are native views, not original
 * object layouts. Caller serializes interface, handle and adapter state. */
struct ar_data_socket {
	uint16_t port;
	uint32_t selector;
	const int32_t *handle;
	uint32_t timeout;
};
struct ar_data_socket_ops {
	unsigned (*selector_type)(unsigned selector, void *context);
	unsigned (*mode)(void *context);
	/* Implements the selected word of getter 0x01000010 for selector 0,
	 * host port 1. Return status is ignored; always initialize *used. */
	int (*query_tx_used)(unsigned selector, unsigned port, uint32_t *used,
		void *context);
	unsigned (*role)(void *context);
	int (*write)(int32_t handle, const uint8_t *data, uint32_t length,
		uint32_t timeout, void *context);
};
/* Null interface/data/handle return -1/-2/-3. A negative handle or selector
 * type other than 2 returns 0. Port 1 queries TX occupancy even on AP; nonzero
 * roles suppress writing above 32 bytes (262 when mode == 1). Other ports
 * bypass that query/gate. Suppression returns 0; otherwise propagate write.
 * Missing adapters return -22. Query storage starts at zero as a native guard
 * against an adapter failing without writing it; original stack is uninitialized.
 * This neither opens streams nor implements the underlying blocking write. */
int ar_data_socket_write(const struct ar_data_socket *, const uint8_t *data,
	uint32_t length, const struct ar_data_socket_ops *, void *context);
#endif
