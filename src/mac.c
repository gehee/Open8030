/* SPDX-License-Identifier: MIT */
/* Independent wire-format implementation, corroborated against Ascent VRX
 * 18.21.10 MAC receive and DEV link-message paths. No vendor table is embedded. */
#include "ar_mac.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint8_t ar_mac_crc8_step(uint8_t index)
{
	static const uint8_t basis[8] = {4, 8, 16, 32, 35, 37, 41, 49};
	uint8_t value = 0;
	if (index < 248) {
		for (unsigned bit = 0; bit < 8; bit++) {
			if (index & (1u << bit)) { value ^= basis[bit]; }
		}
	}
	return value;
}

uint16_t ar_mac_checksum16(const uint8_t *data, size_t length)
{
	uint16_t crc = 0;
	for (size_t i = 0; i < length; i++) {
		/* Stock 0x232eca zeroes these bytes in place; preserve DMA evidence. */
		crc ^= (uint16_t)((i == 2 || i == 3) ? 0 : data[i]) << 8;
		for (unsigned bit = 0; bit < 8; bit++) {
			crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
		}
	}
	return crc;
}

int ar_mac_encode(uint8_t *output, size_t capacity, uint8_t sequence,
	uint8_t opaque_byte5, const uint8_t *outband, size_t outband_length,
	const struct ar_mac_payload *payloads, size_t count, size_t *written)
{
	if (!written) { return AR_MAC_FORMAT; }
	*written = 0;
	if (!output || count > 255 || outband_length > 252 ||
	    (outband_length && !outband) || (count && !payloads)) { return AR_MAC_FORMAT; }
	size_t header = 8 + 4 * count, aligned = (outband_length + 3) & ~(size_t)3;
	size_t total = header + aligned;
	if (total > capacity) { return AR_MAC_TRUNCATED; }
	for (size_t i = 0; i < count; i++) {
		if (payloads[i].length > 0xfffff || payloads[i].flags > 15 ||
		    (payloads[i].length && !payloads[i].data)) { return AR_MAC_FORMAT; }
		if (payloads[i].length > capacity - total) { return AR_MAC_TRUNCATED; }
		total += payloads[i].length;
	}
	memset(output, 0, header + aligned);
	output[0] = 0x58; output[4] = sequence; output[5] = opaque_byte5;
	output[6] = (uint8_t)outband_length; output[7] = (uint8_t)count;
	if (outband_length) { memcpy(output + header, outband, outband_length); }
	size_t offset = header + aligned;
	for (size_t i = 0; i < count; i++) {
		uint32_t word = (uint32_t)payloads[i].length << 12 |
			(uint32_t)payloads[i].flags << 8 | payloads[i].port;
		for (unsigned byte = 0; byte < 4; byte++) { output[8 + 4 * i + byte] = word >> (8 * byte); }
		if (payloads[i].length) { memcpy(output + offset, payloads[i].data, payloads[i].length); }
		offset += payloads[i].length;
	}
	uint16_t check = ar_mac_checksum16(output, header);
	output[2] = (uint8_t)check; output[3] = check >> 8;
	*written = total;
	return AR_MAC_OK;
}

int ar_mac_decode(const uint8_t *data, size_t length, struct ar_mac_frame *frame)
{
	if (!frame) { return AR_MAC_FORMAT; }
	memset(frame, 0, sizeof *frame);
	if (!data || length < 8) { return AR_MAC_TRUNCATED; }
	if (data[0] != 0x58) { return AR_MAC_FORMAT; }
	if (data[1] != 0) { return AR_MAC_UNSUPPORTED; }
	size_t header = 8u + 4u * data[7];
	if (header > length) { return AR_MAC_TRUNCATED; }
	if (ar_mac_checksum16(data, header) != le16(data + 2)) { return AR_MAC_CHECK; }
	size_t aligned_outband = ((size_t)data[6] + 3u) & ~(size_t)3u;
	if (aligned_outband > length - header) { return AR_MAC_TRUNCATED; }
	size_t offset = header + aligned_outband, payload_length = 0;
	for (unsigned i = 0; i < data[7]; i++) {
		size_t count = le32(data + 8 + i * 4) >> 12;
		if (count > length - offset) { return AR_MAC_TRUNCATED; }
		offset += count; payload_length += count;
	}
	frame->descriptors = data + 8;
	frame->outband = data + header;
	frame->data = data + header + aligned_outband;
	frame->header_length = header; frame->outband_length = data[6];
	frame->data_length = payload_length; frame->wire_length = offset;
	frame->sequence = data[4]; frame->descriptor_count = data[7];
	/* Stock accepts trailing DMA padding; it is not included in payload views. */
	return AR_MAC_OK;
}

