/* SPDX-License-Identifier: MIT */
#include "ar_dev.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_dev_control decode(uint8_t *p)
{
	p[2] = 0;
	uint8_t check = 0;
	for (unsigned i = 0; i < p[0]; i++) { check = ar_mac_crc8_step(check ^ p[i]); }
	p[2] = check;
	struct ar_dev_control control;
	assert(ar_dev_control_decode(p, p[0], &control) == 0);
	return control;
}

static void lock_tests(void)
{
	uint8_t packet[113] = {32,1,0,0x40,1,0,2,2,1,2,3,4,1};
	struct ar_dev_idle_config config = {.local = {5,6,7,8}, .receive_channel = 2,
		.channel_count = 4, .slot_bitmap = 1};
	struct ar_dev_lock_context lock = {.peer = {1,2,3,4}, .assigned_slot = 8, .transmit_channel = 2};
	struct ar_dev_login login, empty = {0};
	struct ar_dev_control control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_WAIT_LOGIN);
	packet[12] = 5; control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_TEARDOWN);
	/* Check every packed bitmap, especially holes before the matching slot.
	 * Decoded views are borrowed; evaluating must preserve all input bytes. */
	packet[28] = 1;
	for (unsigned bitmap = 1; bitmap < 256; bitmap++) {
		unsigned count = 0, selected = 0;
		packet[32] = bitmap;
		for (unsigned slot = 0; slot < 8; slot++) {
			packet[12 + slot * 2] = 5; packet[13 + slot * 2] = slot + 9;
			if (!(bitmap & (1u << slot))) { continue; }
			uint8_t *entry = packet + 33 + 10 * count++;
			memset(entry, 0, 10); entry[0] = slot + 9; entry[1] = 17 + slot;
			selected = slot;
		}
		uint8_t *last = packet + 33 + (count - 1) * 10;
		memcpy(last + 2, config.local, 4); memcpy(last + 6, "\1\2\3\4", 4);
		packet[0] = 33 + 10 * count; control = decode(packet);
		uint8_t saved[113]; memcpy(saved, packet, sizeof saved);
		assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_ASSIGNMENT);
		assert(login.slot == selected && login.token == selected + 9 && login.value == selected + 17);
		assert(login.ap_state == 2 && !memcmp(login.sync, "\1\2\3\4", 4));
		assert(!memcmp(saved, packet, sizeof saved));
		last[0] ^= 1; control = decode(packet);
		assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_NO_POSITION);
		assert(!memcmp(&login, &empty, sizeof login));
	}
	/* Token gating precedes the matching record, even when a new record
	 * would otherwise match. A changed AP identity cannot replace the lock. */
	packet[0] = 43; packet[32] = 1; packet[33] = packet[13] = 9;
	memcpy(packet + 35, config.local, 4); lock.assigned_slot = 0; lock.token = 8;
	control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_TOKEN_CHANGED);
	lock.token = 9;
	for (unsigned state = 0; state < 4; state++) {
		packet[12] = 1 | state << 1; control = decode(packet);
		assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_ASSIGNMENT);
		assert(login.ap_state == state); /* Assignment never implies state 2. */
	}
	packet[8] ^= 1; control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_PEER);
	packet[8] ^= 1;
	config.pairing = 1;
	control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_PROFILE);
	packet[12] = 13; control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_ASSIGNMENT);
	assert(login.slot == 0 && login.ap_state == 2);
	config.pairing = 0;
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_PROFILE);
	packet[12] = 5; packet[8] ^= 1;
	packet[8] ^= 1; packet[7] = 3; control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_PROFILE);
	lock.transmit_channel = 3;
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_ASSIGNMENT);
	lock.transmit_channel = 4;
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_BAD_CONFIG);
	lock.transmit_channel = 2;
	packet[7] = 2; packet[3] = 1; control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_PROFILE);
	packet[3] = 0; packet[32] = 0; packet[0] = 33; control = decode(packet);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_NO_POSITION);
	lock.assigned_slot = 9;
	assert(ar_dev_lock_evaluate(&config, &lock, &control, &login) == AR_DEV_LOCK_BAD_CONFIG);
	lock.assigned_slot = 8;
	assert(ar_dev_lock_evaluate(NULL, &lock, &control, &login) == AR_DEV_LOCK_BAD_CONFIG);
	assert(ar_dev_lock_evaluate(&config, NULL, &control, &login) == AR_DEV_LOCK_BAD_CONFIG);
	assert(ar_dev_lock_evaluate(&config, &lock, NULL, &login) == AR_DEV_LOCK_BAD_CONFIG);
	assert(ar_dev_lock_evaluate(&config, &lock, &control, NULL) == AR_DEV_LOCK_BAD_CONFIG);
}

