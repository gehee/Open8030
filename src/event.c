/* SPDX-License-Identifier: MIT */
/* Independent bounded host event transport. Original wire evidence:
 * Ascent VRX 18.21.10 23d368 subscriptions,23b79e notifications,255610 sizes. */
#include "ar_event.h"
#include <string.h>

void ar_events_init(struct ar_events *events) { memset(events, 0, sizeof *events); }

int ar_event_request(struct ar_events *events, const struct ar_frame *request,
	struct ar_frame *response)
{
	unsigned operation = request->reqid >> 16;
	if (operation != 0x0300 && operation != 0x0301) { return 0; }
	*response = (struct ar_frame){.reqid = request->reqid, .msgid = request->msgid};
	unsigned id = request->reqid & 0xffff;
	if (request->status || request->length) { response->status = -22; }
	else if (id > 1) { response->status = -95; }
	else if (operation == 0x0301) { events->subscriptions |= 1u << id; }
	else if (!(events->subscriptions & (1u << id))) { response->status = -2; }
	else {
		events->subscriptions &= ~(1u << id);
		/* Remove queued notifications for this subscription. Preserve the
		 * order and copied ownership of every other pending event. */
		unsigned kept = 0;
		for (unsigned i = 0; i < events->count; i++) {
			struct ar_event_entry e = events->queue[(events->head + i) % AR_EVENT_QUEUE];
			if (e.id != id) { events->queue[(events->head + kept++) % AR_EVENT_QUEUE] = e; }
		}
		events->count = kept;
	}
	return 1;
}

int ar_event_publish(struct ar_events *events, unsigned id, const uint8_t *data, size_t length)
{
	if (id > 1 || !data || length != (id ? 4u : 3u)) { return -22; }
	if (!(events->subscriptions & (1u << id))) { return 0; }
	if (events->count == AR_EVENT_QUEUE) { events->dropped++; return -11; }
	struct ar_event_entry *e = &events->queue[(events->head + events->count++) % AR_EVENT_QUEUE];
	e->id = id; e->length = length; memcpy(e->data, data, length);
	return 0;
}

int ar_event_peek(const struct ar_events *events, uint8_t data[4], struct ar_frame *frame)
{
	if (!events->count) { return 0; }
	const struct ar_event_entry *e = &events->queue[events->head];
	memcpy(data, e->data, e->length);
	/* Original notifications have zero message ID and status; subscription
	 * acknowledgement retains its request ID and message ID. */
	*frame = (struct ar_frame){.reqid = UINT32_C(0x03020000) | e->id,
		.payload = data, .length = e->length};
	return 1;
}

void ar_event_commit(struct ar_events *events)
{
	if (!events->count) { return; }
	events->head = (events->head + 1) % AR_EVENT_QUEUE; events->count--;
}
