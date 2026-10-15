/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_MCS_H
#define AR_TX_MCS_H
#include "ar_interleaver.h"
/* Compared source-only TX-MCS callbacks and request producer. Keys belong to
 * the matched loader profile; no proprietary table is compiled into this code. */
struct ar_tx_mcs_ops {
	uint32_t (*read)(uint32_t field, void *context);
	uint32_t (*word)(uint32_t offset, void *context);
	void (*field)(uint32_t field, uint32_t value, void *context);
	void (*event)(unsigned event, const uint8_t payload[9], void *context);
};
enum ar_tx_mcs_action {
	AR_TX_MCS_PROGRAM, AR_TX_MCS_REQUEST, AR_TX_MCS_DRAIN_APPLY,
	AR_TX_MCS_INITIALIZE, AR_TX_MCS_RETRY_RESET
};
/* Caller serializes state and register access. Bookkeeping users are 0..9;
 * hardware register helpers for other users do not extend this table.
 * User/index bounds are native
 * additions. Invalid key encoding returns -95. Unsupported code-length states
 * return -95 instead of the original assertion; a preceding mode write may
 * already have occurred. Callback effects are synchronous. */
int ar_tx_mcs_control(struct ar_interleaver *state, enum ar_tx_mcs_action action,
	unsigned user, unsigned mcs, const uint32_t keys[25], unsigned chip,
	unsigned dev_role, const struct ar_tx_mcs_ops *ops, void *context);
#endif
