/* SPDX-License-Identifier: MIT */
#ifndef AR_EVENT_H
#define AR_EVENT_H
#include "ar_frame.h"

#define AR_EVENT_QUEUE 16u
#define AR_EVENT_MAX_PAYLOAD 1024u
#define AR_EVENT_STORAGE 4096u
struct ar_event_entry { uint16_t length; uint8_t id; };
struct ar_events {
	struct ar_event_entry queue[AR_EVENT_QUEUE];
	unsigned head, count, subscriptions;
	uint32_t dropped;
	unsigned data_head, data_used;
	uint8_t data[AR_EVENT_STORAGE];
};
void ar_events_init(struct ar_events *events);
/* Original host event-size table; zero means no such event. Subscription support
 * does not establish an implemented producer for that event. */
size_t ar_event_size(unsigned id);
/* Subscribe/unsubscribe. Already copied notifications survive unsubscribe. */
int ar_event_request(struct ar_events *events, const struct ar_frame *request,
	struct ar_frame *response);
int ar_event_publish(struct ar_events *events, unsigned id, const uint8_t *data, size_t length);
/* The caller owns the copy until commit. Insufficient scratch returns zero and
 * leaves the queue intact. Operations are serialized by the polling loop. */
int ar_event_peek(const struct ar_events *events, uint8_t *data, size_t capacity,
	struct ar_frame *frame);
void ar_event_commit(struct ar_events *events);
#endif
