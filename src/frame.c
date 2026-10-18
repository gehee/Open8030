/* SPDX-License-Identifier: MIT */
/* Independently implemented from the documented ar_libre wire format. */
#include "ar_frame.h"
#include <string.h>

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t get_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void put_be32(uint8_t *p, uint32_t value)
{
	p[0] = value >> 24; p[1] = value >> 16;
	p[2] = value >> 8; p[3] = value;
}

static uint8_t header_check(const uint8_t *p)
{
	uint8_t value = 0xff;
	for (size_t i = 0; i < 17; i++) { value ^= p[i]; }
	return value;
}

size_t ar_encode(uint8_t *dst, size_t capacity, const struct ar_frame *frame)
{
	if (frame->length > AR_MAX_PAYLOAD || capacity < frame->length + AR_OVERHEAD ||
	    (frame->length && frame->payload == NULL)) { return 0; }
	dst[0] = 0xaa;
	dst[1] = frame->length; dst[2] = frame->length >> 8;
	dst[3] = frame->length >> 16; dst[4] = frame->length >> 24;
	put_be32(dst + 5, frame->reqid);
	put_be32(dst + 9, frame->msgid);
	put_be32(dst + 13, (uint32_t)frame->status);
	dst[17] = header_check(dst);
	/* A caller may place an owned payload directly at its final wire offset.
	 * Avoid a second copy (and overlapping memcpy) when framing that buffer. */
	if (frame->length && frame->payload != dst + AR_HEADER_SIZE) {
		memcpy(dst + AR_HEADER_SIZE, frame->payload, frame->length);
	}
	dst[18 + frame->length] = 0xbb;
	return frame->length + AR_OVERHEAD;
}

static void discard(struct ar_parser *parser, size_t length)
{
	parser->used -= length;
	memmove(parser->bytes, parser->bytes + length, parser->used);
}

static void consume(struct ar_parser *parser, ar_frame_callback callback, void *context)
{
	while (parser->used) {
		if (parser->bytes[0] != 0xaa) {
			/* Skip to the next start byte in one move (block padding). */
			const uint8_t *next = memchr(parser->bytes, 0xaa, parser->used);
			discard(parser, next ? (size_t)(next - parser->bytes) : parser->used);
			continue;
		}
		if (parser->used < AR_HEADER_SIZE) { return; }
		uint32_t length = get_le32(parser->bytes + 1);
		if (length > AR_MAX_PAYLOAD || header_check(parser->bytes) != parser->bytes[17]) {
			parser->rejected++; discard(parser, 1); continue;
		}
		if (parser->used < length + AR_OVERHEAD) { return; }
		if (parser->bytes[18 + length] != 0xbb) {
			parser->rejected++; discard(parser, 1); continue;
		}
		struct ar_frame frame = {
			.reqid = get_be32(parser->bytes + 5),
			.msgid = get_be32(parser->bytes + 9),
			.status = (int32_t)get_be32(parser->bytes + 13),
			.payload = parser->bytes + 18, .length = length,
		};
		callback(&frame, context);
		discard(parser, length + AR_OVERHEAD);
	}
}

void ar_feed(struct ar_parser *parser, const uint8_t *bytes, size_t length,
	     ar_frame_callback callback, void *context)
{
	/* Copy what fits, then consume every complete frame. A byte at a time,
	 * with the header checked after each byte, a 4 KiB write held the main
	 * loop for about a millisecond: long enough for an AP to miss its
	 * pre-encode. A full buffer always holds a complete or invalid frame
	 * (frames are bounded by its size), so consume frees room each pass. */
	while (length) {
		size_t room = sizeof parser->bytes - parser->used, n = length < room ? length : room;
		memcpy(parser->bytes + parser->used, bytes, n);
		parser->used += n; bytes += n; length -= n;
		consume(parser, callback, context);
	}
}
