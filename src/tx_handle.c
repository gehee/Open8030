/* SPDX-License-Identifier: MIT */
#include "ar_tx_handle.h"

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
