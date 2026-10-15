/* SPDX-License-Identifier: MIT */
#include "ar_ap_time.h"

void ar_ap_time_reset(struct ar_ap_time *time)
{
	time->offset_ms = 0;
	time->valid = 0;
}

void ar_ap_time_receive(struct ar_ap_time *time, const uint8_t control[7], uint32_t local_ms)
{
	/* Matched original 0x248dc6..0x248dea: payload high nibble is ignored.
	 * Header bits normally used for TTL instead supply timestamp bits28..31;
	 * the fifth TTL bit is discarded by the 32-bit shift. */
	uint32_t remote_ms = control[3] | (uint32_t)control[4] << 8 |
		(uint32_t)control[5] << 16 | (uint32_t)(control[6] & 15) << 24 |
		(uint32_t)(control[1] & 0xf0) << 24;
	time->offset_ms = remote_ms - local_ms;
	time->valid = 1;
}

uint32_t ar_ap_time_get(const struct ar_ap_time *time, uint32_t local_ms)
{
	/* 0x229b14: DEV leaves the dispatcher's -1 sentinel unchanged until
	 * an offset is valid. Both subtraction and addition wrap at 32 bits. */
	return time->valid ? local_ms + time->offset_ms : UINT32_MAX;
}
