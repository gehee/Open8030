/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_RETIRE_H
#define AR_TX_RETIRE_H
#include <stddef.h>
#include <stdint.h>

/* Source-only retained-descriptor ring. These native views are not original
 * firmware structures or addresses. Packet byte 7 is the descriptor count;
 * little-endian descriptor words start at byte 8. Storage remains caller-owned. */
struct ar_tx_retire_packet {
	uint8_t *data;
	size_t length;
};
struct ar_tx_retire_ring {
	uint32_t configuration;
	unsigned head, tail, owned;
	struct ar_tx_retire_packet packets[10];
};
/* The original release endpoint receives context bits 24..27, descriptor
 * bits 0..7, a zero direction and descriptor bits 12..31. Its return is ignored.
 * ar_tx_stream_release supplies the separately compared source-only consumer;
 * connecting it to producer ownership and the runtime remains separate work. */
typedef void (*ar_tx_retire_release)(unsigned selector, unsigned stream,
	unsigned direction, uint32_t length, void *context);

/* Retire oldest packets until owned <= retain. NULL ring is a no-op, matching
 * an absent original user context. Unlike the copy-only DEV API, retain may
 * exceed capacity: it then leaves a valid ring unchanged.
 * Call only after the hardware ownership/retention contract is met. No packet
 * bytes or ring pointers are cleared. Callbacks may mutate descriptors and
 * configuration; the packet view is held while its count/words are reloaded.
 * Caller serializes ring ownership. Invalid state/storage returns -22; earlier
 * releases are not undone. A failing packet remains owned, so do not blindly
 * retry after partial release. Native bounds guards are not original errors. */
int ar_tx_retire(struct ar_tx_retire_ring *ring, unsigned retain,
	ar_tx_retire_release release, void *context);
/* Reserve the head slot before packet preparation. Full ring returns -95 with
 * no change. A null selected packet returns -95 AFTER head/owned advance, as at
 * the original assertion boundary. Bad native arguments/head return -22.
 * This does not validate packet storage size or undo a later preparation error. */
int ar_tx_reserve(struct ar_tx_retire_ring *ring, struct ar_tx_retire_packet **packet);
#endif
