/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_ACTION_H
#define AR_DATA_ACTION_H
#include <stddef.h>
#include <stdint.h>

/* Source-only worker transformations. These are native storage/profile views,
 * not original packet or interface layouts. Caller owns exclusive mutable
 * storage and keeps profile/prefix data separate from it. */
struct ar_data_action_buffer { uint8_t *storage; size_t capacity, head, tail; };
struct ar_data_action_profile {
	uint8_t custom;
	uint32_t head_bytes, tail_bytes, prefix_length;
	const uint8_t *prefix;
};
struct ar_data_action_ops {
	/* Null means the source interface could not be found; that action is skipped.
	 * The lookup is repeated for each action that uses the source profile. */
	const struct ar_data_action_profile *(*source)(uint16_t id, void *context);
};

/* Apply metadata override (0x100), then transformations in bit order:
 * 0x01 strips recognized source framing, 0x02 adds the internal envelope,
 * 0x04 strips configured custom framing and adds the internal envelope.
 * Other flags are handled elsewhere. No packet references are acquired/released.
 * Return 0 after processing, -22 for native storage/profile/headroom guards.
 * Earlier mutations remain if a later guard fails. An oversized individual
 * head/tail trim is ignored, following the original cursor helpers.
 * Internal length/checksum use only the low 16 bits of the current data length;
 * the checksum sums that many bytes modulo 65536. */
int ar_data_action_apply(struct ar_data_action_buffer *, uint64_t actions,
	uint16_t source, uint8_t *metadata, uint8_t override,
	const struct ar_data_action_ops *, void *context);
#endif