struct stream_capture { unsigned count; uint8_t bytes[3][132]; size_t lengths[3]; };
static void capture_command(const uint8_t *bytes, size_t length, void *context)
{
	struct stream_capture *capture = context;
	assert(capture->count < 3 && length <= 132);
	memcpy(capture->bytes[capture->count], bytes, length);
	capture->lengths[capture->count++] = length;
}

static void stream_tests(void)
{
	uint8_t commands[156] = {0};
	unsigned starts[] = {0,132,152}, lengths[] = {132,20,4};
	for (unsigned i = 0; i < 3; i++) {
		uint8_t *p = commands + starts[i]; p[0] = 0x4a; p[1] = i + 1; p[2] = lengths[i] - 4;
		for (unsigned j = 4; j < lengths[i]; j++) { p[j] = j; }
		uint8_t check = 0;
		for (unsigned j = 0; j < lengths[i]; j++) { check ^= p[j]; }
		p[3] = check;
	}
	/* Every split, including inside the length/check bytes and 128-byte body. */
	for (unsigned split = 0; split <= sizeof commands; split++) {
		struct ar_dev_stream stream; ar_dev_stream_init(&stream);
		struct stream_capture capture = {0};
		assert(ar_dev_stream_feed(&stream, commands, split, capture_command, &capture) == 0);
		assert(ar_dev_stream_feed(&stream, commands + split, sizeof commands - split, capture_command, &capture) == 0);
		assert(capture.count == 3 && !stream.used && !stream.discarded);
		for (unsigned i = 0; i < 3; i++) {
			assert(capture.lengths[i] == lengths[i] && !memcmp(capture.bytes[i], commands + starts[i], lengths[i]));
		}
	}
	struct ar_dev_stream stream; ar_dev_stream_init(&stream);
	for (unsigned i = 0; i < sizeof commands; i++) { assert(ar_dev_stream_feed(&stream, commands + i, 1, NULL, NULL) == 0); }
	assert(stream.commands[1] == 1 && stream.commands[2] == 1 && stream.commands[3] == 1);
	uint8_t noise[] = {1,2,3,4,5,6};
	ar_dev_stream_init(&stream); assert(ar_dev_stream_feed(&stream, noise, sizeof noise, NULL, NULL) == 0);
	assert(ar_dev_stream_feed(&stream, commands, sizeof commands, NULL, NULL) == 0);
	assert(stream.discarded == sizeof noise && stream.commands[3] == 1);
	/* Bad envelope is fully discarded, allowing the following packet through. */
	ar_dev_stream_init(&stream); commands[3] ^= 1;
	assert(ar_dev_stream_feed(&stream, commands, sizeof commands, NULL, NULL) == 0);
	assert(stream.checksum_errors == 1 && stream.discarded == 132 && stream.commands[2] == 1 && stream.commands[3] == 1);
	commands[3] ^= 1;
	uint8_t oversized[] = {0x4a,1,129,0};
	ar_dev_stream_init(&stream); assert(ar_dev_stream_feed(&stream, oversized, 4, NULL, NULL) == 0);
	assert(stream.length_errors == 1 && stream.discarded == 4 && !stream.used);
	assert(ar_dev_stream_feed(&stream, NULL, 1, NULL, NULL) == AR_MAC_FORMAT);
	assert(ar_dev_stream_feed(NULL, NULL, 0, NULL, NULL) == AR_MAC_FORMAT);
}

