/* SPDX-License-Identifier: MIT */
#include "ar_fch_monitor.h"
#include <string.h>

int ar_fch_monitor_init(struct ar_fch_monitor *m, uint32_t frame_us, uint64_t now_ms)
{
	if (!m) { return -22; }
	/* 231718/231b06 truncate the threshold to a byte BEFORE its upper cap.
	 * The periodic DEV timer instead rounds frame duration (22caac). */
	uint32_t frame_ms = frame_us / 1000;
	uint32_t threshold = frame_ms ? 1000 / frame_ms : UINT32_MAX;
	if (threshold < 8) { threshold = 8; }
	threshold = (uint8_t)threshold;
	if (threshold > 248) { threshold = 248; }
	uint64_t interval = ((uint64_t)frame_us + 500) / 1000;
	/* A zero or wrapped scheduling byte is outside the native profile. */
	if (!interval || interval > 255) { return -95; }
	memset(m, 0, sizeof *m);
	m->threshold = threshold; m->interval_ms = interval; m->last_ms = now_ms;
	return 0;
}
void ar_fch_monitor_enable(struct ar_fch_monitor *m, unsigned locked)
{
	if (!m->enabled) {
		m->enabled = 1; m->locked = !!locked; m->missed = m->streak = m->forced = 0;
	}
}
unsigned ar_fch_monitor_observe(struct ar_fch_monitor *m, unsigned locked)
{
	if (!m->enabled) { return 0; }
	if (!m->locked) {
		if (!locked) { m->streak = 0; return AR_FCH_RESET_RX; }
		if (++m->streak >= 2) { m->locked = 1; m->streak = 0; }
	}
	if (locked) { m->missed = 0; }
	return 0;
}
void ar_fch_monitor_force(struct ar_fch_monitor *m)
{
	if (m->enabled && m->locked) { m->forced = 1; }
}
unsigned ar_fch_monitor_poll(struct ar_fch_monitor *m, uint64_t now_ms)
{
	if (!m->interval_ms || now_ms - m->last_ms < m->interval_ms) { return 0; }
	/* The original handler rearms its timer after processing. A delayed poll
	 * advances once; never manufacture a burst of missed PHY observations. */
	m->last_ms = now_ms;
	if (!m->enabled) { return 0; }
	m->missed++;
	unsigned action = m->locked && m->missed > 50 && !(m->missed % 10) ? AR_FCH_RESET_RX : 0;
	if (m->forced || m->missed >= m->threshold) {
		if (m->locked) { action |= AR_FCH_LOST; }
		m->forced = m->locked = m->missed = 0;
	}
	return action;
}
