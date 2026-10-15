/* SPDX-License-Identifier: MIT */
#ifndef AR_MAC_H
#define AR_MAC_H
#include <stddef.h>
#include <stdint.h>

enum ar_mac_result {
	AR_MAC_OK = 0, AR_MAC_TRUNCATED = -1, AR_MAC_FORMAT = -2,
	AR_MAC_CHECK = -3, AR_MAC_UNSUPPORTED = -4
};

/* Views borrow the input buffer. Consume before its DMA slot is reused.
 * Integrity and extent validation do not imply peer acceptance or link state. */
struct ar_mac_frame {
	const uint8_t *descriptors, *outband, *data;
	size_t header_length, outband_length, data_length, wire_length;
	uint8_t sequence, descriptor_count;
};
struct ar_mac_payload {
	const uint8_t *data;
	size_t length;
	uint8_t port, flags;
};
struct ar_mac_extension { const uint8_t *data; size_t length; };
/* Compact broadcasts carry no peer identity, channel, mode or slot bitmap.
 * Bind to an established mode-0 slot-0 link before applying these fields. */
struct ar_dev_compact {
	uint8_t sequence, token, state, slot_flag, status_flags, power_index;
};
int ar_dev_compact_decode(const uint8_t *data, size_t length, struct ar_dev_compact *control);
struct ar_dev_control {
	const uint8_t *data;
	size_t length;
	uint32_t extensions;
	struct ar_mac_extension extension[6];
	uint8_t sequence, mode, slot_bitmap, receive_channel, transmit_channel;
	uint8_t peer[4];
	uint16_t slots[8];
};

/* Exact Ascent VRX 18.21.10 eight-bit lookup behavior, including its final eight
 * zero entries. This is distinct from the MAC header's 16-bit CRC. */
uint8_t ar_mac_crc8_step(uint8_t index);
/* MAC header / DEV uplink CRC16, logically treating bytes 2 and 3 as zero. */
uint16_t ar_mac_checksum16(const uint8_t *data, size_t length);
/* Inputs must not overlap output. Failure leaves output untouched and sets
 * written to zero. Outband is at most 252 bytes, padded to four bytes;
 * payloads are contiguous. Stock TX rejects a padded outband extent of 256. */
int ar_mac_encode(uint8_t *output, size_t capacity, uint8_t sequence,
	uint8_t opaque_byte5, const uint8_t *outband, size_t outband_length,
	const struct ar_mac_payload *payloads, size_t count, size_t *written);
int ar_mac_decode(const uint8_t *data, size_t length, struct ar_mac_frame *frame);
/* Only pass an unmodified, successfully decoded frame to this accessor. */
int ar_mac_payload_at(const struct ar_mac_frame *frame, unsigned index,
	struct ar_mac_payload *payload);
/* Full DEV outband form (32..200 bytes), including bounded extension views.
 * The six-byte compact form needs link context and returns UNSUPPORTED here. */
int ar_dev_control_decode(const uint8_t *data, size_t length, struct ar_dev_control *control);
#endif
