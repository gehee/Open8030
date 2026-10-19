/* SPDX-License-Identifier: MIT */
#ifndef AR_DEV_MCS_H
#define AR_DEV_MCS_H
#include <stddef.h>
#include <stdint.h>

/* Native analysis interface, not a vendor structure or configuration format.
 * Mode-0 DEV receive policy; bandwidth changes and TX-side policy are separate.
 * MCS numbers are internal indices (configuration numeric value plus two).
 * All times are monotonic milliseconds; subtraction tolerates uint32 wrap. */
#define AR_DEV_MCS_ROWS 10
#define AR_DEV_MCS_NONE 25
#define AR_DEV_MCS_CONFIG_BYTES 136
struct ar_dev_mcs_row {
	uint8_t mcs, errors_up, errors_down;
	int8_t consecutive_down;
	uint16_t snr_up, snr_down, up_ms, down_ms;
};
struct ar_dev_mcs_config {
	uint8_t enabled, automatic, initial_mcs, count, policy;
	uint16_t hold_ms, delay_samples, max_wait_ms;
	/* dist_calc from the same config block: enable, offset in raw units. */
	uint8_t distance_enabled, distance_offset;
	struct ar_dev_mcs_row rows[AR_DEV_MCS_ROWS];
};
enum ar_dev_mcs_action {
	AR_DEV_MCS_NOTHING = 0,
	AR_DEV_MCS_APPLY_BUFFER = 1,
	AR_DEV_MCS_WRITE_FCH = 2,
	AR_DEV_MCS_SYNC_ON = 4,
	AR_DEV_MCS_SYNC_OFF = 8,
	AR_DEV_MCS_RESET_RX = 16,
};
struct ar_dev_mcs {
	struct ar_dev_mcs_config config;
	uint16_t snr[8], errors[8], delayed;
	uint8_t connected, ready, index, started, current, previous, observed;
	uint8_t pending, direction, phase, advertised, preencodes;
	/* Seven FCH bytes, checksum supplied by the existing original-table check.
	 * Cancellation preserves the TTL, length and payload bytes. */
	uint8_t fch[7];
	uint32_t sample_ms, direction_ms, request_ms, retry_ms;
	uint32_t samples, requests, acknowledgements, unsolicited, timeouts;
	uint32_t advertisements, cancellations, average_snr, bad_samples, recent_bad;
	/* Open8030: the host's ceiling, internal MCS (0: none) - no row above it,
	 * and down from one at once. */
	uint8_t ceiling;
};
/* Reject unsupported/ill-formed policy data before modifying the context. */
int ar_dev_mcs_init(struct ar_dev_mcs *ctx, const struct ar_dev_mcs_config *config);
/* DEV4 plan extension: flags, initial MCS, row count, policy, three LE16
 * times, sync-MMIO enable, fixed bandwidth code2, four reserved zeros,
 * then ten 12-byte rows in the order of ar_dev_mcs_row. Unused rows zero.
 * sync-MMIO enable is consumed by the RF integration, not this policy. */
int ar_dev_mcs_decode_config(struct ar_dev_mcs_config *config, const uint8_t *wire, size_t length);
unsigned ar_dev_mcs_connect(struct ar_dev_mcs *ctx, unsigned connected);
/* The highest internal MCS the policy may use (0: no limit). Kept across
 * links; the policy steps down to it at its next sample. */
void ar_dev_mcs_set_ceiling(struct ar_dev_mcs *ctx, unsigned internal_mcs);
unsigned ar_dev_mcs_sample(struct ar_dev_mcs *ctx, uint32_t now_ms,
	uint16_t snr, uint16_t errors);
/* A hardware-valid logical-user8 FCH supplies the observed internal MCS. */
unsigned ar_dev_mcs_observe(struct ar_dev_mcs *ctx, uint8_t mcs);
/* Called before the DEV report is encoded. Slots outside 0..7 cannot request. */
unsigned ar_dev_mcs_preencode(struct ar_dev_mcs *ctx, uint32_t now_ms, unsigned slot);
/* Source18/link202 writes an active command; idle DEV frames need no write. */
unsigned ar_dev_mcs_interleaver(const struct ar_dev_mcs *ctx);
#endif
