/* SPDX-License-Identifier: MIT */
#ifndef AR_PHY_RECEIVE_H
#define AR_PHY_RECEIVE_H
#include "ar_phy_control.h"

struct ar_phy_receive {
	uint8_t mac[4], position, slot, dev_state;
	uint16_t dev_flags;
	uint8_t retransmit_ready, retransmit_flags, retransmit_phase;
};
enum ar_phy_receive_lock { AR_PHY_RECEIVE_CONTROL, AR_PHY_RECEIVE_RETRANSMIT, AR_PHY_RECEIVE_DEV };
struct ar_phy_receive_ops {
	void (*lock)(enum ar_phy_receive_lock which, void *context);
	void (*unlock)(enum ar_phy_receive_lock which, void *context);
	int (*read)(uint8_t packet[7], void *context);
	void (*prepare)(void *context);
	ar_channel_field field;
	void (*dispatch)(unsigned command, uint8_t *payload, size_t length, void *context);
	void (*stop_timer)(void *context);
	void (*start_timer)(uint32_t delay_ms, void *context);
	int (*admit)(const uint8_t record[150], void *context);
	uint32_t (*now_ms)(void *context);
};
/* Original receive validation, cache replacement and synchronization decisions.
 * All callbacks are required and run under the control lock; mode3 nests the
 * retransmit and DEV locks separately. prepare supplies a zeroed retransmit
 * context, represented here by caller-owned state. Admission consumes the
 * synthesized control record; actual link admission remains callback-owned.
 * Return0 accepts, -74 rejects checksum, -22 rejects format/storage, or propagate
 * nonzero read status. The original silently returns on these rejection paths.
 * Runtime integration and physical read/timer/callback effects remain pending. */
int ar_phy_receive_process(struct ar_phy_control *control,
	struct ar_phy_receive *receiver, const struct ar_phy_receive_ops *ops, void *context);
#endif
