/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_ALLOC_H
#define AR_TX_ALLOC_H
#include "ar_tx_stream.h"

/* Matched original allocation-wrapper limit, not available/free heap bytes.
 * Native views are not original declarations: adapters must allocate native
 * metadata and storage, NOT allocate sizeof(original) for native structures.
 * The byte arguments record the original 12-byte object / 80-byte queue-header
 * requests for accounting and comparison. Queue allocation supplies stable
 * entries/entry_count/data_address mappings; creation preserves these mappings
 * and does not clear the data area. Every callback is required. */
#define AR_TX_ALLOC_LIMIT UINT32_C(0x8f980)
struct ar_tx_alloc_ops {
	struct ar_tx_stream *(*stream_alloc)(uint32_t original_bytes, void *context);
	struct ar_tx_stream_queue *(*queue_alloc)(uint32_t original_bytes, void *context);
	void (*stream_free)(struct ar_tx_stream *stream, void *context);
	uint32_t (*mutex_create)(void *context);
	uint32_t (*signal_create)(uint32_t maximum, uint32_t initial, void *context);
};
/* Source-only factory helpers. A byte queue requests capacity+80 bytes, strictly
 * below AR_TX_ALLOC_LIMIT, then initializes empty/enabled metadata and creates
 * a mutex, ready signal and space signal in that order. Signal arguments are
 * (1,0). Zero synchronization tokens are retained without rollback, matching
 * the original factory; this does not establish usable synchronization objects.
 * Stream creation initializes references=0, percentage=100 and flags=0, then
 * creates the queue. Any nonzero retained argument selects a 100-byte queue
 * (five 20-byte entries) and sets stream flag bit 2. Queue allocation failure
 * frees the stream. Missing adapters return NULL. Native capacity overflow is
 * rejected rather than reproducing original wrapped, undersized allocation.
 * Allocator internals, RTOS behavior and device integration remain separate. */
struct ar_tx_stream_queue *ar_tx_queue_create(uint32_t capacity,
	const struct ar_tx_alloc_ops *ops, void *context);
struct ar_tx_stream *ar_tx_stream_create(uint32_t capacity, unsigned retained,
	const struct ar_tx_alloc_ops *ops, void *context);
#endif
