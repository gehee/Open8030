/* SPDX-License-Identifier: MIT */
#ifndef AR_INTERLEAVER_H
#define AR_INTERLEAVER_H
#include <stdint.h>
/* Source-only interleaver service. These are native analysis interfaces;
 * runtime ownership, callbacks and IRQ timing still require integration. */
struct ar_interleaver_user {
	uint8_t mcs, previous_mcs, active, reserved3;
	uint32_t history;
	uint8_t retries, limit, drain, reserved11;
};
struct ar_interleaver {
	struct ar_interleaver_user users[12];
	uint8_t initialized, correction_pending;
	int16_t initial_delta, previous_delta;
	uint16_t reserved;
};
/* Snapshot inputs retain the separately observed first/second hardware user.
 * request is F64; timestamps are E5C/E60/E64 sampled while capture is held.
 * Snapshots do not establish physical read/interrupt ordering. */
struct ar_interleaver_input {
	uint32_t request, timestamps[3];
	uint8_t user, second_user, high_clock, distance_enabled, distance_disabled;
};
struct ar_interleaver_ops {
	void (*field)(uint32_t field, uint32_t value, void *context);
	uint32_t (*exchange)(uint32_t field, uint32_t value, void *context);
	uint32_t (*read)(uint32_t field, void *context);
	void (*reset)(void *context);
	void (*control)(unsigned user, unsigned direction, unsigned value, void *context);
	void (*apply)(unsigned user, unsigned mcs, void *context);
	void (*retry_reset)(unsigned user, void *context);
	/* Original queue 1/message 0x202, length 8, flags 0; errors ignored.
	 * Bytes 6/7 are deliberately zeroed instead of exposing stack contents. */
	void (*post)(const uint8_t payload[8], void *context);
	void (*notify)(unsigned source, const uint8_t payload[8], void *context);
};
/* Caller serializes state. Callback mutations are visible to subsequent work.
 * Invalid source/user or missing callbacks return -22 before any effects. */
int ar_interleaver_service(struct ar_interleaver *state,
	const struct ar_interleaver_input *input, unsigned source,
	const struct ar_interleaver_ops *ops, void *context);
#endif
