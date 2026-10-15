/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_STREAM_H
#define AR_TX_STREAM_H
#include <stddef.h>
#include <stdint.h>

/* Source-only native analysis views, not original firmware declarations.
 * Address, callback, argument, mutex and signal values are opaque adapter tokens.
 * Queue cursors/capacity/used are bytes; retained entries occupy 20 bytes. */
struct ar_tx_stream_entry {
	uint32_t address, length;
	uint16_t produced, consumed;
	uint32_t argument, callback;
};
struct ar_tx_stream_queue {
	uint32_t capacity, write, read, used, pending, mutex, signal;
	struct ar_tx_stream_entry *entries;
	size_t entry_count;
};
struct ar_tx_stream {
	uint32_t references;
	uint8_t flags; /* Bit 2 selects retained entries instead of byte consumption. */
	struct ar_tx_stream_queue *queue;
};
struct ar_tx_stream_registry {
	uint32_t mutex;
	struct ar_tx_stream *streams[8][5][2];
};
struct ar_tx_stream_ops {
	int (*lock)(uint32_t mutex, uint32_t timeout, void *context);
	void (*unlock)(uint32_t mutex, void *context);
	void (*wake)(uint32_t signal, void *context);
	void (*buffer)(uint32_t callback, uint32_t argument, void *context);
};
/* Invalid selector/port or an absent stream is a no-op; any nonzero direction
 * selects direction 1. Lookup adds one reference and the consumer drops two,
 * each drop saturating at zero. The caller's reference producers remain under
 * comparison; the two drops alone do not establish their ownership contract.
 * Retained mode deducts requested bytes from pending modulo 2^32 even on short
 * consumption. Callback mutations are observed; adapters must serialize state
 * and keep the selected stream/queue alive through both reference drops, with
 * entry storage and its base pointer stable throughout the call.
 * Lock returns are ignored as in the compared original path; RTOS correctness
 * is not established. Missing adapters/unsafe entry metadata return -22 with
 * acquired locks released and references dropped; prior effects are not undone.
 * Does not imply hardware completion or peer acknowledgement. */
int ar_tx_stream_release(struct ar_tx_stream_registry *registry,
	unsigned selector, unsigned port, unsigned direction, uint32_t amount,
	const struct ar_tx_stream_ops *ops, void *context);
#endif
