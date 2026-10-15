/* SPDX-License-Identifier: MIT */
#include "ar_data_framing.h"
#include "ar_data_event.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_data_event event;
	unsigned segments, frames, errors, fail_at, events;
	uint8_t frame[257], message[AR_DATA_EVENT_BUFFER_SIZE];
	size_t length;
};
static uint8_t *allocate(size_t bytes, void *context)
{
	(void)context;
	return malloc(bytes);
}
static int event(unsigned id, const uint8_t *message, void *context)
{
	struct fixture *f = context;
	assert(id == 8);
	memcpy(f->message, message, sizeof f->message);
	f->events++;
	return 0;
}
static int segment(enum ar_data_segment_kind kind, const uint8_t *a, size_t n,
	const uint8_t *b, size_t m, void *context)
{
	struct fixture *f = context;
	f->segments++;
	if (f->segments == f->fail_at) { return -1; }
	if (kind != AR_DATA_FRAME) { f->errors++; return 0; }
	assert(n + m <= sizeof f->frame);
	memcpy(f->frame, a, n); memcpy(f->frame + n, b, m);
	f->length = n + m; f->frames++;
	/* Explicit synchronous adapter, not the still-missing runtime route. */
	const struct ar_data_event_interface interface = {0};
	const struct ar_data_event_ops ops = {allocate, event};
	assert(ar_data_event_forward(&f->event, &interface, f->frame, f->length,
		&ops, f) == (int)f->length);
	return 0;
}
static void split_and_wrap(void)
{
	/* Known CRC8-D5 vectors also exercised against the original instructions. */
	const uint8_t stream[] = {3, 4, 5, 0xc8, 3, 8, 0x2d, 0xff,
		0xc8, 2, 5, 0x2b, 0xc8, 2, 2, 0x7f};
	for (size_t start = 0; start < 32; start++) {
		for (size_t chunk = 1; chunk <= sizeof stream; chunk++) {
			uint8_t ring[32] = {0}; struct ar_data_framing s;
			struct fixture f = {0};
			assert(ar_data_framing_init(&s, ring, sizeof ring) == 0);
			s.read = s.write = s.scan = start;
			for (size_t pos = 0; pos < sizeof stream;) {
				size_t n = sizeof stream - pos;
				if (n > chunk) { n = chunk; }
				assert(ar_data_framing_feed(&s, stream + pos, n, segment, &f) == 0);
				pos += n;
			}
			assert(s.used == 0 && s.header == 0 && s.read == s.write);
			assert(f.frames == 2 && f.events == 2 && f.errors > 0);
			assert(f.length == 4 && !memcmp(f.frame, stream + 12, 4));
			assert(f.message[0] == 4 && f.message[1] == 0);
			assert(!memcmp(f.message + 2, stream + 12, 4));
			for (size_t i = 6; i < sizeof f.message; i++) { assert(f.message[i] == 0); }
			assert(s.stats.input_bytes == sizeof stream);
			assert(s.stats.no_sync_bytes + s.stats.bad_crc_bytes == 8);
			assert(s.stats.bad_length_bytes == 0);
			free(f.event.buffer);
		}
	}
}
int main(void)
{
	split_and_wrap();
	uint8_t ring[1024] = {0}; struct ar_data_framing s;
	struct fixture f = {.fail_at = 1};
	const uint8_t frame[] = {0xc8, 2, 5, 0x2b};
	assert(ar_data_framing_init(&s, ring, sizeof ring) == 0);
	assert(ar_data_framing_feed(&s, frame, sizeof frame, segment, &f) == 0);
	assert(s.used == 0 && s.stats.allocation_failures == 1 && f.events == 0);
	assert(ar_data_framing_feed(&s, frame, sizeof frame, segment, &f) == 0);
	assert(s.stats.packets == 1 && s.stats.packet_bytes == 4 && f.events == 1);
	free(f.event.buffer); f = (struct fixture){0};

	/* Reserve one byte without overwriting a buffered partial frame. */
	assert(ar_data_framing_init(&s, ring, 16) == 0);
	assert(ar_data_framing_feed(&s, frame, 2, segment, &f) == 0);
	uint8_t saved[16]; memcpy(saved, ring, sizeof saved);
	uint8_t rejected[14] = {0};
	assert(ar_data_framing_feed(&s, rejected, sizeof rejected, segment, &f) == -9999);
	assert(s.used == 2 && s.header == 1 && !memcmp(saved, ring, sizeof saved));
	assert(ar_data_framing_feed(&s, frame + 2, 2, segment, &f) == 0);
	assert(f.frames == 1 && s.used == 0);
	assert(ar_data_framing_feed(&s, NULL, 0, segment, &f) == 0);
	assert(ar_data_framing_feed(&s, NULL, 1, segment, &f) == -10000);
	assert(ar_data_framing_feed(&s, frame, (size_t)-1, segment, &f) == -10000);
	assert(ar_data_framing_feed(&s, frame, 4, NULL, &f) == -10000);
	assert(ar_data_framing_init(NULL, ring, 16) == -10000);
	assert(ar_data_framing_init(&s, NULL, 16) == -10000);
	assert(ar_data_framing_init(&s, ring, 3) == -10000);
	assert(ar_data_framing_init(&s, ring, (size_t)UINT16_MAX + 1) == -10000);
	free(f.event.buffer);

	/* Maximum frame: 254 body bytes (all zero), length255, zero CRC. */
	f = (struct fixture){0}; uint8_t largest[257] = {0xc8, 0xff};
	assert(ar_data_framing_init(&s, ring, sizeof ring) == 0);
	s.read = s.write = s.scan = sizeof ring - 2;
	for (size_t i = 0; i < sizeof largest; i++) {
		assert(ar_data_framing_feed(&s, largest + i, 1, segment, &f) == 0);
		assert(f.frames == (i == sizeof largest - 1));
	}
	assert(f.length == sizeof largest && !memcmp(f.frame, largest, sizeof largest));
	assert(f.message[0] == 1 && f.message[1] == 1 && s.used == 0);
	free(f.event.buffer);
	return 0;
}
