/* SPDX-License-Identifier: MIT */
#ifndef AR_TELEMETRY_H
#define AR_TELEMETRY_H
#include "ar_data_framing.h"
#include "ar_data_event.h"
#include "ar_event.h"

/* Cooperative mode-3 DEV receive adapter. MAC port 2 contains C8 frames;
 * each valid complete frame becomes original host event 8. MAC port 3 is
 * still delivered by the external socket path; its bounded sample is diagnostic
 * only and makes no claim about the application's payload format. */
struct ar_telemetry_info {
	uint32_t version, linked, sessions, resets;
	uint32_t rx_calls, rx_bytes, frames, frame_bytes;
	uint32_t published, unsubscribed, event_drops, malformed_bytes;
	uint32_t feed_errors, partial_discarded, buffered, last_frame_length;
	uint32_t host_calls, host_bytes, host_length, host_copied;
	uint8_t last_frame[260], host_fragment[256];
};
struct ar_telemetry {
	struct ar_telemetry_info info;
	struct ar_data_framing parser;
	struct ar_data_event bridge;
	struct ar_events *events;
	uint8_t ring[AR_DATA_MODE3_SOCKET1_RING_SIZE];
	uint8_t event_buffer[AR_DATA_EVENT_BUFFER_SIZE];
};
void ar_telemetry_init(struct ar_telemetry *, struct ar_events *);
/* Clear partial framing on every link-state change. Queued host notifications
 * retain their existing event-transport ownership; counters remain cumulative. */
void ar_telemetry_link(struct ar_telemetry *, int linked);
void ar_telemetry_receive(struct ar_telemetry *, unsigned slot, unsigned mac_port,
	const uint8_t *data, size_t length);
#endif
