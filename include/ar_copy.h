/* SPDX-License-Identifier: MIT */
#ifndef AR_COPY_H
#define AR_COPY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* memcpy for buffers that do not share their alignment. The minimal libc
 * copies words only when source and destination are aligned alike, and a
 * byte at a time otherwise: socket data sits 2 bytes off in its request
 * frame, which made a 10 KiB write cost hundreds of microseconds of main
 * loop. This reads aligned words and shifts them together (little-endian),
 * with every word read contained in the requested source range. */
static inline void ar_copy(void *destination, const void *source, size_t length)
{
	uint8_t *d = destination; const uint8_t *s = source;
	while (length && ((uintptr_t)d & 3)) { *d++ = *s++; length--; }
	unsigned offset = (uintptr_t)s & 3;
	if (!offset || length < 8) { memcpy(d, s, length); return; }
	/* Consume one word bytewise so rounding the source down cannot read
	 * before its allocation. Destination alignment is preserved. */
	for (unsigned i = 0; i < 4; i++) { *d++ = *s++; }
	length -= 4;
	if (length < 8 - offset) { memcpy(d, s, length); return; }
	const uint32_t *in = (const uint32_t *)(const void *)(s - offset);
	uint32_t *out = (uint32_t *)(void *)d, low = *in++;
	unsigned right = offset * 8, left = 32 - right;
	for (; length >= 8 - offset; length -= 4, s += 4) {
		uint32_t high = *in++;
		*out++ = low >> right | high << left;
		low = high;
	}
	d = (uint8_t *)out;
	while (length--) { *d++ = *s++; }
}

#endif
