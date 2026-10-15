/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_EVENT_H
#define AR_DATA_EVENT_H
#include <stddef.h>
#include <stdint.h>

#define AR_DATA_EVENT_BUFFER_SIZE 1024u
#define AR_DATA_EVENT_MAX_PAYLOAD (AR_DATA_EVENT_BUFFER_SIZE - 2u)

/* Native views, not original object layouts. The caller owns the cached 1,024-byte
 * buffer and must serialize use. Input must not overlap that buffer; an event
 * callback must consume/copy the message before returning. */
struct ar_data_event_interface { uint16_t port; };
struct ar_data_event { uint8_t *buffer; };
struct ar_data_event_ops {
	uint8_t *(*allocate)(size_t bytes, void *context);
	int (*emit)(unsigned id, const uint8_t *message, void *context);
};

/* Interface-to-event bridge. The first two message bytes hold the little-endian
 * payload length. The caller supplies runtime routing and storage ownership. */
int ar_data_event_forward(struct ar_data_event *state,
	const struct ar_data_event_interface *interface, const uint8_t *data,
	size_t length, const struct ar_data_event_ops *ops, void *context);
#endif
