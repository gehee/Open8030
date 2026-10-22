/* SPDX-License-Identifier: MIT */
#ifndef AR_DEV_H
#define AR_DEV_H
#include "ar_mac.h"

#define AR_DEV_MAX_CANDIDATES 100

/* Idle-node policy, supplied by configuration and current search state.
 * Addresses are four wire-order bytes, not host-endian integers. */
struct ar_dev_idle_config {
	uint8_t local[4], configured_peer[4];
	const uint8_t (*candidates)[4];
	size_t candidate_count;
	uint8_t pairing_skip[3][4];
	uint8_t receive_channel, channel_count, mode, slot_bitmap;
	uint8_t relay, pairing;
};

enum ar_dev_admission {
	AR_DEV_READY = 0, AR_DEV_RECONFIGURE = 1, AR_DEV_NO_SLOT = 2,
	AR_DEV_BAD_CONFIG = -1, AR_DEV_CHANNEL = -2, AR_DEV_RELAY = -3,
	AR_DEV_PAIRING = -4, AR_DEV_LOCAL_POSITION = -5, AR_DEV_PEER = -6
};

/* A READY result describes the next transition; it is not linked state.
 * No MMIO, timer cancellation, MAC queueing or TX release occurs here. */
struct ar_dev_acquisition {
	uint8_t peer[4], peer_hash, selected_slot, available_slot;
	uint8_t receive_channel, transmit_channel, mode, slot_bitmap;
	uint8_t pairing, local_position, control_field_b00, rebuild_frame;
};

/* control and its borrowed input must be an unchanged successful full-form
 * ar_dev_control_decode result. For RECONFIGURE only mode/slot_bitmap are
 * returned: mode policy and frame rebuilding must run before reevaluation.
 * Other non-READY results clear the output. Configuration is never modified. */
int ar_dev_idle_evaluate(const struct ar_dev_idle_config *config,
	const struct ar_dev_control *control, struct ar_dev_acquisition *next);

/* Mode-0 lock-state decision, including matching pairing advertisements.
 * ASSIGNMENT means a matching
 * login record; remote synchronization must still execute before state 2.
 * Inputs/views are read-only and have the same successful-decode contract. */
enum ar_dev_lock_result {
	AR_DEV_LOCK_WAIT_LOGIN, AR_DEV_LOCK_NO_POSITION, AR_DEV_LOCK_ASSIGNMENT,
	AR_DEV_LOCK_TEARDOWN, AR_DEV_LOCK_PEER, AR_DEV_LOCK_TOKEN_CHANGED,
	AR_DEV_LOCK_PROFILE, AR_DEV_LOCK_BAD_CONFIG, AR_DEV_LOCK_RESULT_COUNT
};
struct ar_dev_lock_context {
	uint8_t peer[4], selected_slot, assigned_slot, token;
	uint8_t transmit_channel; /* Admitted uplink, independent of receive channel. */
};
struct ar_dev_login {
	uint8_t slot, token, value, ap_state, sync[4];
};
enum ar_dev_lock_result ar_dev_lock_evaluate(const struct ar_dev_idle_config *config,
	const struct ar_dev_lock_context *lock, const struct ar_dev_control *control,
	struct ar_dev_login *login);

/* DEV retransmission-request watchdog (0x231372). This observes hardware
 * four-bit TLV request IDs, not MAC sequence numbers or over-air ACKs.
 * A threshold of zero disables it; negative signed vendor values map to zero
 * before this API. Caller applies the returned TX control transition. */
struct ar_dev_retx {
	uint8_t threshold, last_id, repeated, phase;
};
enum ar_dev_retx_action {
	AR_DEV_RETX_INVALID = -1, AR_DEV_RETX_NONE,
	AR_DEV_RETX_DISABLE, AR_DEV_RETX_ENABLE
};
void ar_dev_retx_init(struct ar_dev_retx *context, uint8_t threshold);
enum ar_dev_retx_action ar_dev_retx_feed(struct ar_dev_retx *context,
	uint8_t first, uint8_t second); /* Each ID is 0..15 or absent (255). */

/* Internal port-0 byte stream, independent of MAC descriptor boundaries.
 * Callback receives a borrowed complete envelope valid only during the call.
 * Feed must not be reentered from callback. No command effects are implied. */
struct ar_dev_stream {
	uint8_t bytes[132];
	size_t used;
	uint32_t commands[8], checksum_errors, length_errors, discarded;
};
typedef void (*ar_dev_command_callback)(const uint8_t *envelope, size_t length, void *context);
void ar_dev_stream_init(struct ar_dev_stream *stream);
int ar_dev_stream_feed(struct ar_dev_stream *stream, const uint8_t *data, size_t length,
	ar_dev_command_callback callback, void *context);
#endif
