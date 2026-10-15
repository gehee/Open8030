/* SPDX-License-Identifier: MIT */
#ifndef AR_AP_TIME_H
#define AR_AP_TIME_H
#include <stdint.h>

/* DEV-side estimate of the AP's wrapping 32-bit millisecond clock. */
struct ar_ap_time {
	uint32_t offset_ms;
	uint8_t valid;
};
void ar_ap_time_reset(struct ar_ap_time *time);
/* Caller has validated the seven-byte PHY checksum, command7 and length4.
 * Use the same continuous local clock for reception and getter calls. */
void ar_ap_time_receive(struct ar_ap_time *time, const uint8_t control[7], uint32_t local_ms);
/* Original GET_AP_TIME returns success with UINT32_MAX until synchronized. */
uint32_t ar_ap_time_get(const struct ar_ap_time *time, uint32_t local_ms);
#endif
