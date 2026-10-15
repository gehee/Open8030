/* SPDX-License-Identifier: MIT */
#ifndef AR_FCH_MONITOR_H
#define AR_FCH_MONITOR_H
#include <stdint.h>

/* One logical PHY user. The current DEV mode-0 port enables logical user8.
 * Configure/observe/poll are serialized by the cooperative radio task. */
struct ar_fch_monitor {
	uint64_t last_ms;
	uint8_t enabled, locked, forced, streak, missed, threshold, interval_ms;
};
#define AR_FCH_RESET_RX 1u
#define AR_FCH_LOST 2u
int ar_fch_monitor_init(struct ar_fch_monitor *monitor, uint32_t frame_us, uint64_t now_ms);
void ar_fch_monitor_enable(struct ar_fch_monitor *monitor, unsigned locked);
unsigned ar_fch_monitor_observe(struct ar_fch_monitor *monitor, unsigned locked);
void ar_fch_monitor_force(struct ar_fch_monitor *monitor);
unsigned ar_fch_monitor_poll(struct ar_fch_monitor *monitor, uint64_t now_ms);
#endif
