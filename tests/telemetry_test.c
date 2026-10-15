/* SPDX-License-Identifier: MIT */
#include "ar_telemetry.h"
#include <assert.h>
#include <string.h>

static size_t make_frame(uint8_t *p, unsigned body)
{
	p[0] = 0xc8; p[1] = body + 1;
	uint8_t crc = 0;
	for (unsigned i = 0; i < body; i++) {
		p[i + 2] = (uint8_t)(i * 37 + 8); crc ^= p[i + 2];
		for (unsigned b = 0; b < 8; b++) { crc = (uint8_t)((crc << 1) ^ ((crc & 128) ? 0xd5 : 0)); }
	}
	p[body + 2] = crc; return body + 3;
}
static void start(struct ar_telemetry *s, struct ar_events *events)
{
	ar_events_init(events); events->subscriptions = 1u << 8;
	ar_telemetry_init(s, events); ar_telemetry_link(s, 1);
}
static void expect_event(struct ar_events *events, const uint8_t *bytes, size_t n)
{
	uint8_t message[1024]; struct ar_frame f;
	assert(ar_event_peek(events, message, sizeof message, &f));
	assert(f.reqid == 0x03020008 && !f.msgid && !f.status && f.length == 1024);
	assert((message[0] | (unsigned)message[1] << 8) == n);
	assert(!memcmp(message + 2, bytes, n));
	for (size_t i = n + 2; i < sizeof message; i++) { assert(!message[i]); }
	ar_event_commit(events);
}
int main(void)
{
	struct ar_telemetry s; struct ar_events events;
	uint8_t packet[257], other[257];
	/* Every legal frame size, every split, and a wrap in the runtime ring. */
	for (unsigned body = 1; body <= 254; body++) {
		size_t n = make_frame(packet, body);
		for (size_t split = 0; split <= n; split++) {
			start(&s, &events);
			s.parser.read = s.parser.write = s.parser.scan = sizeof s.ring - 3;
			ar_telemetry_receive(&s, 0, 2, packet, split);
			ar_telemetry_receive(&s, 0, 2, packet + split, n - split);
			assert(s.info.frames == 1 && s.info.published == 1 && !s.info.buffered);
			assert(!s.info.feed_errors && !s.info.malformed_bytes);
			memset(s.ring, 0xee, sizeof s.ring); /* queued event owns its bytes */
			expect_event(&events, packet, n);
		}
	}
	/* Many maximum frames in one descriptor, event storage exhaustion and
	 * recovery after draining. Consumed drops must not be replayed. */
	uint8_t burst[257 * 8]; size_t n = make_frame(packet, 254);
	for (unsigned i = 0; i < 8; i++) { memcpy(burst + i * n, packet, n); }
	start(&s, &events); ar_telemetry_receive(&s, 0, 2, burst, sizeof burst);
	assert(s.info.frames == 8 && s.info.published == 4 && s.info.event_drops == 4);
	assert(events.dropped == 4 && !s.info.buffered && !s.info.feed_errors);
	for (unsigned i = 0; i < 4; i++) { expect_event(&events, packet, n); }
	ar_telemetry_receive(&s, 0, 2, packet, n); expect_event(&events, packet, n);
	/* Corrupt frame cannot become a host event; recover at the next C8. */
	start(&s, &events); n = make_frame(packet, 1); memcpy(other, packet, n); other[n-1] ^= 1;
	ar_telemetry_receive(&s, 0, 2, other, n);
	ar_telemetry_receive(&s, 0, 2, packet, n);
	assert(s.info.frames == 1 && s.info.malformed_bytes == n);
	expect_event(&events, packet, n);
	/* No partial packet may cross a link loss/reconnection. */
	start(&s, &events); ar_telemetry_receive(&s, 0, 2, packet, 2);
	ar_telemetry_link(&s, 0); ar_telemetry_receive(&s, 0, 2, packet, n);
	assert(!s.info.frames && s.info.partial_discarded == 2);
	ar_telemetry_link(&s, 1); ar_telemetry_receive(&s, 0, 2, packet, n);
	assert(s.info.frames == 1 && s.info.sessions == 2); expect_event(&events, packet, n);
	/* Missing subscriptions are distinct from a failed publication. */
	start(&s, &events); events.subscriptions = 0;
	ar_telemetry_receive(&s, 0, 2, packet, n);
	assert(s.info.frames == 1 && s.info.unsubscribed == 1 && !s.info.published && !events.count);
	/* Wrong slot/port never enters the telemetry parser. Host-data observation
	 * preserves input and bounds its sample, including empty/oversized chunks. */
	start(&s, &events);
	ar_telemetry_receive(&s, 1, 2, packet, n); ar_telemetry_receive(&s, 0, 4, packet, n);
	assert(!s.info.rx_calls && !events.count);
	ar_telemetry_receive(&s, 0, 3, burst, sizeof burst);
	assert(s.info.host_length == sizeof burst && s.info.host_copied == 256);
	assert(!memcmp(s.info.host_fragment, burst, 256));
	ar_telemetry_receive(&s, 0, 3, NULL, 0); assert(!s.info.host_copied);
	for (unsigned i = 0; i < sizeof s.info.host_fragment; i++) { assert(!s.info.host_fragment[i]); }
	assert(sizeof(struct ar_telemetry_info) == 596);
	return 0;
}
