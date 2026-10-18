/* SPDX-License-Identifier: MIT */
/* AP link control against frames a stock 18.21.10 AP sent (captured by an
 * Open8030 DEV) and against the DEV's own admission and login checks. */
#include "ar_ap.h"
#include "ar_dev.h"
#include "ar_dev_tx.h"
#include "ar_mac.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t ap_mac[4] = {0x12, 0x34, 0x56, 0x78}, gnd_mac[4] = {0x9a, 0xbc, 0xde, 0xf0};

/* Captured from the stock AP, the two addresses replaced (byte 2, the check,
 * recomputed): idle broadcast (sequence 0x25), the login broadcast (sequence
 * 0x2b, token 0x5a) and a compact control (sequence 0x2f). */
static const uint8_t stock_idle[39] = {
	0x27, 0x25, 0x14, 0x40, 0x01, 0x00, 0x0f, 0x0f, 0x12, 0x34, 0x56, 0x78, 0x01, 0x00, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x9a, 0xbc, 0xde, 0xf0, 0x01, 0x50};
static const uint8_t stock_login[45] = {
	0x2d, 0x2b, 0x0a, 0x40, 0x01, 0x00, 0x0f, 0x0f, 0x12, 0x34, 0x56, 0x78, 0x05, 0x5a, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0x09, 0x00, 0x00, 0x00, 0x01, 0x5a, 0x0f, 0x9a, 0xbc, 0xde, 0xf0, 0, 0, 0, 0,
	0x01, 0x50};
static const uint8_t stock_compact[6] = {0x05, 0x2f, 0x5a, 0x02, 0x50, 0x00};

static struct ar_ap_config config(void)
{
	struct ar_ap_config c = {.ground_count = 1, .channel = 15, .status = 0x50, .login_value = 0x0f, .timeout_ms = 500};
	memcpy(c.local, ap_mac, 4); memcpy(c.grounds[0], gnd_mac, 4);
	return c;
}

static void report(uint8_t out[16], unsigned state, unsigned slot, unsigned channel, const uint8_t identity[4])
{
	struct ar_dev_report r = {.state = (uint8_t)state, .assigned_slot = (uint8_t)slot,
		.receive_channel = (uint8_t)channel, .slot_value = 10, .status_a = 2, .status_b = 2};
	memcpy(r.local, identity, 4);
	assert(ar_dev_report_encode(out, 16, &r) == AR_MAC_OK);
}

