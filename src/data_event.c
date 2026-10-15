/* SPDX-License-Identifier: MIT */
#include "ar_data_event.h"
#include <string.h>

int ar_data_event_forward(struct ar_data_event *state,
	const struct ar_data_event_interface *interface, const uint8_t *data,
	size_t length, const struct ar_data_event_ops *ops, void *context)
{
	if (!interface) { return -1; }
	if (!data) { return -2; }
	/* Original forwarding lacks this bound; it allocates 1,024 bytes and copies
	 * after a two-byte prefix. Reject unsafe sizes in the native implementation. */
	if (!state || !ops || !ops->allocate || !ops->emit ||
	    length > AR_DATA_EVENT_MAX_PAYLOAD) { return -22; }
	if (!state->buffer) {
		state->buffer = ops->allocate(AR_DATA_EVENT_BUFFER_SIZE, context);
	}
	/* Preserve the original return contract: allocation failure reports the
	 * supplied length despite forwarding nothing. It is not delivery evidence. */
	if (!state->buffer) { return (int)length; }
	memset(state->buffer, 0, AR_DATA_EVENT_BUFFER_SIZE);
	memcpy(state->buffer + 2, data, length);
	state->buffer[0] = (uint8_t)length;
	state->buffer[1] = (uint8_t)(length >> 8);
	/* Read the live interface after allocation, as the original path does. */
	static const uint8_t ids[] = {8, 10, 11};
	if (interface->port >= sizeof ids) { return 0; }
	return ops->emit(ids[interface->port], state->buffer, context) ? 0 : (int)length;
}