static void retx_tests(void)
{
	struct ar_dev_retx state;
	for (unsigned threshold = 1; threshold < 128; threshold++) {
		ar_dev_retx_init(&state, threshold);
		assert(ar_dev_retx_feed(&state, 7, 255) == AR_DEV_RETX_NONE);
		for (unsigned i = 1; i < threshold; i++) {
			assert(ar_dev_retx_feed(&state, 7, 255) == AR_DEV_RETX_NONE);
		}
		assert(ar_dev_retx_feed(&state, 7, 255) == AR_DEV_RETX_DISABLE);
		assert(state.phase == 2 && state.repeated == threshold);
		assert(ar_dev_retx_feed(&state, 7, 255) == AR_DEV_RETX_NONE && state.phase == 1);
		assert(ar_dev_retx_feed(&state, 9, 255) == AR_DEV_RETX_ENABLE && state.phase == 0);
		assert(state.last_id == 9 && state.repeated == 0);
	}
	/* Sequential wrap and skipped-one ID both count as advancement. */
	ar_dev_retx_init(&state, 6);
	for (unsigned i = 0; i < 64; i++) {
		assert(ar_dev_retx_feed(&state, i & 15, 255) == AR_DEV_RETX_NONE);
		assert(!state.repeated);
	}
	assert(ar_dev_retx_feed(&state, 1, 255) == AR_DEV_RETX_NONE && !state.repeated);
	/* Both comparisons use the old ID: from 1, request2 advances, request4
	 * is still a miss. An absent second ID does not create a miss. */
	assert(ar_dev_retx_feed(&state, 2, 4) == AR_DEV_RETX_NONE);
	assert(state.last_id == 2 && state.repeated == 1);
	assert(ar_dev_retx_feed(&state, 2, 3) == AR_DEV_RETX_NONE);
	assert(state.last_id == 3 && state.repeated == 0);
	struct ar_dev_retx before = state;
	assert(ar_dev_retx_feed(&state, 16, 255) == AR_DEV_RETX_INVALID);
	assert(!memcmp(&before, &state, sizeof state));
	ar_dev_retx_init(&state, 0); before = state;
	for (unsigned i = 0; i < 300; i++) { assert(ar_dev_retx_feed(&state, 7, 255) == AR_DEV_RETX_NONE); }
	assert(!memcmp(&before, &state, sizeof state));
	ar_dev_retx_init(&state, 255); assert(state.threshold == 0);
	assert(ar_dev_retx_feed(NULL, 0, 255) == AR_DEV_RETX_INVALID);
}

