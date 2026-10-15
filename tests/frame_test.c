/* SPDX-License-Identifier: MIT */
#include "ar_frame.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static unsigned received;
static struct ar_frame observed;
static uint8_t copied[AR_MAX_PAYLOAD];
static void receive(const struct ar_frame *frame, void *context)
{
	(void)context;
	observed = *frame;
	memcpy(copied, frame->payload, frame->length);
	observed.payload = copied;
	received++;
}

int main(void)
{
	struct ar_parser parser = {0};
	uint8_t wire[AR_MAX_FRAME], response[AR_MAX_FRAME];
	const uint8_t bytes[] = {0xaa, 0xbb, 0x00, 0xff};
	struct ar_frame frame = {.reqid = AR_REQ_HEARTBEAT, .msgid = 0x12345678,
		.status = -262, .payload = bytes, .length = sizeof bytes};
	size_t length = ar_encode(wire, sizeof wire, &frame);
	assert(length == 23 && wire[1] == 4 && wire[5] == 0xff && wire[8] == 2);
	assert(wire[9] == 0x12 && wire[13] == 0xff && wire[16] == 0xfa);
	for (size_t i = 0; i < length; i++) { ar_feed(&parser, wire + i, 1, receive, NULL); }
	assert(received == 1 && parser.used == 0 && observed.status == -262);
	assert(observed.msgid == frame.msgid && memcmp(copied, bytes, sizeof bytes) == 0);

	/* Invalid checksum and absurd payload length must resynchronize. */
	wire[17] ^= 1;
	ar_feed(&parser, wire, length, receive, NULL);
	frame.status = 0;
	length = ar_encode(wire, sizeof wire, &frame);
	ar_feed(&parser, wire, length, receive, NULL);
	assert(received == 2 && parser.rejected > 0);
	uint8_t oversized[18] = {0xaa, 0xff, 0xff, 0xff, 0xff};
	ar_feed(&parser, oversized, sizeof oversized, receive, NULL);
	ar_feed(&parser, wire, length, receive, NULL);
	assert(received == 3);

	/* Actual request handling, signed errors, and binary heartbeat payload. */
	size_t reply_length;
	ar_dispatch(&frame, response, &reply_length);
	ar_feed(&parser, response, reply_length, receive, NULL);
	assert(received == 4 && observed.status == 0 && observed.length == 4);
	assert(memcmp(observed.payload, bytes, 4) == 0);
	frame.reqid = 0x0b000000;
	ar_dispatch(&frame, response, &reply_length);
	ar_feed(&parser, response, reply_length, receive, NULL);
	assert(observed.status == AR_STATUS_UNSUPPORTED && observed.length == 0);
	/* Daemon enumeration depends on the logical-device count, independently
	 * of the diagnostic identity and transport frame capacity. */
	frame.reqid = AR_REQ_HELLO;
	frame.payload = NULL; frame.length = 0;
	ar_dispatch(&frame, response, &reply_length);
	ar_feed(&parser, response, reply_length, receive, NULL);
	assert(observed.status == 0 && observed.length == 8);
	assert(observed.payload[0] == 0 && observed.payload[1] == 16);
	assert(observed.payload[3] == 0 && memcmp(observed.payload + 4, "ZEPH", 4) == 0);
#ifdef CONFIG_SOC_AR8030_BRINGUP
	assert(observed.payload[2] == 1);
#else
	assert(observed.payload[2] == 0);
#endif
	frame.reqid = AR_DAEMON_DIAGNOSTIC_BASE;
	ar_dispatch(&frame, response, &reply_length);
	ar_feed(&parser, response, reply_length, receive, NULL);
	assert(observed.reqid == AR_DAEMON_DIAGNOSTIC_BASE && observed.status == 0);
	assert(observed.length >= 14 && memcmp(observed.payload, "ar8030-zephyr/", 14) == 0);
	/* Read aliases preserve the reply ID; start/configuration requests stay
	 * unrecognized instead of acquiring new mutation routes. */
	assert(ar_diagnostic_request_id(AR_DAEMON_DIAGNOSTIC_BASE | 0x23) == AR_REQ_HOST_AUDIT);
	assert(ar_diagnostic_request_id(AR_DAEMON_DIAGNOSTIC_BASE | 0x24) == AR_REQ_BOOT_INFO);
	assert(ar_diagnostic_request_id(AR_DAEMON_DIAGNOSTIC_BASE | 0x25) == AR_REQ_RF_LIFECYCLE_INFO);
	assert(ar_diagnostic_request_id(AR_DAEMON_DIAGNOSTIC_BASE | 0x26) == AR_REQ_RF_RECOVERY_INFO);
	for (unsigned i = 0x10; i <= 0x16; i++) {
		assert(ar_diagnostic_request_id(AR_DAEMON_DIAGNOSTIC_BASE | i) == (AR_DAEMON_DIAGNOSTIC_BASE | i));
	}
	frame.length = AR_MAX_PAYLOAD + 1;
	assert(ar_encode(wire, sizeof wire, &frame) == 0);

	/* Exercise the exact storage bound and corrupt-tail recovery under ASan. */
	static uint8_t maximum[AR_MAX_PAYLOAD];
	for (size_t i = 0; i < sizeof maximum; i++) { maximum[i] = (uint8_t)i; }
	frame.reqid = AR_REQ_HEARTBEAT;
	frame.payload = maximum;
	frame.length = sizeof maximum;
	length = ar_encode(wire, sizeof wire, &frame);
	assert(length == sizeof wire);
	/* In-place maximum payload must produce exactly the same wire bytes,
	 * including the header checksum and the last byte at the storage bound. */
	memcpy(response + AR_HEADER_SIZE, maximum, sizeof maximum);
	frame.payload = response + AR_HEADER_SIZE;
	assert(ar_encode(response, sizeof response, &frame) == length);
	assert(memcmp(response, wire, length) == 0);
	ar_feed(&parser, wire, length, receive, NULL);
	assert(observed.length == sizeof maximum && memcmp(copied, maximum, sizeof maximum) == 0);
	frame.payload = NULL;
	frame.length = 0;
	length = ar_encode(wire, sizeof wire, &frame);
	wire[length - 1] = 0;
	unsigned before = received;
	ar_feed(&parser, wire, length, receive, NULL);
	wire[length - 1] = 0xbb;
	ar_feed(&parser, wire, length, receive, NULL);
	assert(received == before + 1 && parser.used == 0);
	puts("frame/dispatch tests passed");
	return 0;
}
