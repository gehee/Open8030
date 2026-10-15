/* SPDX-License-Identifier: MIT */
#include "ar_mac.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent byte-fold CRC for constructing synthetic test frames. */
static void seal_mac(uint8_t *p)
{
	uint16_t crc = 0;
	p[2] = p[3] = 0;
	for (unsigned i = 0; i < 8u + 4u * p[7]; i++) {
		crc = (uint16_t)((crc >> 8) | (crc << 8));
		crc ^= p[i]; crc ^= (crc & 255) >> 4;
		crc ^= (uint16_t)(crc << 12); crc ^= (uint16_t)((crc & 255) << 5);
	}
	p[2] = crc; p[3] = crc >> 8;
}
static void seal_control(uint8_t *p)
{
	uint8_t check = 0; p[2] = 0;
	for (unsigned i = 0; i < p[0]; i++) { check = ar_mac_crc8_step(check ^ p[i]); }
	p[2] = check;
}
static void word(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++) { p[i] = value >> (8 * i); }
}

int main(void)
{
	/* Invented addresses/content; checks independently generated offline. */
	uint8_t control[200] = {32,9,0x10,0,1,0,2,3,1,2,3,4,1};
	uint8_t packet[1100] = {0x58,0,0x75,0x82,7,0,32,0};
	memcpy(packet + 8, control, 32);
	uint8_t copy[40]; memcpy(copy, packet, sizeof copy);
	struct ar_mac_frame frame;
	struct ar_dev_control dev;
	assert(ar_mac_decode(packet, 40, &frame) == 0);
	assert(frame.sequence == 7 && frame.descriptor_count == 0 && frame.wire_length == 40);
	assert(frame.outband == packet + 8 && frame.outband_length == 32 && frame.data_length == 0);
	assert(ar_dev_control_decode(frame.outband, frame.outband_length, &dev) == 0);
	assert(dev.length == 32 && dev.sequence == 9 && dev.mode == 0 && dev.slot_bitmap == 1);
	assert(dev.receive_channel == 2 && dev.transmit_channel == 3 && dev.slots[0] == 1);
	assert(memcmp(dev.peer, "\1\2\3\4", 4) == 0);
	assert(memcmp(packet, copy, sizeof copy) == 0); /* Checks never mutate evidence. */
	for (size_t length = 0; length < 40; length++) {
		uint8_t *short_packet = malloc(length ? length : 1);
		assert(short_packet); memcpy(short_packet, packet, length);
		assert(ar_mac_decode(short_packet, length, &frame) == AR_MAC_TRUNCATED);
		assert(!frame.data && !frame.outband);
		free(short_packet);
	}
	packet[4] ^= 1;
	assert(ar_mac_decode(packet, 40, &frame) == AR_MAC_CHECK);
	packet[4] ^= 1; packet[1] = 1;
	assert(ar_mac_decode(packet, 40, &frame) == AR_MAC_UNSUPPORTED);
	packet[1] = 0; packet[0] = 0;
	assert(ar_mac_decode(packet, 40, &frame) == AR_MAC_FORMAT);
	packet[0] = 0x58;
	/* Two descriptors, explicit flags and odd-sized adjacent payloads. */
	packet[6] = 1; packet[7] = 2;
	word(packet + 8, (3u << 12) | 0xa03);
	word(packet + 12, (5u << 12) | 0x102);
	memcpy(packet + 20, "abcdefgh", 8); seal_mac(packet);
	assert(ar_mac_decode(packet, 28, &frame) == 0);
	struct ar_mac_payload payload;
	assert(ar_mac_payload_at(&frame, 0, &payload) == 0);
	assert(payload.port == 3 && payload.flags == 10 && payload.length == 3);
	assert(memcmp(payload.data, "abc", 3) == 0);
	assert(ar_mac_payload_at(&frame, 1, &payload) == 0);
	assert(payload.port == 2 && payload.flags == 1 && payload.length == 5);
	assert(memcmp(payload.data, "defgh", 5) == 0);
	assert(ar_mac_payload_at(&frame, 2, &payload) == AR_MAC_FORMAT && !payload.data);
	assert(ar_mac_decode(packet, 27, &frame) == AR_MAC_TRUNCATED);
	assert(ar_mac_decode(packet, 29, &frame) == 0 && frame.wire_length == 28);
	word(packet + 12, 0xfffff002); seal_mac(packet);
	assert(ar_mac_decode(packet, sizeof packet, &frame) == AR_MAC_TRUNCATED);
	/* Max wire descriptor count is bounded by the available header extent. */
	memset(packet, 0, sizeof packet); packet[0] = 0x58; packet[7] = 255; seal_mac(packet);
	assert(ar_mac_decode(packet, 1028, &frame) == 0 && frame.descriptor_count == 255);
	assert(ar_mac_decode(packet, 1027, &frame) == AR_MAC_TRUNCATED);
	/* Extensions 2 and 3: two addresses followed by two packed slot values. */
	control[0] = 44; control[28] = 12; control[32] = 2;
	memcpy(control + 33, "12345678", 8); control[41] = 2; control[42] = 0x18; control[43] = 0x29;
	seal_control(control);
	assert(ar_dev_control_decode(control, 44, &dev) == 0);
	assert(dev.extension[2].length == 9 && dev.extension[3].length == 3);
	assert(dev.extension[2].data == control + 32 && dev.extension[3].data == control + 41);
	control[32] = 255; seal_control(control);
	assert(ar_dev_control_decode(control, 44, &dev) == AR_MAC_TRUNCATED && !dev.data);
	control[32] = 2; control[41] = 3; seal_control(control);
	assert(ar_dev_control_decode(control, 44, &dev) == AR_MAC_TRUNCATED);
	control[41] = 2; seal_control(control); control[20] ^= 1;
	assert(ar_dev_control_decode(control, 44, &dev) == AR_MAC_CHECK);
	control[20] ^= 1; control[28] = 16; seal_control(control);
	assert(ar_dev_control_decode(control, 44, &dev) == AR_MAC_UNSUPPORTED);
	control[28] = 0; control[31] = 0x80; seal_control(control);
	assert(ar_dev_control_decode(control, 44, &dev) == AR_MAC_UNSUPPORTED);
	control[31] = 0; control[0] = 31; seal_control(control);
	assert(ar_dev_control_decode(control, 32, &dev) == AR_MAC_FORMAT);
	assert(ar_dev_control_decode(control, 6, &dev) == AR_MAC_UNSUPPORTED);
	assert(ar_dev_control_decode(control, 201, &dev) == AR_MAC_FORMAT);
	/* Bitmap extensions and the one-byte flag extension. */
	memset(control, 0, sizeof control); control[0] = 58; control[28] = 0x23;
	control[32] = 0x81; control[53] = 5; seal_control(control);
	assert(ar_dev_control_decode(control, 58, &dev) == 0);
	assert(dev.extension[0].length == 21 && dev.extension[1].length == 3 && dev.extension[5].length == 1);
	control[0] = 33; control[32] = 0; seal_control(control);
	assert(ar_dev_control_decode(control, 33, &dev) == 0);
	assert(dev.extension[0].length == 0 && dev.extension[1].length == 0 && dev.extension[5].length == 1);
	/* Exact allocations exercise each short-buffer boundary under ASan. */
	for (size_t length = 0; length < 33; length++) {
		uint8_t *short_control = malloc(length ? length : 1); assert(short_control);
		memcpy(short_control, control, length);
		assert(ar_dev_control_decode(short_control, length, &dev) != 0 && !dev.data);
		free(short_control);
	}
	assert(ar_mac_decode(NULL, 8, &frame) == AR_MAC_TRUNCATED);
	assert(ar_dev_control_decode(NULL, 32, &dev) == AR_MAC_TRUNCATED);
	struct ar_dev_compact compact;
	uint8_t short_form[6] = {0,17,42,0,0,0};
	for (unsigned state = 0; state < 4; state++) {
		for (unsigned power = 0; power < 64; power++) {
			for (unsigned flags = 0; flags < 8; flags++) {
				short_form[3] = state | 4; short_form[4] = (power << 3) | flags;
				short_form[5] = power >> 5; short_form[0] = 0;
				for (unsigned i = 1; i < 6; i++) { short_form[0] = ar_mac_crc8_step(short_form[0] ^ short_form[i]); }
				assert(ar_dev_compact_decode(short_form, 6, &compact) == 0);
				assert(compact.sequence == 17 && compact.token == 42 && compact.state == state);
				assert(compact.power_index == power && compact.status_flags == flags && compact.slot_flag == 1);
				short_form[0] ^= 1;
				assert(ar_dev_compact_decode(short_form, 6, &compact) == AR_MAC_CHECK);
				assert(!compact.token);
			}
		}
	}
	for (unsigned length = 0; length < 6; length++) {
		uint8_t *small = malloc(length ? length : 1); assert(small);
		assert(ar_dev_compact_decode(small, length, &compact) == AR_MAC_TRUNCATED); free(small);
	}
	assert(ar_dev_compact_decode(short_form, 7, &compact) == AR_MAC_FORMAT);
	puts("bounded MAC and DEV control parser tests passed");
	return 0;
}
