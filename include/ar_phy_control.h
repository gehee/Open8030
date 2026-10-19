/* SPDX-License-Identifier: MIT */
#ifndef AR_PHY_CONTROL_H
#define AR_PHY_CONTROL_H
#include "ar_channel_work.h"

struct ar_phy_control {
	uint8_t tx[7], rx[7];
	uint32_t time_value, frame_us;
	uint16_t callbacks;
	uint8_t role, mode, wake, ticks, time_valid, lead_ms;
	/* The AP time (command 7) goes when the channel is free, at most once per
	 * period: 0 is stock's 60001 ms. */
	uint16_t time_period_ms;
	/* Open8030's frame stamp (command 10, ar_frame_stamp.h), between them: the
	 * AP's stamp_us (36 bits) when the channel is free, at most once per
	 * stamp_period_ms (0: none). stamps counts those sent. */
	uint64_t stamp_us;
	uint32_t stamp_ms, stamps;
	uint16_t stamp_period_ms;
};
struct ar_phy_control_ops {
	void (*lock)(void *context);
	void (*unlock)(void *context);
	ar_channel_field field;
	void (*write)(const uint8_t packet[7], void *context);
	void (*dispatch)(unsigned outgoing, unsigned command, uint8_t *payload,
		size_t length, void *context);
	/* AP mode3 idle refresh owns the complete packet, including checksum. */
	void (*idle)(uint8_t packet[7], void *context);
};
uint8_t ar_phy_control_checksum(const uint8_t packet[7]);
/* Single pending slot: -1 means busy (a frame stamp holds it one tick). Native -22 rejects unsafe storage,
 * command >9, TTL >31 or length >4 before effects; original enqueue lacks
 * these guards. All ops callbacks are required. Unwritten payload tails are
 * retained and checksummed. */
int ar_phy_control_enqueue(struct ar_phy_control *state, unsigned command,
	unsigned ttl, const uint8_t *payload, size_t length,
	const struct ar_phy_control_ops *ops, void *context);
/* Caller supplies a sampled 64-bit millisecond clock. Sending precedes TTL
 * decrement; local dispatch occurs on a later tick at TTL0. TTL31 persists.
 * All callbacks run under the control lock. Hardware writes, callback bodies,
 * receive validation/cache replacement and scheduling remain caller-owned. */
int ar_phy_control_tick(struct ar_phy_control *state, uint64_t now_ms,
	const struct ar_phy_control_ops *ops, void *context);
int ar_phy_control_cancel(struct ar_phy_control *state,
	const struct ar_phy_control_ops *ops, void *context);
int ar_phy_control_expire(struct ar_phy_control *state,
	const struct ar_phy_control_ops *ops, void *context);
#endif
