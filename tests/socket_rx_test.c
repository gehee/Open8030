/* SPDX-License-Identifier: MIT */
#include "ar_socket_rx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_socket_rx sockets;
static uint8_t scratch[4096], input[AR_SOCKET_RX_CAPACITY + 20], options[12];
static struct ar_frame response;
static int command(unsigned action, unsigned slot, unsigned port, uint32_t msgid, size_t length)
{
	struct ar_frame request = {.reqid = 0x04000000u | action << 16 | slot << 8 | port,
		.msgid = msgid, .payload = options, .length = length};
	assert(ar_socket_rx_request(&sockets, &request, &response));
	assert(response.reqid == request.reqid && response.msgid == msgid);
	return response.status;
}
static void configure(unsigned flags, unsigned capacity)
{
	memset(options, 0, sizeof options); options[0] = flags;
	for (unsigned i = 0; i < 4; i++) { options[8+i] = capacity >> (i*8); }
}
static int send_bytes(uint64_t position, const uint8_t *data, size_t length)
{
	uint8_t body[AR_MAX_PAYLOAD]; assert(length <= sizeof body - 8);
	for (unsigned i = 0; i < 8; i++) { body[i] = position >> (8*i); }
	if (length) { memcpy(body + 8, data, length); }
	struct ar_frame request = {.reqid = 0x04010002, .msgid = 9876,
		.payload = body, .length = length + 8};
	assert(ar_socket_rx_request(&sockets, &request, &response));
	assert(response.msgid == 9876 && response.reqid == request.reqid);
	if (response.length) {
		assert(response.length == 16 && !memcmp(response.payload, body, 8));
		assert(!memcmp(response.payload + 12, "\0\0\0\0", 4));
	}
	/* The parser reuses its buffer immediately after the reply. */
	memset(body, 0xe5, sizeof body);
	return response.status;
}
static void transmit_contract(void)
{
	ar_socket_rx_init(&sockets);
	assert(send_bytes(0, input, 1) == -260);
	configure(3, 56320); options[4] = 5;
	assert(command(0, 0, 2, 0x99991234, 12) == 0);
	assert(sockets.ports[2].capacity == 56320 && sockets.ports[2].tx_capacity == 5);
	assert(command(0, 0, 2, 11, 12) == 257);
	/* Exercise carry without allocating/sending four gigabytes. */
	uint64_t position = UINT64_C(0xfffffffa);
	sockets.ports[2].position = position;
	assert(send_bytes(position, input, 13) == -262 && response.payload[8] == 13);
	assert(sockets.ports[2].position == UINT64_C(0x100000007));
	assert(send_bytes(position + 13, input + 13, 2) == -262);
	assert(send_bytes(position + 15, input + 15, 1) == -263);
	assert(sockets.ports[2].position == position + 15 && response.payload[8] == 1);
	assert(send_bytes(position + 14, input, 1) == -264 && response.payload[8] == 9);
	assert(!sockets.ports[2].tx_used && sockets.ports[2].pending_count == 2);
	struct ar_frame frame, retry;
	assert(!ar_socket_tx_completion(&sockets, scratch, &frame));
	uint8_t drained[15]; size_t total = 0; unsigned done = 0;
	for (unsigned guard = 0; guard < 50 && total < sizeof drained; guard++) {
		ar_socket_tx_service(&sockets);
		if (ar_socket_tx_completion(&sockets, scratch, &frame)) {
			assert(frame.reqid == 0x04010002 && frame.msgid == 0x1234 && frame.length == 8);
			assert(frame.status == (done ? 2 : 13));
			assert(ar_socket_tx_completion(&sockets, scratch, &retry));
			assert(retry.status == frame.status && sockets.ports[2].pending_count == 2 - done);
			ar_socket_tx_complete(&sockets, &frame); done++;
		}
		unsigned port = 99;
		size_t count = ar_socket_tx_peek(&sockets, scratch, 3, &port);
		assert(count && count <= 3 && port == 3);
		uint8_t copy[3]; memcpy(copy, scratch, count);
		assert(ar_socket_tx_peek(&sockets, scratch, 3, &port) == count);
		assert(!memcmp(copy, scratch, count));
		assert(total + count <= sizeof drained); memcpy(drained + total, scratch, count);
		ar_socket_tx_commit(&sockets, port, count); total += count;
	}
	assert(total == 15 && done == 2 && !memcmp(drained, input, 15));
	assert(!sockets.ports[2].tx_used && sockets.ports[2].submitted == 15);
	assert(sockets.ports[2].enqueued == 2 && sockets.ports[2].completed == 2);
	assert(send_bytes(position + 15, NULL, 0) == -262);
	assert(ar_socket_tx_completion(&sockets, scratch, &frame) && frame.status == -7);
	ar_socket_tx_complete(&sockets, &frame);
	assert(sockets.ports[2].position == position + 15);
	/* Close discards endpoint-owned bytes, including a partially copied write. */
	assert(send_bytes(position + 15, input, 20) == -262);
	ar_socket_tx_service(&sockets);
	assert(command(3, 0, 2, 44, 0) == 0);
	assert(!ar_socket_tx_completion(&sockets, scratch, &frame));
	unsigned port;
	assert(!ar_socket_tx_peek(&sockets, scratch, sizeof scratch, &port));
	configure(2, 0); options[5] = 8;
	assert(command(0, 0, 2, 55, 12) == 0 && !sockets.ports[2].capacity);
	assert(!ar_socket_rx_feed(&sockets, 0, 3, input, 1));
	assert(command(0, 0, 2, 66, 12) == 257);
	assert(send_bytes(0, input, 2048) == -262);
	ar_socket_tx_service(&sockets);
	assert(ar_socket_tx_completion(&sockets, scratch, &frame) && frame.status == 2048);
	/* Completion transfers credit while data remains endpoint-owned for MAC. */
	ar_socket_tx_complete(&sockets, &frame);
	assert(sockets.ports[2].tx_used == 2048 && !sockets.ports[2].pending_count);
	assert(send_bytes(2048, input, 100) == -262);
	ar_socket_tx_service(&sockets);
	assert(!ar_socket_tx_completion(&sockets, scratch, &frame));
	assert(command(3, 0, 2, 44, 0) == 0);
	/* Writes stay isolated and MAC scheduling is round-robin, independent of
	 * completion order. Each open ID is retained at the endpoint. */
	ar_socket_rx_init(&sockets);
	configure(3, 56320); options[4] = 32;
	for (unsigned i = 0; i < 4; i++) {
		assert(command(0, 0, i, 100 + i, 12) == 0);
		uint8_t body[9] = {0}; body[8] = i + 10;
		struct ar_frame request = {.reqid = 0x04010000 | i, .msgid = 90, .payload = body, .length = sizeof body};
		assert(ar_socket_rx_request(&sockets, &request, &response) && response.status == -262);
		body[8] = 0;
	}
	ar_socket_tx_service(&sockets);
	for (unsigned i = 0; i < 4; i++) {
		assert(ar_socket_tx_peek(&sockets, scratch, sizeof scratch, &port) == 1);
		assert(port == i + 1 && scratch[0] == i + 10);
		ar_socket_tx_commit(&sockets, port, 1);
		assert(ar_socket_tx_completion(&sockets, scratch, &frame) && frame.msgid == 100 + i);
		ar_socket_tx_complete(&sockets, &frame);
	}
	assert(!ar_socket_tx_peek(&sockets, scratch, sizeof scratch, &port));
	assert(!ar_socket_tx_completion(&sockets, scratch, &frame));
	assert(command(3, 0, 2, 44, 0) == 0);
	configure(0x23, 100); options[4] = 8;
	assert(command(0, 0, 2, 44, 12) == -95);
	configure(2, 0); options[5] = 0x11;
	assert(command(0, 0, 2, 44, 12) == -259);
}
int main(void)
{
	ar_socket_rx_init(&sockets);
	for (unsigned i = 0; i < sizeof input; i++) { input[i] = i; }
	assert(!ar_socket_rx_feed(&sockets, 0, 4, input, 100));
	configure(1, 7);
	assert(command(0, 0, 3, 0xabcd1234, 11) == -22);
	assert(command(0, 1, 3, 1, 12) == -259 && response.length == 4 && response.payload[0] == 250);
	assert(command(0, 0, 4, 1, 12) == -259);
	assert(command(0, 0, 3, 0xabcd1234, 12) == 0 && !response.length);
	assert(command(0, 0, 3, 8, 12) == 257 && sockets.ports[3].msgid == 0x1234);
	assert(!ar_socket_rx_feed(&sockets, 0, 0, input, 100));
	assert(!ar_socket_rx_feed(&sockets, 1, 4, input, 100));
	assert(ar_socket_rx_feed(&sockets, 0, 4, input, 6) == 6);
	struct ar_frame frame, retry;
	assert(ar_socket_rx_peek(&sockets, scratch, 4, &frame));
	assert(frame.reqid == 0x04020003 && frame.msgid == 0x1234 && frame.length == 4 && !frame.status);
	assert(!memcmp(frame.payload, input, 4));
	/* SDIO rejection keeps the same bytes and does not advance ownership. */
	assert(ar_socket_rx_peek(&sockets, scratch, 4, &retry));
	assert(retry.length == frame.length && sockets.ports[3].used == 6 && !sockets.ports[3].delivered);
	ar_socket_rx_commit(&sockets, &frame);
	assert(ar_socket_rx_feed(&sockets, 0, 4, input + 6, 8) == 5);
	assert(ar_socket_rx_peek(&sockets, scratch, sizeof scratch, &frame));
	assert(frame.length == 7 && !memcmp(frame.payload, input + 4, 7));
	ar_socket_rx_commit(&sockets, &frame);
	assert(sockets.ports[3].offered == 14 && sockets.ports[3].accepted == 11 && sockets.ports[3].dropped == 3);
	assert(sockets.ports[3].delivered == 11 && !sockets.ports[3].used);
	assert(!ar_socket_rx_peek(&sockets, scratch, sizeof scratch, &frame));
	assert(command(1, 0, 3, 9, 12) == -261);
	assert(command(3, 0, 3, 10, 0) == 0);
	assert(command(3, 0, 3, 10, 0) == 259);
	configure(3, 100); assert(command(0, 0, 3, 1, 12) == -259);
	configure(1, 0); assert(command(0, 0, 3, 1, 12) == -259);
	configure(1, AR_SOCKET_RX_CAPACITY + 1); assert(command(0, 0, 3, 1, 12) == -259 && response.payload[0] == 253);
	/* All host ports map to MAC port+1, are isolated, and drain fairly. */
	configure(0x21, AR_SOCKET_RX_CAPACITY);
	for (unsigned port = 0; port < 4; port++) {
		assert(command(0, 0, port, port + 20, 12) == 0);
		assert(ar_socket_rx_feed(&sockets, 0, port + 1, input + port, 4100) == 4100);
	}
	for (unsigned round = 0; round < 2; round++) {
		for (unsigned port = 0; port < 4; port++) {
			assert(ar_socket_rx_peek(&sockets, scratch, sizeof scratch, &frame));
			assert((frame.reqid & 255) == port && frame.msgid == port + 20);
			assert(frame.length == (round ? 4 : 4096));
			assert(!memcmp(frame.payload, input + port + (round ? 4096 : 0), frame.length));
			ar_socket_rx_commit(&sockets, &frame);
		}
	}
	assert(ar_socket_rx_feed(&sockets, 0, 4, input, sizeof input) == AR_SOCKET_RX_CAPACITY);
	assert(command(3, 0, 3, 10, 0) == 0);
	assert(command(0, 0, 3, 44, 12) == 0);
	assert(!ar_socket_rx_peek(&sockets, scratch, sizeof scratch, &frame)); /* No stale bytes after reopen. */
	/* RF can append while a host batch is being encoded. Its scratch copy
	 * and pending head must remain stable until the successful SDIO commit. */
	assert(ar_socket_rx_feed(&sockets, 0, 4, input, 5000) == 5000);
	assert(ar_socket_rx_peek(&sockets, scratch, 1005, &frame));
	assert(ar_socket_rx_feed(&sockets, 0, 4, input + 5000, 1000) == 1000);
	assert(frame.length == 1005 && !memcmp(frame.payload, input, frame.length));
	ar_socket_rx_commit(&sockets, &frame);
	size_t consumed = 1005;
	while (ar_socket_rx_peek(&sockets, scratch, 1005, &frame)) {
		assert(!memcmp(frame.payload, input + consumed, frame.length));
		consumed += frame.length;
		ar_socket_rx_commit(&sockets, &frame);
	}
	assert(consumed == 6000 && sockets.ports[3].delivered == 6000 && !sockets.ports[3].dropped);
	transmit_contract();
	puts("socket RX/TX ownership, stream positions, completion and backpressure contracts passed");
	return 0;
}
