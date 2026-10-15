/* SPDX-License-Identifier: MIT */
#include "ar_dev_tx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t crc16(const uint8_t *data, size_t length)
{
	/* Independent byte-fold implementation; checksum bytes are skipped. */
	uint16_t crc = 0;
	for (size_t i = 0; i < length; i++) {
		crc = (uint16_t)((crc >> 8) | (crc << 8));
		crc ^= (i == 2 || i == 3) ? 0 : data[i];
		crc ^= (crc & 255) >> 4;
		crc ^= (uint16_t)(crc << 12); crc ^= (uint16_t)((crc & 255) << 5);
	}
	return crc;
}

int main(void)
{
	struct ar_dev_report report = {.local = {1,2,3,4}, .state = 1,
		.assigned_slot = 8, .receive_channel = 2, .status_a = 0, .status_b = 2};
	static const uint8_t expected[24] = {0x58,0,0xcd,0xd6,0,0,16,0,
		16,0,0xa0,0xfc,0x21,2,0x40,0,1,2,3,4,0,0,0,0};
	struct ar_dev_tx_context context;
	const struct ar_dev_tx_packet *packet;
	ar_dev_tx_init(&context);
	assert(ar_dev_tx_retire(&context, 1) == 0 && context.owned == 0);
	assert(ar_dev_tx_prepare(&context, &report, 31, &packet) == 0);
	assert(!memcmp(packet->bytes, expected, 24));
	uint8_t paired_report[16];
	report.pairing = 1;
	assert(!ar_dev_report_encode(paired_report, sizeof paired_report, &report));
	assert(paired_report[4] == 0x61);
	assert((paired_report[2] | (unsigned)paired_report[3] << 8) == crc16(paired_report, 16));
	report.pairing = 0;
	assert(context.owned == 1 && context.sequence == 1);
	assert((uintptr_t)packet->bytes % 4 == 0 && (uintptr_t)(packet->bytes + 8) % 4 == 0);
	struct ar_dev_tx_context before = context;
	assert(ar_dev_tx_prepare(&context, &report, 23, &packet) == AR_MAC_TRUNCATED && !packet);
	assert(!memcmp(&before, &context, sizeof context));
	report.state = 3;
	assert(ar_dev_tx_prepare(&context, &report, 24, &packet) == AR_MAC_FORMAT && !packet);
	assert(!memcmp(&before, &context, sizeof context)); report.state = 1;
	assert(ar_dev_tx_retire(&context, 2) == 0); /* Upper bound, not actual depth. */
	assert(!memcmp(&before, &context, sizeof context));
	assert(ar_dev_tx_retire(&context, 11) == AR_DEV_TX_BAD_RETAIN);
	assert(!memcmp(&before, &context, sizeof context));
	assert(ar_dev_tx_retire(&context, 0) == 0);

	/* Retain all ten buffers and verify caller reuse cannot change them. */
	ar_dev_tx_init(&context);
	const struct ar_dev_tx_packet *held[10]; uint8_t copies[10][24];
	for (unsigned i = 0; i < 10; i++) {
		report.receive_channel = i;
		assert(ar_dev_tx_prepare(&context, &report, 24, &held[i]) == 0);
		memcpy(copies[i], held[i]->bytes, 24);
	}
	before = context;
	assert(ar_dev_tx_prepare(&context, &report, 24, &packet) == AR_DEV_TX_FULL && !packet);
	assert(!memcmp(&before, &context, sizeof context));
	assert(ar_dev_tx_retire(&context, 7) == 0 && context.tail == 3);
	for (unsigned i = 0; i < 3; i++) {
		assert(ar_dev_tx_prepare(&context, &report, 24, &packet) == 0);
		assert(packet == held[i] && packet->bytes[4] == 10 + i);
	}
	for (unsigned i = 3; i < 10; i++) { assert(!memcmp(copies[i], held[i]->bytes, 24)); }
	assert(ar_dev_tx_retire(&context, 0) == 0);
	/* Repeated wrapping includes the 255 -> 0 MAC sequence transition. */
	for (unsigned i = 13; i < 600; i++) {
		assert(ar_dev_tx_prepare(&context, &report, 65535, &packet) == 0);
		assert(packet->bytes[4] == (uint8_t)i);
		assert(ar_dev_tx_retire(&context, 0) == 0);
	}

	uint8_t out[1200], saved[1200];
	memset(out, 0xa5, sizeof out); memcpy(saved, out, sizeof out);
	for (size_t size = 0; size < 16; size++) {
		uint8_t *short_buffer = malloc(size ? size : 1); assert(short_buffer);
		memset(short_buffer, 0xa5, size);
		assert(ar_dev_report_encode(short_buffer, size, &report) == AR_MAC_TRUNCATED);
		for (size_t j = 0; j < size; j++) { assert(short_buffer[j] == 0xa5); }
		free(short_buffer);
	}
	/* Initial state ignores assigned slot; sentinel slot 8 suppresses its
	 * value. Other raw status bits do not change the bit-1-derived flags. */
	for (unsigned state = 0; state < 3; state++) {
		for (unsigned slot = 0; slot < 16; slot++) {
			for (unsigned status = 0; status < 256; status++) {
				report.state = state; report.assigned_slot = slot; report.slot_value = 42;
				report.status_a = status; report.status_b = status ^ 2;
				assert(ar_dev_report_encode(out, sizeof out, &report) == 0);
				unsigned actual_slot = state ? slot : 8;
				assert(out[4] == (state | (actual_slot << 2)));
				assert((out[6] & 63) == (actual_slot == 8 ? 0 : actual_slot < 12 ? 42 : 63));
				assert((out[6] & 0xc0) == ((status & 2) ? 0x80 : 0x40));
				assert((out[2] | (unsigned)out[3] << 8) == crc16(out, 16));
			}
		}
	}

	/* MAC encoding with odd outband/payload extents and nonzero flags/ports. */
	const uint8_t aux[] = {1,2,3}, a[] = {4,5,6}, b[] = {7,8};
	struct ar_mac_payload payloads[255] = {{.data=a,.length=3,.port=3,.flags=10},
		{.data=b,.length=2,.port=4,.flags=1}};
	size_t length;
	assert(ar_mac_encode(out, sizeof out, 17, 9, aux, 3, payloads, 2, &length) == 0);
	assert(length == 25 && out[5] == 9 && out[19] == 0);
	assert((out[2] | (unsigned)out[3] << 8) == crc16(out, 16));
	struct ar_mac_frame frame; struct ar_mac_payload payload;
	assert(ar_mac_decode(out, length, &frame) == 0 && frame.data_length == 5);
	assert(ar_mac_payload_at(&frame, 0, &payload) == 0 && payload.flags == 10 && payload.port == 3);
	assert(!memcmp(payload.data, a, 3));
	assert(ar_mac_payload_at(&frame, 1, &payload) == 0 && payload.flags == 1 && payload.port == 4);
	assert(!memcmp(payload.data, b, 2));
	for (size_t capacity = 0; capacity < 25; capacity++) {
		memcpy(out, saved, sizeof out); length = 100;
		assert(ar_mac_encode(out, capacity, 0, 0, aux, 3, payloads, 2, &length) == AR_MAC_TRUNCATED);
		assert(!length && !memcmp(out, saved, sizeof out));
	}
	payloads[0].length = 0x100000;
	assert(ar_mac_encode(out, sizeof out, 0, 0, aux, 3, payloads, 2, &length) == AR_MAC_FORMAT);
	assert(!memcmp(out, saved, sizeof out));
	payloads[0].length = 3; payloads[1].flags = 16;
	assert(ar_mac_encode(out, sizeof out, 0, 0, aux, 3, payloads, 2, &length) == AR_MAC_FORMAT);
	assert(!memcmp(out, saved, sizeof out));
	memset(payloads, 0, sizeof payloads);
	assert(ar_mac_encode(out, sizeof out, 0, 0, NULL, 0, payloads, 255, &length) == 0);
	assert(length == 1028 && ar_mac_decode(out, length, &frame) == 0 && frame.descriptor_count == 255);
	assert(ar_mac_encode(out, sizeof out, 0, 0, NULL, 0, payloads, 256, &length) == AR_MAC_FORMAT);
	assert(ar_mac_encode(out, sizeof out, 0, 0, NULL, 1, NULL, 0, &length) == AR_MAC_FORMAT);
	uint8_t large_aux[255] = {0};
	assert(ar_mac_encode(out, sizeof out, 0, 0, large_aux, 252, NULL, 0, &length) == 0);
	assert(length == 260);
	memcpy(saved, out, sizeof out);
	for (unsigned size = 253; size <= 255; size++) {
		assert(ar_mac_encode(out, sizeof out, 0, 0, large_aux, size, NULL, 0, &length) == AR_MAC_FORMAT);
		assert(!length && !memcmp(saved, out, sizeof out));
	}
	assert(ar_dev_tx_prepare(NULL, &report, 24, &packet) == AR_MAC_FORMAT);
	assert(ar_dev_tx_retire(NULL, 0) == AR_MAC_FORMAT);
	/* Port-0 command ACK: bounded envelope and whole-packet XOR check.
	 * 33 wire bytes require a rounded pre-encode budget of at least 40. */
	uint8_t ack[5], sync_check = 0;
	assert(ar_dev_command_encode(ack, sizeof ack, 4, &sync_check, 1) == 0);
	assert(!memcmp(ack, "\x4a\x04\x01\x4f\x00", 5));
	ar_dev_tx_init(&context); report.state = 2; report.assigned_slot = 0;
	before = context;
	for (unsigned budget = 24; budget < 40; budget++) {
		assert(ar_dev_tx_prepare_control(&context, &report, ack, 5, budget, &packet) == AR_MAC_TRUNCATED);
		assert(!packet && !memcmp(&before, &context, sizeof context));
	}
	assert(ar_dev_tx_prepare_control(&context, &report, ack, 5, 40, &packet) == 0);
	assert(packet->length == 33 && packet->header_length == 12);
	assert(ar_mac_decode(packet->bytes, packet->length, &frame) == 0);
	assert(ar_mac_payload_at(&frame, 0, &payload) == 0 && payload.port == 0 && payload.length == 5);
	assert(!memcmp(payload.data, ack, 5));
	memset(ack, 0xff, sizeof ack);
	assert(!memcmp(payload.data, "\x4a\x04\x01\x4f\x00", 5));
	assert(frame.outband[4] == 2 && crc16(frame.outband, 16) ==
		(frame.outband[2] | (unsigned)frame.outband[3] << 8));
	for (unsigned length = 0; length <= 128; length++) {
		uint8_t data[128], command[132];
		for (unsigned i = 0; i < length; i++) { data[i] = length + i; }
		assert(ar_dev_command_encode(command, sizeof command, 17, data, length) == 0);
		uint8_t check = 0;
		for (unsigned i = 0; i < length + 4; i++) { check ^= command[i]; }
		assert(!check && command[2] == length && !memcmp(command + 4, data, length));
	}
	/* All host ports carry owned, bounded stream fragments. */
	for (unsigned port = 1; port <= 4; port++) {
		uint8_t source[132]; for (unsigned i = 0; i < sizeof source; i++) { source[i] = i + port; }
		ar_dev_tx_init(&context); before = context;
		assert(ar_dev_tx_prepare_payload(&context, &report, port, source, 132, 159, &packet) == AR_MAC_TRUNCATED);
		assert(!memcmp(&before, &context, sizeof context));
		assert(ar_dev_tx_prepare_payload(&context, &report, port, source, 132, 160, &packet) == 0);
		assert(packet->length == 160 && packet->header_length == 12);
		assert(ar_mac_decode(packet->bytes, packet->length, &frame) == 0);
		assert(ar_mac_payload_at(&frame, 0, &payload) == 0 && payload.port == port && payload.length == 132);
		assert(!memcmp(payload.data, source, 132));
		memset(source, 0, sizeof source);
		assert(payload.data[0] == port && payload.data[131] == 131 + port);
		assert(ar_dev_tx_prepare_payload(&context, &report, 5, source, 1, 160, &packet) == AR_MAC_FORMAT);
	}
	puts("DEV uplink encoding and transmit ownership tests passed");
	return 0;
}