int ar_mac_payload_at(const struct ar_mac_frame *frame, unsigned index, struct ar_mac_payload *payload)
{
	if (!payload) { return AR_MAC_FORMAT; }
	memset(payload, 0, sizeof *payload);
	if (!frame || index >= frame->descriptor_count) { return AR_MAC_FORMAT; }
	size_t offset = 0;
	for (unsigned i = 0; i <= index; i++) {
		uint32_t word = le32(frame->descriptors + i * 4);
		size_t count = word >> 12;
		if (count > frame->data_length - offset) { return AR_MAC_TRUNCATED; }
		if (i == index) {
			payload->data = frame->data + offset; payload->length = count;
			payload->port = (uint8_t)word; payload->flags = (word >> 8) & 15;
			return AR_MAC_OK;
		}
		offset += count;
	}
	return AR_MAC_FORMAT;
}

static unsigned population(uint8_t value)
{
	unsigned count = 0;
	for (; value; value >>= 1) { count += value & 1; }
	return count;
}

int ar_dev_compact_decode(const uint8_t *data, size_t length, struct ar_dev_compact *control)
{
	if (!control) { return AR_MAC_FORMAT; }
	memset(control, 0, sizeof *control);
	if (!data || length < 6) { return AR_MAC_TRUNCATED; }
	if (length != 6) { return AR_MAC_FORMAT; }
	/* 0x22e8e6 checks exactly bytes1..5. Unlike full DEV control, no
	 * checksum byte is zeroed or included, and the first byte is not length. */
	uint8_t check = 0;
	for (unsigned i = 1; i < 6; i++) { check = ar_mac_crc8_step(check ^ data[i]); }
	if (check != data[0]) { return AR_MAC_CHECK; }
	control->sequence = data[1]; control->token = data[2];
	control->state = data[3] & 3; control->slot_flag = (data[3] >> 2) & 1;
	control->status_flags = data[4] & 7;
	control->power_index = (data[4] >> 3) | ((data[5] & 1) << 5);
	return AR_MAC_OK;
}

int ar_dev_control_decode(const uint8_t *data, size_t length, struct ar_dev_control *control)
{
	if (!control) { return AR_MAC_FORMAT; }
	memset(control, 0, sizeof *control);
	if (!data) { return AR_MAC_TRUNCATED; }
	if (length == 6) { return AR_MAC_UNSUPPORTED; }
	if (length < 32) { return AR_MAC_TRUNCATED; }
	if (length > 200 || data[0] < 32) { return AR_MAC_FORMAT; }
	size_t declared = data[0];
	if (declared > length) { return AR_MAC_TRUNCATED; }
	uint8_t check = 0;
	for (size_t i = 0; i < declared; i++) {
		check = ar_mac_crc8_step(check ^ (i == 2 ? 0 : data[i]));
	}
	if (check != data[2]) { return AR_MAC_CHECK; }
	uint32_t extensions = le32(data + 28);
	/* Table 0x254838 has readers for bits 0,1,2,3,5. Bit 4 has no reader;
	 * higher bits have no established format. Never silently accept them. */
	if (extensions & ~UINT32_C(0x2f)) { return AR_MAC_UNSUPPORTED; }
	struct ar_dev_control parsed = {0};
	size_t offset = 32;
	for (unsigned i = 0; i < 6; i++) {
		if (!(extensions & (1u << i))) { continue; }
		if (offset == declared) { return AR_MAC_TRUNCATED; }
		unsigned count = data[offset];
		size_t extent;
		if (i == 0 || i == 1) {
			count = population((uint8_t)count);
			/* These two original readers return zero for an empty bitmap. */
			extent = count ? 1u + count * (i == 0 ? 10u : 1u) : 0;
		} else if (i == 2) { extent = 1u + 4u * count; }
		else if (i == 3) { extent = 1u + count; }
		else { extent = 1; }
		if (extent > declared - offset) { return AR_MAC_TRUNCATED; }
		parsed.extension[i].data = data + offset;
		parsed.extension[i].length = extent;
		offset += extent;
	}
	parsed.data = data; parsed.length = declared; parsed.extensions = extensions;
	parsed.sequence = data[1]; parsed.mode = data[3] & 3; parsed.slot_bitmap = data[4];
	parsed.receive_channel = data[6]; parsed.transmit_channel = data[7];
	memcpy(parsed.peer, data + 8, 4);
	for (unsigned i = 0; i < 8; i++) { parsed.slots[i] = le16(data + 12 + 2 * i); }
	*control = parsed;
	return AR_MAC_OK;
}
