/* SPDX-License-Identifier: MIT */
#include "ar_channel_monitor.h"

static void complete(struct ar_channel_monitor_user *user, unsigned success,
	const struct ar_channel_monitor_ops *ops, void *context)
{
	uint8_t payload[16] = {0};
	user->flags &= (uint8_t)~0x40;
	payload[0] = success; payload[4] = 1; payload[8] = user->channel;
	(void)ops->post(0,0x109,payload,sizeof payload,UINT32_MAX,context);
}

int ar_channel_monitor_observe(struct ar_channel_monitor *state, unsigned user,
	uint32_t locked, const uint16_t *first, size_t count,
	const struct ar_channel_monitor_ops *ops, void *context)
{
	if (!state || user >= 10 || count > 255 || (!first && count) || !ops ||
		!ops->reset || !ops->field || !ops->post ||
		((state->policy_flags & 4) && (!ops->current || !ops->preferred ||
		!ops->next || !ops->last || !ops->hop || !ops->now_ms))) { return -22; }
	struct ar_channel_monitor_user *entry = &state->users[user];
	if (!(entry->flags & 1)) { return 0; }
	if (!(entry->flags & 2)) {
		unsigned streak = ((entry->flags >> 3) + 1) & 3;
		entry->flags &= (uint8_t)~0x18;
		if (!locked) { ops->reset(!!state->mode,user,context); return 0; }
		entry->flags |= streak << 3;
		if (streak > 1) { entry->flags = (entry->flags & 0xa5) | 2; }
	}
	if (locked) {
		if (entry->flags & 0x20) {
			entry->flags &= (uint8_t)~0x20;
			state->hold_mask &= (uint16_t)~(1u << user);
			ops->field((0x9a4+8*user) << 16 | 0x1801,0,context);
		}
		if (entry->flags & 0x40) {
			state->successes++;
			if (state->successes > 2) { state->successes = 0; complete(entry,1,ops,context); }
		} else { state->successes = 0; }
		entry->missed = 0;
	} else { state->successes = 0; }
	if (!(state->policy_flags & 4)) { return 0; }
	if (locked) { state->unlocks = 0; return 0; }
	state->unlocks++;
	if (state->unlocks <= 10 || (state->policy_flags & 8)) { return 0; }
	uint32_t current = ops->current(2+!!state->role,context);
	uint32_t preferred = ops->preferred(state->group,current & 255,context);
	uint16_t channel = preferred == 255 ? (uint16_t)current : (uint16_t)preferred;
	if (preferred == 255 || preferred == current) {
		if (state->unlocks <= 40) { return 0; }
		for (unsigned group = 0; group < count; group++) {
			if (ops->next(&channel,group,context)) { channel = first[group]; break; }
		}
		if (channel == current) {
			uint16_t low = 0, high = 0;
			(void)ops->next(&low,state->group,context);
			(void)ops->last(&high,state->group,context);
			/* Preserve signed comparison of wrapped RV32 subtraction. */
			uint32_t left = (uint32_t)high-current, right = current-low;
			unsigned choose_low = (left ^ 0x80000000u) < (right ^ 0x80000000u);
			channel = (choose_low ? low : high) & 255;
		}
	}
	state->hop_ms[state->band == 2] = ops->now_ms(context);
	state->unlocks = 0;
	ops->hop(channel,context);
	return 0;
}

int ar_channel_monitor_tick(struct ar_channel_monitor *state,
	const struct ar_channel_monitor_ops *ops, void *context)
{
	if (!state || !ops || !ops->reset || !ops->post) { return -22; }
	for (unsigned user = 0; user < 10; user++) {
		struct ar_channel_monitor_user *entry = &state->users[user];
		if (!(entry->flags & 1)) { continue; }
		uint8_t threshold = entry->threshold;
		entry->missed++;
		if ((entry->flags & 2) && !(entry->missed % 10) && state->role < 2 &&
			entry->missed > (state->role ? 50 : 19) &&
			(!state->reset_suppressed || state->reset_mode != 1)) {
			ops->reset(!!state->mode,user,context);
		}
		if ((entry->flags & 2) && (entry->flags & 0x40) && entry->missed == entry->deadline) {
			complete(entry,0,ops,context);
		}
		if ((entry->flags & 4) || entry->missed >= threshold) {
			if (ops->lost) { ops->lost(user,entry,context); }
			entry->flags &= (uint8_t)~0x46;
			entry->missed = 0;
		}
	}
	return 0;
}
