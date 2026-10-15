/* SPDX-License-Identifier: MIT */
#include "ar_dev_tx.h"
#include <string.h>

int ar_dev_report_encode(uint8_t *output, size_t capacity, const struct ar_dev_report *report)
{
	if (!output || !report || report->state > 2 || report->assigned_slot > 15 ||
	    report->slot_value > 63) { return AR_MAC_FORMAT; }
	if (capacity < 16) { return AR_MAC_TRUNCATED; }
	/* 0x22de32 initializes this report to zero, then copies local address at
	 * offset 8. 0x238630..0x238762 fills the mode-0/no-extension report. */
	uint8_t bytes[16] = {16};
	unsigned slot = report->state ? report->assigned_slot : 8;
	bytes[4] = report->state | (uint8_t)(slot << 2);
	bytes[5] = report->receive_channel;
	bytes[6] = slot == 8 ? 0 : slot < 12 ? report->slot_value : 63;
	bytes[6] |= (((report->status_a ^ 2) >> 1) & 1) << 6;
	bytes[6] |= (((report->status_b ^ 2) >> 1) & 1) << 7;
	memcpy(bytes + 8, report->local, 4);
	uint16_t check = ar_mac_checksum16(bytes, sizeof bytes);
	bytes[2] = (uint8_t)check; bytes[3] = check >> 8;
	memcpy(output, bytes, sizeof bytes);
	return AR_MAC_OK;
}

void ar_dev_tx_init(struct ar_dev_tx_context *context)
{
	if (context) { memset(context, 0, sizeof *context); }
}

int ar_dev_tx_prepare(struct ar_dev_tx_context *context,
	const struct ar_dev_report *report, size_t capacity, const struct ar_dev_tx_packet **packet)
{
	return ar_dev_tx_prepare_control(context, report, NULL, 0, capacity, packet);
}

int ar_dev_command_encode(uint8_t *output, size_t capacity, uint8_t command,
	const uint8_t *payload, size_t length)
{
	if (!output || length > 128 || (length && !payload)) { return AR_MAC_FORMAT; }
	if (capacity < length + 4) { return AR_MAC_TRUNCATED; }
	uint8_t check = 0x4a ^ command ^ (uint8_t)length;
	for (size_t i = 0; i < length; i++) { check ^= payload[i]; }
	output[0] = 0x4a; output[1] = command; output[2] = length; output[3] = check;
	if (length) { memcpy(output + 4, payload, length); }
	return AR_MAC_OK;
}

int ar_dev_tx_prepare_control(struct ar_dev_tx_context *context,
	const struct ar_dev_report *report, const uint8_t *payload, size_t length,
	size_t capacity, const struct ar_dev_tx_packet **packet)
{
	return ar_dev_tx_prepare_payload(context, report, 0, payload, length, capacity, packet);
}

int ar_dev_tx_prepare_payload(struct ar_dev_tx_context *context,
	const struct ar_dev_report *report, unsigned port, const uint8_t *payload, size_t length,
	size_t capacity, const struct ar_dev_tx_packet **packet)
{
	if (!packet) { return AR_MAC_FORMAT; }
	*packet = NULL;
	if (!context || port > 4 || length > 132 || (length && !payload)) { return AR_MAC_FORMAT; }
	if (context->owned == AR_DEV_TX_RING_SIZE) { return AR_DEV_TX_FULL; }
	if ((capacity & ~(size_t)7) < 24 + (length ? 4 + length : 0)) { return AR_MAC_TRUNCATED; }
	uint8_t control[16];
	int result = ar_dev_report_encode(control, sizeof control, report);
	if (result) { return result; }
	struct ar_dev_tx_packet *next = &context->packets[context->head];
	size_t written;
	/* Original 0x238952 writes port and length, preserving the unused nibble.
	 * Original RX 0x232eca consumes only port and length. Initialize that
	 * reserved nibble to zero rather than copying allocator residue. */
	struct ar_mac_payload part = {.data = payload, .length = length, .port = port, .flags = 0};
	result = ar_mac_encode(next->bytes, sizeof next->bytes, context->sequence, 0,
		control, sizeof control, length ? &part : NULL, length ? 1 : 0, &written);
	if (result) { return result; }
	next->length = written; next->header_length = length ? 12 : 8;
	context->head = (context->head + 1) % AR_DEV_TX_RING_SIZE;
	context->owned++; context->sequence++;
	*packet = next;
	return AR_MAC_OK;
}

int ar_dev_tx_retire(struct ar_dev_tx_context *context, unsigned retain_limit)
{
	if (!context) { return AR_MAC_FORMAT; }
	if (retain_limit > AR_DEV_TX_RING_SIZE) { return AR_DEV_TX_BAD_RETAIN; }
	while (context->owned > retain_limit) {
		context->tail = (context->tail + 1) % AR_DEV_TX_RING_SIZE;
		context->owned--;
	}
	return AR_MAC_OK;
}
