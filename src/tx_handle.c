/* SPDX-License-Identifier: MIT */
#include "ar_tx_handle.h"

int ar_tx_handle_open(struct ar_tx_handles *handles, unsigned role,
	unsigned selector, unsigned host_port, unsigned flags,
	uint32_t tx_capacity, uint32_t rx_capacity,
	struct ar_tx_stream_registry *registry,
	const struct ar_tx_stream_open_ops *open_ops,
	const struct ar_tx_stream_close_ops *close_ops, void *context)
{
	if (!handles || !registry || !open_ops || !open_ops->lock ||
	    !open_ops->unlock || !open_ops->create || !open_ops->destroy ||
	    !close_ops || !close_ops->lock || !close_ops->unlock ||
	    !close_ops->destroy_sync || !close_ops->free_queue ||
	    !close_ops->free_stream || !close_ops->delay)
		return -22;
	if ((role ? selector != 0 : selector >= 8) || host_port >= 4)
		return -6;
	unsigned port = host_port + 1;
	int result = ar_tx_stream_open(registry, selector, port, flags,
		tx_capacity, rx_capacity, open_ops, context);
	if (result)
		return result;
	(void)open_ops->lock(handles->mutex, UINT32_MAX, context);
	struct ar_tx_handle_record *record = NULL;
	result = -11;
	if (handles->count > handles->record_count ||
	    (handles->count && !handles->records)) {
		result = -22;
	} else {
		for (uint32_t i = 0; i < handles->count; i++) {
			if (!(handles->records[i].bytes[0] & 1)) {
				record = &handles->records[i];
				record->bytes[0] |= 1;
				break;
			}
		}
	}
	open_ops->unlock(handles->mutex, context);
	if (!record) {
		(void)ar_tx_stream_close(registry, selector, port, flags, close_ops, context);
		return result;
	}
	record->bytes[0] = (record->bytes[0] & ~6u) | ((flags & 3) << 1);
	uint32_t word = (uint32_t)record->bytes[0] | (uint32_t)record->bytes[1] << 8 |
		(uint32_t)record->bytes[2] << 16 | (uint32_t)record->bytes[3] << 24;
	for (unsigned i = 0; i < 4; i++)
		record->bytes[4 + i] = selector >> (i * 8);
	record->bytes[8] = port;
	return (word >> 3) & 0x1fffu;
}

int ar_tx_handle_read(struct ar_tx_handles *handles, uint32_t handle,
	struct ar_tx_handle_record *output, unsigned keep_active,
	const struct ar_tx_stream_close_ops *ops, void *context)
{
	if (!handles || !output || !ops || !ops->lock || !ops->unlock)
		return -22;
	if (handle >= handles->count)
		return -1;
	(void)ops->lock(handles->mutex, UINT32_MAX, context);
	if (!handles->records || handle >= handles->record_count) {
		ops->unlock(handles->mutex, context);
		return -22;
	}
	*output = handles->records[handle];
	if (!keep_active)
		handles->records[handle].bytes[0] &= ~1u;
	ops->unlock(handles->mutex, context);
	return (output->bytes[0] & 1) ? 0 : -1;
}

int ar_tx_handle_close(struct ar_tx_handles *handles, uint32_t handle,
	struct ar_tx_stream_registry *registry,
	const struct ar_tx_stream_close_ops *ops, void *context)
{
	struct ar_tx_handle_record record;
	int result = ar_tx_handle_read(handles, handle, &record, 1, ops, context);
	if (result)
		return result;
	uint32_t selector = (uint32_t)record.bytes[4] | (uint32_t)record.bytes[5] << 8 |
		(uint32_t)record.bytes[6] << 16 | (uint32_t)record.bytes[7] << 24;
	result = ar_tx_stream_close(registry, selector, record.bytes[8],
		(record.bytes[0] >> 1) & 3, ops, context);
	if (result)
		return result;
	return ar_tx_handle_read(handles, handle, &record, 0, ops, context);
}
