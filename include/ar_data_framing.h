/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_FRAMING_H
#define AR_DATA_FRAMING_H
#include <stddef.h>
#include <stdint.h>

/* Parser for the mode-3 event-port-0 and socket-port-1 profile:
 * C8, length, body, CRC8-D5. This does not implement other original profiles,
 * route ownership, packet allocation, or RTOS workers. */
#define AR_DATA_MODE3_EVENT_RING_SIZE 1043u
#define AR_DATA_MODE3_SOCKET1_RING_SIZE 524u
enum ar_data_segment_kind {
	AR_DATA_FRAME,
	AR_DATA_NO_SYNC,
	AR_DATA_BAD_LENGTH,
	AR_DATA_BAD_CRC,
};
struct ar_data_framing_stats {
	uint64_t input_bytes, calls, packet_bytes, packets, allocation_failures;
	uint64_t overflow_bytes, no_sync_bytes, bad_length_bytes, bad_crc_bytes;
};
struct ar_data_framing {
	uint8_t *ring;
	size_t capacity, read, write, used, scan;
	int header;
	struct ar_data_framing_stats stats;
};
/* A segment may wrap into two spans. Consume/copy synchronously; do not reenter
 * the parser or modify its storage. Return zero for accepted storage, nonzero
 * for allocation failure. Either result consumes the bytes, as in the original.
 * Error segments are separate from valid frames and must not be routed as such.
 * The callback is a packet-storage boundary, not evidence of final delivery. */
typedef int (*ar_data_segment_fn)(enum ar_data_segment_kind kind,
	const uint8_t *first, size_t first_length,
	const uint8_t *second, size_t second_length, void *context);

/* Caller owns and serializes ring/state. The matched mode-3 parser rings use
 * the sizes above; the socket's separate RX queue holds 1,024 bytes.
 * At least 258 bytes can hold every supported frame plus the reserved byte.
 * Smaller rings reproduce the original incomplete oversized-frame behavior.
 * Input must not overlap the ring. Invalid arguments return -10000. */
int ar_data_framing_init(struct ar_data_framing *state, uint8_t *ring,
	size_t capacity);
/* Return 0 after processing (possibly retaining a partial frame), or -9999
 * without accepting input if it would fill/overflow the ring. Reserving one
 * byte follows the original worker; the original direct parser accepts an
 * exactly full ring but then confuses its equal cursors with an empty ring. */
int ar_data_framing_feed(struct ar_data_framing *state, const uint8_t *data,
	size_t length, ar_data_segment_fn segment, void *context);
#endif
