/* SPDX-License-Identifier: MIT */
#ifndef AR_CHANNEL_MONITOR_H
#define AR_CHANNEL_MONITOR_H
#include "ar_channel_work.h"

struct ar_channel_monitor_user {
	uint8_t flags, threshold, missed, channel, deadline;
};
struct ar_channel_monitor {
	struct ar_channel_monitor_user users[10];
	uint32_t hop_ms[2];
	uint16_t hold_mask;
	uint8_t successes, unlocks, role, mode, policy_flags, group, band;
	uint8_t reset_suppressed, reset_mode;
};
struct ar_channel_monitor_ops {
	void (*reset)(unsigned per_user, unsigned user, void *context);
	ar_channel_field field;
	ar_channel_post post;
	/* Optional loss callback may update the user before timer cleanup. */
	void (*lost)(unsigned user, struct ar_channel_monitor_user *state, void *context);
	uint32_t (*current)(unsigned selector, void *context);
	uint32_t (*preferred)(unsigned group, unsigned channel, void *context);
	int (*next)(uint16_t *channel, unsigned group, void *context);
	int (*last)(uint16_t *channel, unsigned group, void *context);
	void (*hop)(uint16_t channel, void *context);
	uint32_t (*now_ms)(void *context);
};
/* Serialized monitor state transitions. Flags retain original enabled/locked,
 * forced, two-bit streak, band-hold and pending-completion bits. Success and
 * unlock counters are shared by all users. first[] supplies each group's first
 * channel for blind hopping. Native storage errors return -22 before effects.
 * Hardware callbacks, timer scheduling and diagnostic history collection are
 * caller responsibilities; these helpers are not yet integrated into runtime. */
int ar_channel_monitor_observe(struct ar_channel_monitor *state, unsigned user,
	uint32_t locked, const uint16_t *first, size_t count,
	const struct ar_channel_monitor_ops *ops, void *context);
int ar_channel_monitor_tick(struct ar_channel_monitor *state,
	const struct ar_channel_monitor_ops *ops, void *context);
#endif
