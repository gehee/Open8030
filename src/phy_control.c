/* SPDX-License-Identifier: MIT */
#include "ar_phy_control.h"
#include "ar_frame_stamp.h"
#include "ar_mac.h"
#include <string.h>

uint8_t ar_phy_control_checksum(const uint8_t packet[7])
{
	uint8_t crc = 0;
	for (unsigned i = 1; i < 7; i++) {
		crc = ar_mac_crc8_step(crc ^ (i == 6 ? packet[i] | 0xf0 : packet[i]));
	}
	return crc;
}
static unsigned ttl(const uint8_t packet[7]) { return (packet[1] >> 4) | (packet[2] & 1) << 4; }
static void checksum(struct ar_phy_control *state)
{
	uint8_t value = ar_phy_control_checksum(state->tx);
	state->tx[0] = state->mode == 3 ? (state->tx[0] & 0xf0) | (value & 15) : value;
}
static int valid(const struct ar_phy_control *state, const struct ar_phy_control_ops *ops)
{
	return state && ops && ops->lock && ops->unlock && ops->field && ops->write &&
		ops->dispatch && ops->idle;
}
static void wake(const struct ar_phy_control *state, unsigned value,
	const struct ar_phy_control_ops *ops, void *context)
{
	if (state->wake) { ops->field(0x0d301008,value,context); }
}
static void clear(struct ar_phy_control *state, const struct ar_phy_control_ops *ops, void *context)
{
	state->tx[1] &= 0xf0;
	wake(state,0,ops,context);
}
int ar_phy_control_enqueue(struct ar_phy_control *state, unsigned command,
	unsigned life, const uint8_t *payload, size_t length,
	const struct ar_phy_control_ops *ops, void *context)
{
	if (!valid(state,ops) || command > 9 || life > 31 || length > 4 || (!payload && length)) { return -22; }
	ops->lock(context);
	int result = -1;
	if (!(state->tx[1] & 15)) {
		state->tx[1] = (life << 4) | command;
		state->tx[2] = (state->tx[2] & 0xf0) | (length << 1) | (life >> 4);
		if (length) { memcpy(state->tx+3,payload,length); }
		wake(state,255,ops,context);
		result = 0;
	}
	ops->unlock(context);
	return result;
}
int ar_phy_control_tick(struct ar_phy_control *state, uint64_t now_ms,
	const struct ar_phy_control_ops *ops, void *context)
{
	if (!valid(state,ops) || ((state->tx[1] & 15) > 9 && (state->tx[1] & 15) != AR_PHY_STAMP &&
	    (state->tx[1] & 15) != AR_PHY_RETRIES) ||
	    ((state->tx[2] >> 1) & 7) > 4) { return -22; }
	ops->lock(context);
	unsigned command = state->tx[1] & 15, life = ttl(state->tx);
	if (!command) {
		uint32_t period = state->time_period_ms ? state->time_period_ms : 60001u;
		if (state->role) { goto done; }
		if (!state->time_valid && now_ms-state->time_value < period) {
			/* Between the AP time's messages: Open8030's frame stamp, packed
			 * as command 7's ms (32 bits); its resend map every few frames
			 * when no stamp is due. */
			const int stamp_due = state->stamp_period_ms && now_ms - state->stamp_ms >= state->stamp_period_ms;
			if (!stamp_due && state->retry_every &&
			    (uint8_t)(state->retry_frame - state->retry_sent_frame) >= state->retry_every) {
				state->retry_sent_frame = state->retry_frame;
				wake(state,255,ops,context);
				for (unsigned i = 0; i < 3; i++) { state->tx[3+i] = (uint8_t)(state->retry_map >> (8*i)); }
				state->tx[6] = state->retry_frame & 15;
				state->tx[1] = (uint8_t)((state->retry_frame >> 4) << 4 | AR_PHY_RETRIES);
				state->tx[2] = (state->tx[2] & 0xf0) | 8;
				checksum(state); ops->write(state->tx,context);
				goto done;
			}
			if (!stamp_due) { goto done; }
			state->stamp_ms = (uint32_t)now_ms; state->stamps++;
			wake(state,255,ops,context);
			const uint32_t stamp = (uint32_t)state->stamp_us;
			for (unsigned i = 0; i < 3; i++) { state->tx[3+i] = (uint8_t)(stamp >> (8*i)); }
			state->tx[6] = (uint8_t)((stamp >> 24) & 15);
			state->tx[1] = (uint8_t)((stamp >> 28) << 4 | AR_PHY_STAMP);
			state->tx[2] = (state->tx[2] & 0xf0) | 8;
			checksum(state); ops->write(state->tx,context);
			goto done;
		}
		/* The lead is the frame's (taken again each time: 40 MHz and the short
		 * frame change it). */
		state->lead_ms = (uint8_t)((state->frame_us+1100u)/1000);
		state->time_value = (uint32_t)now_ms; state->time_valid = 0;
		uint32_t remote = state->time_value + state->lead_ms;
		wake(state,255,ops,context);
		for (unsigned i = 0; i < 4; i++) { state->tx[3+i] = remote >> (8*i); }
		state->tx[1] = (remote >> 28) << 4 | 7;
		state->tx[2] = (state->tx[2] & 0xf0) | 8;
		checksum(state); ops->write(state->tx,context);
	} else if (command == 7 || command == AR_PHY_STAMP || command == AR_PHY_RETRIES || life == 31) {
		if (command == 7 || command == AR_PHY_STAMP || command == AR_PHY_RETRIES) { clear(state,ops,context); }
		checksum(state); ops->write(state->tx,context);
	} else if (life) {
		checksum(state); ops->write(state->tx,context);
		life = (ttl(state->tx)+31) & 31;
		state->tx[1] = (state->tx[1] & 15) | (life << 4);
		state->tx[2] = (state->tx[2] & 0xfe) | (life >> 4);
	} else {
		if (state->callbacks & (1u << command)) {
			ops->dispatch(1,command,state->tx+3,(state->tx[2] >> 1) & 7,context);
		}
		clear(state,ops,context);
		if (!state->role && state->mode == 3) { ops->idle(state->tx,context); }
		/* Original zero-TTL path writes the retained checksum here. */
		ops->write(state->tx,context);
	}
done:
	state->ticks++;
	ops->unlock(context);
	return 0;
}
int ar_phy_control_cancel(struct ar_phy_control *state,
	const struct ar_phy_control_ops *ops, void *context)
{
	if (!valid(state,ops)) { return -22; }
	ops->lock(context);
	if (state->tx[1] & 15) {
		clear(state,ops,context); checksum(state); ops->write(state->tx,context);
	}
	ops->unlock(context);
	return 0;
}
int ar_phy_control_expire(struct ar_phy_control *state,
	const struct ar_phy_control_ops *ops, void *context)
{
	if (!valid(state,ops)) { return -22; }
	ops->lock(context);
	unsigned command = state->rx[1] & 15;
	if (ttl(state->rx) && command < 10 && (state->callbacks & (1u << command)) &&
		((state->rx[2] >> 1) & 7) > 4) { ops->unlock(context); return -22; }
	if (ttl(state->rx) && command < 10 && (state->callbacks & (1u << command))) {
		ops->dispatch(0,command,state->rx+3,(state->rx[2] >> 1) & 7,context);
	}
	ops->unlock(context);
	return 0;
}
