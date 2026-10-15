/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_PACKET_H
#define AR_TX_PACKET_H
#include "ar_tx_stream.h"

/* Source-only prepared-packet phase, not an original structure declaration.
 * A ring slot and optional outband have already been selected by the caller.
 * Packet bytes 0..27 contain the header, byte 28 the request, and 32..231 the
 * five span groups. Addresses are adapter tokens, not native pointers. */
struct ar_tx_packet_state {
	uint32_t configuration, packet_address, outband_address, outband_length, budget;
	uint8_t request, outband_flag;
	uint8_t packet[232];
	uint8_t policy_enabled, mode, role;
	uint8_t present[8], user_flags[8][2];
	uint32_t policy_control;
};
struct ar_tx_packet_ops {
	/* Compared call boundaries only; units and producers remain unverified. */
	int (*policy_value)(unsigned selector, uint32_t *value, void *context);
	uint32_t (*policy_word)(void *context);
	void (*cache)(uint32_t address, uint32_t length, void *context);
	void (*submit)(uint32_t address, uint32_t length, void *context);
};
/* Composes availability and acquisition; submits header, optional outband,
 * then nonempty spans in descriptor order, followed by submit(0,0). Caller
 * supplies budget >= 8+outband_length and serializes all state/storage. Adapters
 * must not mutate the packet or input parameters during callbacks.
 * Acquired references remain held for descriptor retirement. Acquisition return
 * is ignored by the original builder. Length mismatch returns -95 at the
 * original assertion boundary, AFTER segment submissions and WITHOUT terminal
 * submit or rollback. Never blindly retry/retire an error: descriptors may
 * advertise more than was acquired. Native metadata errors return -22 with
 * earlier effects retained. This API does not establish a safe recovery policy,
 * cache correctness, hardware execution, full handler or runtime integration. */
int ar_tx_packet_prepare(struct ar_tx_packet_state *state,
	struct ar_tx_stream_registry *registry,
	const struct ar_tx_stream_acquire_ops *stream_ops,
	const struct ar_tx_packet_ops *ops, void *context);
#endif
