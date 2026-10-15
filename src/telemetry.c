/* SPDX-License-Identifier: MIT */
#include "ar_telemetry.h"
#include <string.h>

void ar_telemetry_init(struct ar_telemetry *s, struct ar_events *events)
{
	memset(s, 0, sizeof *s);
	s->events = events; s->info.version = 1;
	s->bridge.buffer = s->event_buffer;
	(void)ar_data_framing_init(&s->parser, s->ring, sizeof s->ring);
}

void ar_telemetry_link(struct ar_telemetry *s, int linked)
{
	if (s->info.linked == (uint32_t)!!linked) { return; }
	s->info.partial_discarded += s->parser.used;
	(void)ar_data_framing_init(&s->parser, s->ring, sizeof s->ring);
	s->info.buffered = 0; s->info.last_frame_length = 0;
	s->info.host_length = s->info.host_copied = 0;
	memset(s->info.last_frame, 0, sizeof s->info.last_frame);
	memset(s->info.host_fragment, 0, sizeof s->info.host_fragment);
	s->info.linked = !!linked;
	if (linked) { s->info.sessions++; } else { s->info.resets++; }
}

static uint8_t *buffer(size_t length, void *context)
{
	struct ar_telemetry *s = context;
	return length == sizeof s->event_buffer ? s->event_buffer : NULL;
}

static int emit(unsigned id, const uint8_t *message, void *context)
{
	struct ar_telemetry *s = context;
	if (!(s->events->subscriptions & (1u << id))) {
		s->info.unsubscribed++; return 0;
	}
	int result = ar_event_publish(s->events, id, message, AR_DATA_EVENT_BUFFER_SIZE);
	if (result) { s->info.event_drops++; } else { s->info.published++; }
	return result;
}

static int segment(enum ar_data_segment_kind kind, const uint8_t *first,
	size_t first_length, const uint8_t *second, size_t second_length, void *context)
{
	struct ar_telemetry *s = context;
	size_t length = first_length + second_length;
	if (kind != AR_DATA_FRAME) { s->info.malformed_bytes += length; return 0; }
	/* The compared C8 profile emits complete envelopes of at most 257 bytes. */
	if (length > 257) { s->info.feed_errors++; return -22; }
	memset(s->info.last_frame, 0, sizeof s->info.last_frame);
	memcpy(s->info.last_frame, first, first_length);
	if (second_length) { memcpy(s->info.last_frame + first_length, second, second_length); }
	s->info.frames++; s->info.frame_bytes += length;
	s->info.last_frame_length = length;
	const struct ar_data_event_interface interface = {.port = 0};
	const struct ar_data_event_ops ops = {buffer, emit};
	int result = ar_data_event_forward(&s->bridge, &interface, s->info.last_frame,
		length, &ops, s);
	return result == (int)length ? 0 : -11;
}

void ar_telemetry_receive(struct ar_telemetry *s, unsigned slot, unsigned port,
	const uint8_t *data, size_t length)
{
	if (!s || !s->events || !s->info.linked || slot || (!data && length)) { return; }
	if (port == 3) {
		s->info.host_calls++; s->info.host_bytes += length;
		s->info.host_length = length;
		size_t n = length < sizeof s->info.host_fragment ? length : sizeof s->info.host_fragment;
		s->info.host_copied = n;
		memset(s->info.host_fragment, 0, sizeof s->info.host_fragment);
		if (n) { memcpy(s->info.host_fragment, data, n); }
		return;
	}
	if (port != 2) { return; }
	s->info.rx_calls++; s->info.rx_bytes += length;
	/* A retained partial C8 frame is at most 256 bytes. Feeding at most 256
	 * bytes at a time stays below the 524-byte ring's reserved-byte limit,
	 * even when a MAC descriptor concatenates many frames. */
	while (length) {
		size_t n = length < 256 ? length : 256;
		if (ar_data_framing_feed(&s->parser, data, n, segment, s)) {
			s->info.feed_errors++;
			/* An impossible state must not join fragments across lost input. */
			s->info.partial_discarded += s->parser.used + length;
			(void)ar_data_framing_init(&s->parser, s->ring, sizeof s->ring);
			break;
		}
		data += n; length -= n;
	}
	s->info.buffered = s->parser.used;
}
