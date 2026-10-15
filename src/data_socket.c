/* SPDX-License-Identifier: MIT */
#include "ar_data_socket.h"

int ar_data_socket_write(const struct ar_data_socket *socket, const uint8_t *data,
	uint32_t length, const struct ar_data_socket_ops *ops, void *context)
{
	if (!socket) { return -1; }
	if (!data) { return -2; }
	if (!socket->handle) { return -3; }
	if (*socket->handle < 0) { return 0; }
	if (!ops || !ops->selector_type || !ops->mode || !ops->query_tx_used ||
	    !ops->role || !ops->write) { return -22; }
	if (ops->selector_type(socket->selector, context) != 2) { return 0; }
	if (socket->port == 1) {
		unsigned limit = ops->mode(context) == 1 ? 262 : 32;
		uint32_t used = 0;
		(void)ops->query_tx_used(0, 1, &used, context);
		if (ops->role(context) && used > limit) { return 0; }
	}
	return ops->write(*socket->handle, data, length, socket->timeout, context);
}
