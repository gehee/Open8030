/* SPDX-License-Identifier: MIT */
/* Independent polling endpoint, Ascent VRX 18.21.10: dispatch 0x240782,
 * SDK-to-MAC mapping 0x24241c, RX worker 0x23bb5c, TX worker 0x24029e.
 * Host RF/session initialization remains separate. */
#include "ar_socket_rx.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
	return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static void put64(uint8_t *p, uint64_t value)
{
	for (unsigned i = 0; i < 8; i++) { p[i] = value >> (8 * i); }
}

static void write_request(struct ar_socket_rx *sockets, struct ar_socket_rx_port *state,
	const struct ar_frame *request, struct ar_frame *response)
{
	if (!state->flags) { response->status = -260; return; }
	if (!(state->flags & 2)) { response->status = -261; return; }
	if (request->length < 8 || request->length > AR_MAX_PAYLOAD) {
		response->status = AR_STATUS_INVALID; return;
	}
	uint64_t position = le64(request->payload);
	uint32_t length = request->length - 8, count = length;
	if (position != state->position) {
		response->status = -264; count = (uint32_t)state->position;
	} else if (state->pending_count == AR_SOCKET_TX_PENDING) {
		response->status = -263;
	} else {
		struct ar_socket_write *write = &state->pending[
			(state->pending_head + state->pending_count) % AR_SOCKET_TX_PENDING];
		write->position = position; write->length = length; write->copied = 0;
		if (length) { memcpy(write->bytes, request->payload + 8, length); }
		state->pending_count++; state->position += length; state->enqueued++;
		response->status = -262;
	}
	if (response->status != -262) { state->rejected++; }
	/* Original dispatch emits 16 bytes, the last word being stack residue.
	 * Zero that reserved word; preserve position and the returned count. */
	memset(sockets->error_payload, 0, sizeof sockets->error_payload);
	put64(sockets->error_payload, position);
	for (unsigned i = 0; i < 4; i++) { sockets->error_payload[8+i] = count >> (8*i); }
	response->payload = sockets->error_payload; response->length = 16;
}

void ar_socket_rx_init(struct ar_socket_rx *sockets)
{
	if (sockets) { memset(sockets, 0, sizeof *sockets); }
}

static void open_error(struct ar_socket_rx *sockets, struct ar_frame *response, int32_t error)
{
	for (unsigned i = 0; i < 4; i++) { sockets->error_payload[i] = (uint32_t)error >> (8 * i); }
	response->status = -259; response->payload = sockets->error_payload; response->length = 4;
}

int ar_socket_rx_request(struct ar_socket_rx *sockets, const struct ar_frame *request,
	struct ar_frame *response)
{
	if (!sockets || !request || !response || request->reqid >> 24 != 4) { return 0; }
	*response = (struct ar_frame){.reqid = request->reqid, .msgid = request->msgid};
	unsigned slot = request->reqid >> 8 & 255, port = request->reqid & 255;
	unsigned command = request->reqid >> 16 & 255;
	if (request->status || (request->length && !request->payload)) {
		response->status = AR_STATUS_INVALID; return 1;
	}
	if (command != 0 && command != 1 && command != 3) { response->status = AR_STATUS_UNSUPPORTED; return 1; }
	if (slot || port >= AR_SOCKET_RX_PORTS) {
		if (command == 0) { open_error(sockets, response, -6); }
		else { response->status = command == 1 ? -260 : 0x103; }
		return 1;
	}
	struct ar_socket_rx_port *state = &sockets->ports[port];
	if (command == 1) { write_request(sockets, state, request, response); return 1; }
	if (command == 3) {
		if (request->length) { response->status = AR_STATUS_INVALID; }
		else if (!state->flags) { response->status = 0x103; }
		else { memset(state, 0, sizeof *state); }
		return 1;
	}
	if (request->length != 12) { response->status = AR_STATUS_INVALID; return 1; }
	if (state->flags) { response->status = 0x101; return 1; }
	uint32_t flags = le32(request->payload), capacity = le32(request->payload + 8);
	uint32_t tx_capacity = le32(request->payload + 4);
	/* Retained-descriptor TX (0x20) has a different capacity contract and is
	 * still unsupported. The matched backend opens plain copy mode, flags3. */
	if (flags != 1 && flags != 2 && flags != 3 && flags != 0x21) {
		response->status = AR_STATUS_UNSUPPORTED; return 1;
	}
	if (((flags & 1) && !capacity) || ((flags & 2) && !tx_capacity)) {
		open_error(sockets, response, -6); return 1;
	}
	if (((flags & 1) && capacity > AR_SOCKET_RX_CAPACITY) ||
	    ((flags & 2) && tx_capacity > AR_SOCKET_TX_CAPACITY)) {
		open_error(sockets, response, -3); return 1;
	}
	memset(state, 0, sizeof *state);
	state->flags = flags;
	state->capacity = (flags & 1) ? capacity : 0;
	state->tx_capacity = (flags & 2) ? tx_capacity : 0;
	/* Stock stores the open message ID in a halfword at socket +4. */
	state->msgid = request->msgid & 65535;
	return 1;
}

