/* SPDX-License-Identifier: MIT */
#include "ar_data_action.h"
#include <string.h>

static const uint8_t marker[] = {0xff, 0xa5, 0xaa, 0x5a, 0xff};
static void trim(struct ar_data_action_buffer *buffer, uint32_t head, uint32_t tail)
{
	if (head <= buffer->tail - buffer->head) { buffer->head += head; }
	if (tail <= buffer->tail - buffer->head) { buffer->tail -= tail; }
}
static int wrap(struct ar_data_action_buffer *buffer, uint8_t metadata)
{
	if (buffer->head < 15) { return -22; }
	uint16_t length = (uint16_t)(buffer->tail - buffer->head), sum = 0;
	for (uint32_t i = 0; i < length; ++i) { sum += buffer->storage[buffer->head + i]; }
	buffer->head -= 15;
	uint8_t *out = buffer->storage + buffer->head;
	memcpy(out, marker, sizeof marker);
	out[5] = (uint8_t)length; out[6] = (uint8_t)(length >> 8);
	out[7] = metadata;
	memset(out + 8, 0, 5);
	out[13] = (uint8_t)sum; out[14] = (uint8_t)(sum >> 8);
	return 0;
}
int ar_data_action_apply(struct ar_data_action_buffer *buffer, uint64_t actions,
	uint16_t source, uint8_t *metadata, uint8_t override,
	const struct ar_data_action_ops *ops, void *context)
{
	if (!buffer || !buffer->storage || !metadata || buffer->head > buffer->tail ||
		buffer->tail > buffer->capacity || ((actions & 5) && (!ops || !ops->source))) { return -22; }
	if (actions & 0x100) { *metadata = override; }
	if (actions & 1) {
		const struct ar_data_action_profile *profile = ops->source(source, context);
		if (profile) {
			const uint8_t *prefix = profile->custom ? profile->prefix : marker;
			size_t length = profile->custom ? profile->prefix_length : sizeof marker;
			if (length > buffer->tail - buffer->head || (length && !prefix)) { return -22; }
			if (!length || !memcmp(buffer->storage + buffer->head, prefix, length)) {
				trim(buffer, profile->custom ? profile->head_bytes : 15,
					profile->custom ? profile->tail_bytes : 0);
			}
		}
	}
	if (actions & 2) {
		int result = wrap(buffer, *metadata);
		if (result) { return result; }
	}
	if (actions & 4) {
		const struct ar_data_action_profile *profile = ops->source(source, context);
		if (profile && profile->custom) {
			trim(buffer, profile->head_bytes, profile->tail_bytes);
			return wrap(buffer, *metadata);
		}
	}
	return 0;
}
