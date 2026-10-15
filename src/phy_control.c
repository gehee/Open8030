/* SPDX-License-Identifier: MIT */
#include "ar_phy_control.h"
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
	if (!valid(state,ops) || (state->tx[1] & 15) > 9 || ((state->tx[2] >> 1) & 7) > 4) { return -22; }
	ops->lock(context);
	unsigned command = state->tx[1] & 15, life = ttl(state->tx);
	if (!command) {
		if (state->role || (!state->time_valid && now_ms-state->time_value < 60001)) { goto done; }
		if (!state->lead_ms) { state->lead_ms = (uint32_t)(state->frame_us+1100u)/1000; }
		state->time_value = (uint32_t)now_ms; state->time_valid = 0;
		uint32_t remote = state->time_value + state->lead_ms;
		wake(state,255,ops,context);
		for (unsigned i = 0; i < 4; i++) { state->tx[3+i] = remote >> (8*i); }
		state->tx[1] = (remote >> 28) << 4 | 7;
		state->tx[2] = (state->tx[2] & 0xf0) | 8;
		checksum(state); ops->write(state->tx,context);
	} else if (command == 7 || life == 31) {
		if (command == 7) { clear(state,ops,context); }
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