size_t ar_socket_rx_feed(struct ar_socket_rx *sockets, unsigned slot, unsigned mac_port,
	const uint8_t *data, size_t length)
{
	if (!sockets || slot || !mac_port || mac_port > AR_SOCKET_RX_PORTS || (!data && length)) { return 0; }
	struct ar_socket_rx_port *state = &sockets->ports[mac_port - 1];
	if (!state->capacity) { return 0; }
	size_t copied = state->capacity - state->used;
	if (copied > length) { copied = length; }
	state->offered += length; state->accepted += copied; state->dropped += length - copied;
	size_t tail = (state->head + state->used) % state->capacity;
	size_t first = state->capacity - tail;
	if (first > copied) { first = copied; }
	if (first) { memcpy(state->bytes + tail, data, first); }
	if (copied > first) { memcpy(state->bytes, data + first, copied - first); }
	state->used += copied;
	return copied;
}

int ar_socket_rx_peek(struct ar_socket_rx *sockets, uint8_t *scratch, size_t capacity,
	struct ar_frame *frame)
{
	if (!sockets || !scratch || !capacity || !frame) { return 0; }
	if (capacity > AR_MAX_PAYLOAD) { capacity = AR_MAX_PAYLOAD; }
	for (unsigned i = 0; i < AR_SOCKET_RX_PORTS; i++) {
		unsigned port = (sockets->next_port + i) % AR_SOCKET_RX_PORTS;
		struct ar_socket_rx_port *state = &sockets->ports[port];
		if (!state->used) { continue; }
		size_t copied = state->used < capacity ? state->used : capacity;
		size_t first = state->capacity - state->head;
		if (first > copied) { first = copied; }
		memcpy(scratch, state->bytes + state->head, first);
		if (copied > first) { memcpy(scratch + first, state->bytes, copied - first); }
		*frame = (struct ar_frame){.reqid = 0x04020000u | port, .msgid = state->msgid,
			.payload = scratch, .length = copied};
		return 1;
	}
	return 0;
}

void ar_socket_rx_commit(struct ar_socket_rx *sockets, const struct ar_frame *frame)
{
	if (!sockets || !frame || (frame->reqid & 0xfffffffc) != 0x04020000) { return; }
	unsigned port = frame->reqid & 3;
	struct ar_socket_rx_port *state = &sockets->ports[port];
	if (!state->capacity || frame->msgid != state->msgid || frame->length > state->used) { return; }
	state->head = (state->head + frame->length) % state->capacity;
	state->used -= frame->length; state->delivered += frame->length;
	sockets->next_port = (port + 1) % AR_SOCKET_RX_PORTS;
}

