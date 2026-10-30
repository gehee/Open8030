/* SPDX-License-Identifier: MIT */
/* The stock camera may pipeline five retained writes before any completion. */
#include "ar_socket_rx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_socket_rx sockets;
static uint8_t input[AR_MAX_PAYLOAD], scratch[6000];
static void position(uint64_t value)
{
	for (unsigned i = 0; i < 8; i++) { input[i] = value >> (8 * i); }
}
int main(void)
{
	const size_t payload = AR_MAX_PAYLOAD - 8;
	ar_socket_rx_init(&sockets);
	uint8_t options[12] = {0x23, 0, 0, 0, 5, 0, 0, 0, 0, 8, 0, 0};
	struct ar_frame request = {.reqid = 0x04000003, .msgid = 42,
		.payload = options, .length = sizeof options}, reply;
	assert(ar_socket_rx_request(&sockets, &request, &reply) == 1 && !reply.status);
	request.reqid = 0x04010003; request.payload = input; request.length = sizeof input;
	for (unsigned i = 0; i < 5; i++) {
		position(i * payload);
		for (size_t j = 0; j < payload; j++) { input[8 + j] = (uint8_t)(i * 37 + j); }
		assert(ar_socket_rx_request(&sockets, &request, &reply) == 1);
		assert(reply.status == -262); /* accepted, completion deferred */
	}
	assert(sockets.ports[3].pending_count == 5 && !sockets.tx_full);
	position(5 * payload);
	assert(ar_socket_rx_request(&sockets, &request, &reply) == 1 && reply.status == -263);
	assert(sockets.ports[3].position == 5 * payload);

	unsigned completed = 0;
	size_t sent = 0;
	for (unsigned step = 0; step < 100 && (completed != 5 || sent != 5 * payload); step++) {
		ar_socket_tx_service(&sockets);
		struct ar_frame completion;
		if (ar_socket_tx_completion(&sockets, scratch, &completion)) {
			assert(completion.status == (int32_t)payload && completion.msgid == 42);
			uint64_t offset = 0;
			for (unsigned i = 0; i < 8; i++) { offset |= (uint64_t)completion.payload[i] << (8 * i); }
			assert(offset == completed * payload);
			ar_socket_tx_complete(&sockets, &completion); completed++;
		}
		unsigned port = 0;
		size_t n = ar_socket_tx_peek(&sockets, scratch, sizeof scratch, &port);
		if (!n) { continue; }
		assert(port == 4);
		for (size_t i = 0; i < n; i++) {
			size_t offset = sent + i;
			assert(scratch[i] == (uint8_t)((offset / payload) * 37 + offset % payload));
		}
		ar_socket_tx_commit(&sockets, port, n); sent += n;
	}
	assert(completed == 5 && sent == 5 * payload);
	assert(!sockets.ports[3].pending_count && !sockets.ports[3].tx_used);
	position(5 * payload); memset(input + 8, 0x77, payload);
	assert(ar_socket_rx_request(&sockets, &request, &reply) == 1 && reply.status == -262);
	assert(sockets.ports[3].position == 6 * payload && !sockets.tx_out_of_order);
	/* The other ports are small on an air unit: kestrel-air's control port
	 * (2: retained TX, two in flight, 2 KB in) gets a 4 KB ring and takes two
	 * writes, the third is -263; a port asking more than its 8 KB in is -3. */
	assert(sockets.ports[3].tx_capacity == AR_SOCKET_TX_CAPACITY);
	uint8_t control[12] = {0x23, 0, 0, 0, 2, 0, 0, 0, 0, 8, 0, 0};
	request = (struct ar_frame){.reqid = 0x04000002, .msgid = 43, .payload = control, .length = sizeof control};
	assert(ar_socket_rx_request(&sockets, &request, &reply) == 1 && !reply.status);
	assert(sockets.ports[2].tx_capacity == AR_SOCKET_SMALL_TX_CAPACITY && sockets.ports[2].capacity == 0x800);
	request.reqid = 0x04010002; request.payload = input; request.length = 8 + 100;
	for (unsigned i = 0; i < 3; i++) {
		position(i * 100);
		assert(ar_socket_rx_request(&sockets, &request, &reply) == 1);
		assert(reply.status == (i < AR_SOCKET_SMALL_TX_PENDING ? -262 : -263));
	}
	uint8_t big_in[12] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0, 0};   /* 16 KB in */
	request = (struct ar_frame){.reqid = 0x04000000, .msgid = 44, .payload = big_in, .length = sizeof big_in};
	assert(ar_socket_rx_request(&sockets, &request, &reply) == 1 && reply.status);
	assert(sizeof sockets < 200000);   /* was 353 KB with four full-size ports */
	puts("AP five-write admission, completion order, payload integrity, ring reuse and small side ports passed");
	return 0;
}
