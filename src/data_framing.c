/* SPDX-License-Identifier: MIT */
#include "ar_data_framing.h"
#include <string.h>

int ar_data_framing_init(struct ar_data_framing *s, uint8_t *ring, size_t capacity)
{
	if (!s || !ring || capacity < 4 || capacity > UINT16_MAX) { return -10000; }
	*s = (struct ar_data_framing){.ring = ring, .capacity = capacity};
	return 0;
}

static uint8_t peek(const struct ar_data_framing *s, size_t offset)
{
	return s->ring[(s->read + offset) % s->capacity];
}

static size_t next_sync(const struct ar_data_framing *s, size_t offset)
{
	while (offset < s->used && peek(s, offset) != 0xc8) { offset++; }
	return offset;
}

static int checksum_matches(const struct ar_data_framing *s, size_t body)
{
	uint8_t crc = 0;
	for (size_t i = 0; i < body; i++) {
		crc ^= peek(s, 2 + i);
		for (unsigned bit = 0; bit < 8; bit++) {
			crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0xd5 : 0));
		}
	}
	return crc == peek(s, 2 + body);
}

static void consume(struct ar_data_framing *s, size_t length,
	enum ar_data_segment_kind kind, ar_data_segment_fn segment, void *context)
{
	size_t first = s->capacity - s->read;
	if (first > length) { first = length; }
	if (kind == AR_DATA_NO_SYNC) { s->stats.no_sync_bytes += length; }
	if (kind == AR_DATA_BAD_LENGTH) { s->stats.bad_length_bytes += length; }
	if (kind == AR_DATA_BAD_CRC) { s->stats.bad_crc_bytes += length; }
	if (segment(kind, s->ring + s->read, first, s->ring, length - first, context)) {
		s->stats.allocation_failures++;
	} else {
		s->stats.packet_bytes += length;
		s->stats.packets++;
	}
	s->read = (s->read + length) % s->capacity;
	s->scan = s->read;
	s->used -= length;
	s->header = 0;
}

int ar_data_framing_feed(struct ar_data_framing *s, const uint8_t *data,
	size_t length, ar_data_segment_fn segment, void *context)
{
	if (!s || !s->ring || !segment || (!data && length) || length > UINT16_MAX ||
	    s->capacity < 4 || s->capacity > UINT16_MAX || s->used >= s->capacity ||
	    s->read >= s->capacity || s->write >= s->capacity || s->scan >= s->capacity) {
		return -10000;
	}
	s->stats.calls++;
	s->stats.input_bytes += length;
	size_t free = s->capacity - s->used;
	if (length && length >= free) {
		s->stats.overflow_bytes += length > free ? length - free : 1;
		return -9999;
	}
	size_t first = s->capacity - s->write;
	if (first > length) { first = length; }
	if (first) { memcpy(s->ring + s->write, data, first); }
	if (length > first) { memcpy(s->ring, data + first, length - first); }
	s->write = (s->write + length) % s->capacity;
	s->used += length;
	while (s->used >= 2) {
		if (peek(s, 0) != 0xc8) {
			consume(s, next_sync(s, 0), AR_DATA_NO_SYNC, segment, context);
			continue;
		}
		unsigned encoded = peek(s, 1);
		if (encoded < 2 || encoded - 1 > s->capacity) {
			consume(s, next_sync(s, 1), AR_DATA_BAD_LENGTH, segment, context);
			continue;
		}
		size_t total = encoded + 2;
		if (s->used < total) {
			s->header = 1;
			s->scan = s->write;
			break;
		}
		if (!checksum_matches(s, encoded - 1)) {
			consume(s, next_sync(s, 1), AR_DATA_BAD_CRC, segment, context);
			continue;
		}
		consume(s, total, AR_DATA_FRAME, segment, context);
	}
	return 0;
}
