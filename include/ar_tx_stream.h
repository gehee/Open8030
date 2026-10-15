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
	uint32_t flags, acquire, available, data_address, ready_signal;
	uint64_t acquired_bytes;
	uint32_t high_water, short_writes;
	uint64_t written_bytes;
};
struct ar_tx_stream {
	uint32_t references;
	uint8_t flags; /* Bit 2 selects retained entries instead of byte consumption. */
	struct ar_tx_stream_queue *queue;
	uint8_t percentage;
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
struct ar_tx_stream_span { uint32_t address, length; };
struct ar_tx_stream_acquire_ops {
	int (*lock)(uint32_t mutex, uint32_t timeout, void *context);
	void (*unlock)(uint32_t mutex, void *context);
	uint64_t (*clock_us)(void *context);
	int (*wait)(uint32_t signal, uint32_t timeout_ms, void *context);
};
struct ar_tx_stream_write_ops {
	int (*lock)(uint32_t mutex, uint32_t timeout, void *context);
	void (*unlock)(uint32_t mutex, void *context);
	void (*wake)(uint32_t signal, void *context);
	uint64_t (*clock_us)(void *context);
	int (*wait)(uint32_t signal, uint32_t timeout_ms, void *context);
};
/* Source-only queue producer. Storage is a stable native mapping of the queue's
 * data area; it must cover capacity bytes and must not overlap input. Retained
 * entry serialization belongs to the caller. Full writes add pending_increment;
 * partial writes still publish available bytes, update counters and wake readers.
 * discard_available removes unacquired bytes before the space check, preserving
 * acquired bytes. require_space returns -12 without copying if the whole request
 * does not fit, but preceding discard effects remain. Disabled queues return -10,
 * even after a partial write. Null queue/input or zero amount returns -6.
 * Zero timeout returns immediately; any signed-negative timeout retries 100-ms
 * waits indefinitely. Positive deadlines use signed 32-bit deltas and waits of
 * at most 100 ms. Lock/wait results are ignored. Adapters must serialize state,
 * keep storage live and provide eventual clock/wait progress. Unsafe native
 * metadata or missing adapters returns -22, not an original firmware result.
 * This does not establish RTOS scheduling, DMA ownership or runtime integration. */
int ar_tx_queue_write(struct ar_tx_stream_queue *queue, uint8_t *storage,
	size_t storage_size, const uint8_t *input, uint32_t amount,
	uint32_t timeout_ms, unsigned require_space, unsigned discard_available,
	uint32_t pending_increment, const struct ar_tx_stream_write_ops *ops,
	void *context);
/* Source-only comparison of acquisition. Budget is the wrapped 32-bit product
 * of amount and percentage divided by 100, with a minimum of one. Nonempty
 * output retains the lookup reference; a corresponding release consumes it.
 * Retained mode writes five spans, byte mode two (remaining output untouched).
 * Zero amount returns -1 without touching output; absent stream returns -1
 * after clearing two spans. Empty successful output drops the lookup reference.
 * Byte mode uses queue flag bit 1, acquire/available and acquired_bytes; retained
 * mode advances entry.produced, without changing pending. Timeout UINT32_MAX
 * retries 100-ms waits indefinitely; finite deadlines use signed 32-bit deltas.
 * Lock/wait results are ignored, as in the original. Adapters must provide
 * serialization, live storage, and eventual clock/wait progress. Invalid native
 * pointers or unsafe queue metadata return -22; previous effects are not undone.
 * No RTOS/DMA concurrency or hardware transmission is established by this API. */
int ar_tx_stream_acquire(struct ar_tx_stream_registry *registry,
	unsigned selector, unsigned port, unsigned direction, uint32_t amount,
	struct ar_tx_stream_span output[5], uint32_t timeout_ms,
	const struct ar_tx_stream_acquire_ops *ops, void *context);
/* Prepared-packet availability scan: sum retained length-produced modulo 2^32,
 * or read byte-ring available; a negative signed result becomes zero. Lookup
 * and drop bracket the queue lock. This does not reserve the reported bytes. */
int ar_tx_stream_available(struct ar_tx_stream_registry *registry,
	unsigned selector, unsigned port, uint32_t *available,
	const struct ar_tx_stream_acquire_ops *ops, void *context);
/* Invalid selector/port or an absent stream is a no-op; any nonzero direction
 * selects direction 1. Lookup adds one reference and the consumer drops two,
 * each drop saturating at zero. Acquisition holds one reference when it returns
 * nonempty spans. This consumer drops that and its own lookup reference.
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
