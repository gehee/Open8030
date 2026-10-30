/* SPDX-License-Identifier: MIT */
/* Independent bounded host event transport. Original wire evidence:
 * Ascent VRX 18.21.10 23d368 subscriptions,23b79e notifications,255610 sizes. */
#include "ar_event.h"
#include <string.h>

void ar_events_init(struct ar_events *events) { memset(events, 0, sizeof *events); }

size_t ar_event_size(unsigned id)
{
	/* 14 is ours: TX power {slot, 0 the peer's (announced) / 1 our own,
	 * dBm now, dBm before; 0 = not known}. */
	static const uint16_t sizes[] = {3, 4, 4, 322, 0, 0, 256, 4, 1024, 12, 1024, 1024, 4, 136, 4};
	return id < sizeof sizes / sizeof sizes[0] ? sizes[id] : 0;
}

int ar_event_request(struct ar_events *events, const struct ar_frame *request,
	struct ar_frame *response)
{
	unsigned operation = request->reqid >> 16;
	if (operation != 0x0300 && operation != 0x0301) { return 0; }
	*response = (struct ar_frame){.reqid = request->reqid, .msgid = request->msgid};
	unsigned id = request->reqid & 0xffff;
	if (request->status || request->length) { response->status = -22; }
	else if (!ar_event_size(id)) { response->status = -2; }
	else if (operation == 0x0301) { events->subscriptions |= 1u << id; }
	else if (!(events->subscriptions & (1u << id))) { response->status = -2; }
	else {
		events->subscriptions &= ~(1u << id);
		/* Original removal unlinks the callback; notifications already copied
		 * into the host queue remain owned by that queue. */
	}
	return 1;
}

int ar_event_publish(struct ar_events *events, unsigned id, const uint8_t *data, size_t length)
{
	if (!ar_event_size(id) || !data || length != ar_event_size(id)) { return -22; }
	if (!(events->subscriptions & (1u << id))) { return 0; }
	if (events->count == AR_EVENT_QUEUE || length > AR_EVENT_STORAGE - events->data_used) {
		events->dropped++; return -11;
	}
	struct ar_event_entry *e = &events->queue[(events->head + events->count++) % AR_EVENT_QUEUE];
	e->id = id; e->length = length;
	size_t tail = (events->data_head + events->data_used) % AR_EVENT_STORAGE;
	size_t first = AR_EVENT_STORAGE - tail;
	if (first > length) { first = length; }
	memcpy(events->data + tail, data, first);
	if (length > first) { memcpy(events->data, data + first, length - first); }
	events->data_used += length;
	return 0;
}

int ar_event_peek(const struct ar_events *events, uint8_t *data, size_t capacity,
	struct ar_frame *frame)
{
	if (!events->count) { return 0; }
	const struct ar_event_entry *e = &events->queue[events->head];
	if (!data || !frame || capacity < e->length) { return 0; }
	size_t first = AR_EVENT_STORAGE - events->data_head;
	if (first > e->length) { first = e->length; }
	memcpy(data, events->data + events->data_head, first);
	if (e->length > first) { memcpy(data + first, events->data, e->length - first); }
	/* Original notifications have zero message ID and status; subscription
	 * acknowledgement retains its request ID and message ID. */
	*frame = (struct ar_frame){.reqid = UINT32_C(0x03020000) | e->id,
		.payload = data, .length = e->length};
	return 1;
}

void ar_event_commit(struct ar_events *events)
{
	if (!events->count) { return; }
	size_t length = events->queue[events->head].length;
	events->data_head = (events->data_head + length) % AR_EVENT_STORAGE;
	events->data_used -= length;
	events->head = (events->head + 1) % AR_EVENT_QUEUE; events->count--;
}