int main(void)
{
	struct ar_ap_config c = config();
	struct ar_ap ap;
	ar_ap_init(&ap, &c, 0x5a);
	uint8_t ob[200];

	/* Idle: byte for byte what the stock AP broadcasts. */
	ap.sequence = 0x25;
	size_t n = ar_ap_control(&ap, ob, sizeof ob);
	assert(n == sizeof stock_idle && !memcmp(ob, stock_idle, n));
	assert(ap.sequence == 0x26);
	assert(ar_ap_control(&ap, ob, 38) == 0);

	/* The DEV admits it: itself first in extension 2, the AP a candidate, slot 0 free. */
	struct ar_dev_control control;
	assert(ar_dev_control_decode(stock_idle, sizeof stock_idle, &control) == 0);
	struct ar_dev_idle_config dev = {.channel_count = 42, .receive_channel = 15, .slot_bitmap = 1,
		.candidate_count = 1};
	uint8_t candidates[1][4]; memcpy(candidates[0], ap_mac, 4); dev.candidates = candidates;
	memcpy(dev.local, gnd_mac, 4);
	struct ar_dev_acquisition acquisition;
	assert(ar_dev_idle_evaluate(&dev, &control, &acquisition) == AR_DEV_READY);
	assert(acquisition.selected_slot == 0 && !memcmp(acquisition.peer, ap_mac, 4));

	/* Reports that do not start a login: bad check, a ground not served, the
	 * wrong channel, a ground already claiming a slot. */
	uint8_t r[16];
	report(r, 1, 8, 15, gnd_mac); r[9] ^= 1;
	assert(ar_ap_report(&ap, r, 16, 0) == -74 && ap.bad_reports == 1);
	uint8_t other[4] = {1, 2, 3, 4};
	report(r, 1, 8, 15, other);
	assert(ar_ap_report(&ap, r, 16, 0) == 0 && ap.state == AR_AP_IDLE && ap.foreign_reports == 1);
	report(r, 1, 8, 14, gnd_mac);
	assert(ar_ap_report(&ap, r, 16, 0) == 0 && ap.state == AR_AP_IDLE);
	report(r, 2, 0, 15, gnd_mac);
	assert(ar_ap_report(&ap, r, 16, 0) == 0 && ap.state == AR_AP_IDLE);

	/* Our ground answers: login, byte for byte the stock login broadcast. */
	report(r, 1, 8, 15, gnd_mac);
	assert(ar_ap_report(&ap, r, 16, 100) == 1 && ap.state == AR_AP_LOGIN && ap.logins == 1);
	ap.sequence = 0x2b;
	n = ar_ap_control(&ap, ob, sizeof ob);
	assert(n == sizeof stock_login && !memcmp(ob, stock_login, n));
	/* The DEV's lock check finds its assignment: slot 0, our token, AP state 2, sync 0. */
	assert(ar_dev_control_decode(ob, n, &control) == 0);
	struct ar_dev_lock_context lock = {.selected_slot = 0, .assigned_slot = 8};
	memcpy(lock.peer, ap_mac, 4);
	struct ar_dev_login login;
	assert(ar_dev_lock_evaluate(&dev, &lock, &control, &login) == AR_DEV_LOCK_ASSIGNMENT);
	assert(login.slot == 0 && login.token == 0x5a && login.ap_state == 2 && login.value == 0x0f &&
		!login.sync[0]);

	/* A wrong ACK is counted and ignored; the right one connects. */
	uint8_t check = 0;
	for (unsigned i = 0; i < 4; i++) { check = ar_mac_crc8_step(check ^ login.sync[i]); }
	uint8_t ack[5], wrong = (uint8_t)(check ^ 1);
	assert(ar_dev_command_encode(ack, sizeof ack, 4, &wrong, 1) == 0);
	ar_ap_command(&ap, ack, sizeof ack, 110);
	assert(ap.state == AR_AP_LOGIN && ap.bad_acks == 1);
	assert(ar_dev_command_encode(ack, sizeof ack, 4, &check, 1) == 0);
	ar_ap_command(&ap, ack, sizeof ack, 120);
	assert(ap.state == AR_AP_CONNECTED && ap.acks == 1 && ap.connects == 1);

	/* Connected: the compact control, byte for byte, and the DEV decodes it. */
	ap.sequence = 0x2f;
	n = ar_ap_control(&ap, ob, sizeof ob);
	assert(n == 6 && !memcmp(ob, stock_compact, 6));
	struct ar_dev_compact compact;
	assert(ar_dev_compact_decode(ob, n, &compact) == 0 && compact.token == 0x5a && compact.state == 2);

	/* A state-2 report also connects (ACK lost); reports keep it alive. */
	struct ar_ap again; ar_ap_init(&again, &c, 7);
	report(r, 1, 8, 15, gnd_mac); assert(ar_ap_report(&again, r, 16, 0) == 1);
	report(r, 2, 0, 15, gnd_mac); assert(ar_ap_report(&again, r, 16, 10) == 1 && again.state == AR_AP_CONNECTED);
	assert(ar_ap_poll(&again, 400) == 0);
	assert(ar_ap_report(&again, r, 16, 400) == 0 && ar_ap_poll(&again, 800) == 0);

	/* Reports stop: the ground is dropped, the next login takes a new token. */
	assert(ar_ap_poll(&again, 900) == 1 && again.state == AR_AP_IDLE && again.drops == 1 && again.token == 8);
	n = ar_ap_control(&again, ob, sizeof ob);
	assert(n == 39 && ob[12] == 1 && ob[13] == 0);
	/* A ground that falls back to state 1 while connected is dropped too. */
	report(r, 1, 8, 15, gnd_mac); assert(ar_ap_report(&again, r, 16, 1000) == 1 && again.state == AR_AP_LOGIN);
	report(r, 2, 0, 15, gnd_mac); assert(ar_ap_report(&again, r, 16, 1010) == 1);
	report(r, 1, 8, 15, gnd_mac); assert(ar_ap_report(&again, r, 16, 1020) == 1 && again.state == AR_AP_IDLE);

	/* Reconfiguring away the connected ground drops it; pairing serves anyone. */
	report(r, 1, 8, 15, gnd_mac); assert(ar_ap_report(&again, r, 16, 1100) == 1);
	struct ar_ap_config none = c; none.ground_count = 0;
	ar_ap_configure(&again, &none);
	assert(again.state == AR_AP_IDLE);
	n = ar_ap_control(&again, ob, sizeof ob);
	assert(n == 35 && ob[32] == 0 && ob[33] == 1 && ob[34] == 0x50);
	none.pairing = 1; ar_ap_configure(&again, &none);
	report(r, 1, 8, 15, other); assert(ar_ap_report(&again, r, 16, 1200) == 1 && !memcmp(again.ground, other, 4));

	/* Bandwidth request from the ground (port-0 command 9). */
	uint8_t code = 4, bw[5];
	assert(ar_dev_command_encode(bw, sizeof bw, 9, &code, 1) == 0);
	ar_ap_command(&again, bw, sizeof bw, 1300);
	assert(again.bandwidth_request == 5);
	puts("AP broadcast (stock bytes), login, ACK, compact control, loss and pairing passed");
	return 0;
}
