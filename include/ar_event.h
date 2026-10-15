/* SPDX-License-Identifier: MIT */
#ifndef AR_EVENT_H
#define AR_EVENT_H
#include "ar_frame.h"

#define AR_EVENT_QUEUE 16u
struct ar_event_entry { uint8_t id, length, data[4]; };
struct ar_events {
	struct ar_event_entry queue[AR_EVENT_QUEUE];
	unsigned head, count, subscriptions;
	uint32_t dropped;
};
void ar_events_init(struct ar_events *events);
/* Matched subscriptions for link(0) and MCS(1); other sources unsupported. */
int ar_event_request(struct ar_events *events, const struct ar_frame *request,
	struct ar_frame *response);
int ar_event_publish(struct ar_events *events, unsigned id, const uint8_t *data, size_t length);
int ar_event_peek(const struct ar_events *events, uint8_t data[4], struct ar_frame *frame);
void ar_event_commit(struct ar_events *events);
#endif
