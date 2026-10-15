/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_HANDLE_H
#define AR_TX_HANDLE_H
#include "ar_tx_stream.h"

/* Source-only view of the handle-table contract, not an original declaration.
 * A record has LE flags at 0, selector at 4 and port at 8; all 12 bytes are
 * preserved on reads. Flag bit 0 means active, bits 1/2 select RX/TX at close. */
struct ar_tx_handle_record { uint8_t bytes[12]; };
struct ar_tx_handles {
	uint32_t mutex, count;
	struct ar_tx_handle_record *records;
	size_t record_count; /* Native storage bound, separate from original count. */
};
/* Out-of-range handles return -1 without touching output or taking a lock.
 * Otherwise copy the record under the table mutex, optionally clear its active
 * bit, unlock, and return 0/-1 from the COPIED active bit. Output must not overlap
 * table storage. Lock returns are ignored. Missing native adapters/storage or
 * out-of-bounds native metadata return -22, not an original firmware error. */
int ar_tx_handle_read(struct ar_tx_handles *handles, uint32_t handle,
	struct ar_tx_handle_record *output, unsigned keep_active,
	const struct ar_tx_stream_close_ops *ops, void *context);
/* Keep the handle active while closing its selected streams, then re-read and
 * clear it only after successful stream close. Propagate lookup/close/final-read
 * results. A failed final read does not restore destroyed streams. Table and
 * selector storage must stay valid throughout callbacks/delays; serialize all
 * mutations. This does not stop host socket workers or implement the full host
 * close request, and does not establish concurrent RTOS or DMA behavior. */
int ar_tx_handle_close(struct ar_tx_handles *handles, uint32_t handle,
	struct ar_tx_stream_registry *registry,
	const struct ar_tx_stream_close_ops *ops, void *context);
#endif