void ar_socket_tx_service(struct ar_socket_rx *sockets)
{
	if (!sockets) { return; }
	for (unsigned port = 0; port < AR_SOCKET_RX_PORTS; port++) {
		struct ar_socket_rx_port *state = &sockets->ports[port];
		if (!state->pending_count) { continue; }
		struct ar_socket_write *write = &state->pending[state->pending_head];
		size_t count = write->length - write->copied;
		if (count > state->tx_capacity - state->tx_used) { count = state->tx_capacity - state->tx_used; }
		if (!count) { continue; }
		size_t tail = (state->tx_head + state->tx_used) % state->tx_capacity;
		size_t first = state->tx_capacity - tail;
		if (first > count) { first = count; }
		memcpy(state->tx_bytes + tail, write->bytes + write->copied, first);
		if (count > first) { memcpy(state->tx_bytes, write->bytes + write->copied + first, count - first); }
		state->tx_used += count; write->copied += count;
	}
}

int ar_socket_tx_completion(struct ar_socket_rx *sockets, uint8_t scratch[8], struct ar_frame *frame)
{
	if (!sockets || !scratch || !frame) { return 0; }
	for (unsigned i = 0; i < AR_SOCKET_RX_PORTS; i++) {
		unsigned port = (sockets->next_completion + i) % AR_SOCKET_RX_PORTS;
		struct ar_socket_rx_port *state = &sockets->ports[port];
		if (!state->pending_count) { continue; }
		struct ar_socket_write *write = &state->pending[state->pending_head];
		if (write->copied != write->length) { continue; }
		put64(scratch, write->position);
		*frame = (struct ar_frame){.reqid = 0x04010000u | port, .msgid = state->msgid,
			.status = write->length ? (int32_t)write->length : -7, .payload = scratch, .length = 8};
		return 1;
	}
	return 0;
}

void ar_socket_tx_complete(struct ar_socket_rx *sockets, const struct ar_frame *frame)
{
	if (!sockets || !frame || (frame->reqid & 0xfffffffc) != 0x04010000 ||
	    frame->length != 8 || !frame->payload) { return; }
	unsigned port = frame->reqid & 3;
	struct ar_socket_rx_port *state = &sockets->ports[port];
	if (!state->pending_count || frame->msgid != state->msgid) { return; }
	struct ar_socket_write *write = &state->pending[state->pending_head];
	if (write->copied != write->length || write->position != le64(frame->payload) ||
	    frame->status != (write->length ? (int32_t)write->length : -7)) { return; }
	state->pending_head = (state->pending_head + 1) % AR_SOCKET_TX_PENDING;
	state->pending_count--; state->completed++;
	sockets->next_completion = (port + 1) % AR_SOCKET_RX_PORTS;
}

size_t ar_socket_tx_peek(struct ar_socket_rx *sockets, uint8_t *scratch,
	size_t capacity, unsigned *mac_port)
{
	if (!sockets || !scratch || !capacity || !mac_port) { return 0; }
	for (unsigned i = 0; i < AR_SOCKET_RX_PORTS; i++) {
		unsigned port = (sockets->next_tx_port + i) % AR_SOCKET_RX_PORTS;
		struct ar_socket_rx_port *state = &sockets->ports[port];
		if (!state->tx_used) { continue; }
		size_t count = state->tx_used < capacity ? state->tx_used : capacity;
		size_t first = state->tx_capacity - state->tx_head;
		if (first > count) { first = count; }
		memcpy(scratch, state->tx_bytes + state->tx_head, first);
		if (count > first) { memcpy(scratch + first, state->tx_bytes, count - first); }
		*mac_port = port + 1;
		return count;
	}
	return 0;
}

void ar_socket_tx_commit(struct ar_socket_rx *sockets, unsigned mac_port, size_t length)
{
	if (!sockets || !mac_port || mac_port > AR_SOCKET_RX_PORTS || !length) { return; }
	struct ar_socket_rx_port *state = &sockets->ports[mac_port - 1];
	if (!state->tx_capacity || length > state->tx_used) { return; }
	state->tx_head = (state->tx_head + length) % state->tx_capacity;
	state->tx_used -= length; state->submitted += length;
	sockets->next_tx_port = mac_port % AR_SOCKET_RX_PORTS;
}
