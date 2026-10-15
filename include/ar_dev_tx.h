/* SPDX-License-Identifier: MIT */
#ifndef AR_DEV_TX_H
#define AR_DEV_TX_H
#include "ar_mac.h"

/* The initial mode-0 DEV uplink report is distinct from the AP broadcast
 * parsed by ar_dev_control_decode. Assigned slot stays 8 until AP login assigns
 * it; do not substitute the selected transmit context (normally slot 0). */
struct ar_dev_report {
	uint8_t local[4];
	uint8_t state, assigned_slot, receive_channel, slot_value;
	/* Raw status bytes corresponding to original 0x26a874 and 0x26a884.
	 * Their physical semantics remain unresolved; preserve the observed bits. */
	uint8_t status_a, status_b;
};
int ar_dev_report_encode(uint8_t *output, size_t capacity, const struct ar_dev_report *report);

#define AR_DEV_TX_RING_SIZE 10
struct ar_dev_tx_packet {
	_Alignas(4) uint8_t bytes[160]; /* Header, report, optional stream fragment. */
	size_t length, header_length;
};
struct ar_dev_tx_context {
	struct ar_dev_tx_packet packets[AR_DEV_TX_RING_SIZE];
	unsigned head, tail, owned;
	uint8_t sequence;
};
enum ar_dev_tx_result { AR_DEV_TX_FULL = -10, AR_DEV_TX_BAD_RETAIN = -11 };
/* Call init only while hardware is quiesced and owns none of this storage. */
void ar_dev_tx_init(struct ar_dev_tx_context *context);
/* Prepare a control-only packet for a pre-encode request. The returned storage
 * is context-owned until retire; caller inputs may be reused immediately.
 * Capacity is the raw pre-encode byte budget (rounded down to eight by stock).
 * This performs no MMIO and does not mean DMA submission/transmission succeeded. */
int ar_dev_tx_prepare(struct ar_dev_tx_context *context,
	const struct ar_dev_report *report, size_t capacity, const struct ar_dev_tx_packet **packet);
/* One copied port-0 stream fragment, up to 132 bytes. An insufficient byte
 * budget leaves ownership unchanged; the caller must retain/retry its data. */
int ar_dev_tx_prepare_control(struct ar_dev_tx_context *context,
	const struct ar_dev_report *report, const uint8_t *control, size_t length,
	size_t capacity, const struct ar_dev_tx_packet **packet);
/* Port 0 is internal control; ports 1..4 are host streams. Same ownership and
 * budget contract as prepare_control; at most one descriptor per packet. */
int ar_dev_tx_prepare_payload(struct ar_dev_tx_context *context,
	const struct ar_dev_report *report, unsigned port, const uint8_t *payload, size_t length,
	size_t capacity, const struct ar_dev_tx_packet **packet);
/* Internal port-0 command envelope, 0x23288a. Payload at most 128 bytes;
 * checksum is XOR of the entire packet with byte 3 logically zero. */
int ar_dev_command_encode(uint8_t *output, size_t capacity, uint8_t command,
	const uint8_t *payload, size_t length);
/* Release oldest packets until at most retain_limit remain (0x230a80). Stock
 * retains one at pre-encode and zero at TX-end. These are ownership events,
 * not a pending DMA count, packet sequence number or inferred over-air ACK.
 * Call only after the corresponding completion/retention contract is met. */
int ar_dev_tx_retire(struct ar_dev_tx_context *context, unsigned retain_limit);
#endif