int main(void)
{
	/* Invented wire data. Extension 2 lists this receiver at position zero. */
	uint8_t packet[41] = {37,1,0,0x40,1,0,2,3,1,2,3,4,1};
	packet[28] = 4; packet[32] = 1;
	memcpy(packet + 33, "\5\6\7\10", 4);
	const uint8_t candidates[2][4] = {{9,10,11,12},{1,2,3,4}};
	struct ar_dev_idle_config config = {
		.local = {5,6,7,8}, .configured_peer = {9,9,9,9},
		.candidates = candidates, .candidate_count = 2,
		.receive_channel = 2, .channel_count = 4, .slot_bitmap = 1
	};
	struct ar_dev_acquisition plan, empty = {0};
	struct ar_dev_control control = decode(packet);
	struct ar_dev_idle_config saved = config;
	uint8_t saved_packet[41]; memcpy(saved_packet, packet, sizeof packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
	assert(plan.selected_slot == 0 && plan.available_slot == 0 && plan.local_position == 0);
	assert(plan.peer_hash == 4 && !plan.pairing && !plan.control_field_b00 && !plan.rebuild_frame);
	assert(plan.receive_channel == 2 && plan.transmit_channel == 3);
	assert(!memcmp(plan.peer, candidates[1], 4));
	assert(!memcmp(&config, &saved, sizeof config));
	assert(!memcmp(packet, saved_packet, sizeof packet));

	/* Both configured address and candidate lookup can admit a normal peer. */
	config.candidate_count = 1;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_PEER);
	assert(!memcmp(&plan, &empty, sizeof plan));
	memcpy(config.configured_peer, candidates[1], 4);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
	config = saved;
	config.receive_channel = 1;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_CHANNEL);
	config = saved; config.channel_count = 3;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_CHANNEL);
	config = saved; config.relay = 1;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_RELAY);
	packet[3] |= 0x20; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
	packet[3] &= ~0x20; control = decode(packet); config = saved;

	/* Missing, unknown and nonzero local list positions cannot be bypassed by
	 * a configured peer. Local-address list and candidates are distinct. */
	packet[28] = 0; packet[0] = 32; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_LOCAL_POSITION);
	packet[28] = 4; packet[0] = 37; packet[33] ^= 1; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_LOCAL_POSITION);
	packet[0] = 41; packet[32] = 2; memcpy(packet + 37, config.local, 4); control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_LOCAL_POSITION);
	packet[33] ^= 1; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY); /* First match wins. */
	packet[0] = 37; packet[32] = 1;

	/* No mode mismatch is silently treated as a lock. */
	packet[3] = 1; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_RECONFIGURE);
	assert(plan.mode == 1 && plan.slot_bitmap == 1 && plan.peer_hash == 0);
	packet[3] = 0; packet[4] = 3; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_RECONFIGURE);
	packet[4] = 1;

	/* Test every slot and state field; only enabled/free entries qualify.
	 * Mode 0 selects logical slot 0 even if the available entry is elsewhere. */
	for (unsigned slot = 0; slot < 8; slot++) {
		for (unsigned bits = 0; bits < 8; bits++) {
			memset(packet + 12, 0, 16); packet[12 + slot * 2] = bits;
			control = decode(packet);
			int result = ar_dev_idle_evaluate(&config, &control, &plan);
			assert(result == (bits == 1 ? AR_DEV_READY : AR_DEV_NO_SLOT));
			if (result == AR_DEV_READY) { assert(plan.selected_slot == 0 && plan.available_slot == slot); }
			else { assert(!memcmp(&plan, &empty, sizeof plan)); }
		}
	}
	memset(packet + 12, 0, 16); packet[12] = 1;
	for (unsigned mode = 0; mode < 4; mode++) {
		config.mode = mode; packet[3] = mode; control = decode(packet);
		assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
		assert(plan.selected_slot == ((mode == 1 || mode == 2) ? 8 : 0));
		assert(plan.rebuild_frame == (mode == 1) && plan.control_field_b00 == 1);
	}
	/* Mode 3 must also have the selected local position free. */
	packet[12] = 0; packet[14] = 1; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_NO_SLOT);
	config = saved; packet[3] = 0; packet[12] = 9;
	control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_PAIRING);
	config.pairing = 1;
	/* Pairing can admit an unknown peer/local address, but requires a matching
	 * advertised pairing slot. It does not silently become normal pairing. */
	packet[33] ^= 1; config.candidate_count = 0; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
	assert(plan.pairing && plan.local_position == 8 && plan.available_slot == 0);
	packet[12] = 1; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_PAIRING);
	packet[12] = 11; control = decode(packet); /* Pairing advertised but busy. */
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_NO_SLOT);
	packet[12] = 9; packet[33] ^= 1; control = decode(packet);
	memcpy(config.pairing_skip[2], control.peer, 4);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_PEER);
	config.candidate_count = 2;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
	assert(!plan.pairing);
	config = saved; config.pairing = 1; config.mode = 3; packet[3] = 3;
	packet[12] = 1; packet[18] = 9; control = decode(packet);
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_READY);
	assert(plan.selected_slot == 3 && plan.available_slot == 3 && plan.pairing);

	config = saved; config.candidates = NULL;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_BAD_CONFIG);
	config = saved; config.candidate_count = AR_DEV_MAX_CANDIDATES + 1;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_BAD_CONFIG);
	config = saved; config.channel_count = 0;
	assert(ar_dev_idle_evaluate(&config, &control, &plan) == AR_DEV_BAD_CONFIG);
	assert(ar_dev_idle_evaluate(NULL, &control, &plan) == AR_DEV_BAD_CONFIG);
	assert(ar_dev_idle_evaluate(&saved, NULL, &plan) == AR_DEV_BAD_CONFIG);
	assert(ar_dev_idle_evaluate(&saved, &control, NULL) == AR_DEV_BAD_CONFIG);
	lock_tests();
	retx_tests();
	stream_tests();
	puts("DEV idle admission and lock decision tests passed");
	return 0;
}
