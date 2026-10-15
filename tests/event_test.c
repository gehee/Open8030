/* SPDX-License-Identifier: MIT */
#include "ar_event.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void subscribe(struct ar_events *e, unsigned id)
{
	struct ar_frame q = {.reqid = 0x03010000 | id, .msgid = 0x12345678}, r;
	assert(ar_event_request(e, &q, &r));
	assert(r.reqid == q.reqid && r.msgid == q.msgid && !r.status && !r.length);
}

int main(void)
{
	struct ar_events e; ar_events_init(&e);
	uint8_t link[] = {0, 2, 1}, mcs[] = {0, 1, 10, 8}, copy[AR_EVENT_MAX_PAYLOAD];
	struct ar_frame r, q = {.reqid = 0x01000000};
	assert(!ar_event_request(&e, &q, &r));
	assert(!ar_event_publish(&e, 0, link, sizeof link) && !e.count);
	subscribe(&e, 0); subscribe(&e, 0); subscribe(&e, 1);
	assert(e.subscriptions == 3 && !e.count); /* No invented initial event. */
	q = (struct ar_frame){.reqid = 0x03010004};
	assert(ar_event_request(&e, &q, &r) && r.status == -2 && e.subscriptions == 3);
	q.reqid = 0x03010000; q.length = 1;
	assert(ar_event_request(&e, &q, &r) && r.status == -22);
	q.length = 0; q.status = 1;
	assert(ar_event_request(&e, &q, &r) && r.status == -22);
	assert(ar_event_publish(&e, 0, NULL, 3) == -22);
	assert(ar_event_publish(&e, 1, link, 3) == -22);
	assert(!ar_event_publish(&e, 0, link, sizeof link));
	link[1] = 0; /* Caller storage can change after publication. */
	assert(ar_event_peek(&e, copy, sizeof copy, &r) && r.reqid == 0x03020000 && !r.msgid && !r.status);
	assert(r.length == 3 && copy[1] == 2 && copy[2] == 1);
	/* Appending while a frame is being sent must not disturb the peek. */
	assert(!ar_event_publish(&e, 1, mcs, sizeof mcs));
	assert(copy[1] == 2 && e.count == 2);
	ar_event_commit(&e);
	assert(ar_event_peek(&e, copy, sizeof copy, &r) && r.reqid == 0x03020001 && r.length == 4);
	assert(!memcmp(copy, mcs, 4)); ar_event_commit(&e);
	assert(!ar_event_peek(&e, copy, sizeof copy, &r)); ar_event_commit(&e);
	/* Wrap around, bounded backpressure, FIFO, and unsubscribe without cancelling queued copies. */
	for (unsigned round = 0; round < 4; round++) {
		for (unsigned i = 0; i < AR_EVENT_QUEUE; i++) {
			mcs[2] = i;
			assert(!ar_event_publish(&e, i & 1, (i & 1) ? mcs : link, (i & 1) ? 4 : 3));
		}
		assert(ar_event_publish(&e, 0, link, 3) == -11 && e.count == AR_EVENT_QUEUE);
		q = (struct ar_frame){.reqid = 0x03000000};
		assert(ar_event_request(&e, &q, &r) && !r.status && e.count == AR_EVENT_QUEUE);
		assert(ar_event_request(&e, &q, &r) && r.status == -2);
		for (unsigned i = 0; i < AR_EVENT_QUEUE; i++) {
			assert(ar_event_peek(&e, copy, sizeof copy, &r) && r.reqid == (0x03020000u | (i & 1)));
			assert((i & 1) ? copy[2] == i : copy[1] == 0);
			ar_event_commit(&e);
		}
		assert(!e.count); subscribe(&e, 0);
	}
	assert(e.dropped == 4);
	ar_events_init(&e); assert(!e.count && !e.subscriptions && !e.dropped);
	const size_t sizes[] = {3, 4, 4, 322, 0, 0, 256, 4, 1024, 12, 1024, 1024, 4, 136};
	uint8_t payload[AR_EVENT_MAX_PAYLOAD];
	for (unsigned round = 0; round < 20; round++) {
		for (unsigned id = 0; id < sizeof sizes / sizeof sizes[0]; id++) {
			assert(ar_event_size(id) == sizes[id]);
			if (!sizes[id]) { continue; }
			for (size_t j = 0; j < sizes[id]; j++) { payload[j] = (uint8_t)(id + j + round); }
			subscribe(&e, id);
			assert(!ar_event_publish(&e, id, payload, sizes[id]));
			memset(payload, 0, sizeof payload);
			assert(!ar_event_peek(&e, copy, sizes[id] - 1, &r) && e.count == 1);
			assert(ar_event_peek(&e, copy, sizeof copy, &r));
			assert(r.reqid == (0x03020000u | id) && r.length == sizes[id] && !r.status && !r.msgid);
			for (size_t j = 0; j < sizes[id]; j++) { assert(copy[j] == (uint8_t)(id + j + round)); }
			ar_event_commit(&e); assert(!e.data_used);
		}
	}
	assert(ar_event_size(65535) == 0 && ar_event_size((unsigned)-1) == 0);
	/* Byte capacity fills before descriptor capacity for large events. */
	for (unsigned i = 0; i < 4; i++) {
		memset(payload, i, sizeof payload);
		assert(!ar_event_publish(&e, 8, payload, sizeof payload));
	}
	assert(e.count == 4 && e.data_used == AR_EVENT_STORAGE);
	assert(ar_event_publish(&e, 8, payload, sizeof payload) == -11);
	q = (struct ar_frame){.reqid = 0x03000008};
	assert(ar_event_request(&e, &q, &r) && !r.status && e.count == 4);
	assert(!ar_event_publish(&e, 8, payload, sizeof payload) && e.count == 4);
	for (unsigned i = 0; i < 4; i++) {
		assert(ar_event_peek(&e, copy, sizeof copy, &r));
		for (size_t j = 0; j < sizeof copy; j++) { assert(copy[j] == i); }
		ar_event_commit(&e);
	}
	assert(!e.count && !e.data_used);
	puts("bounded host event contracts passed");
}
