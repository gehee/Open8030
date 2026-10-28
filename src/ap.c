/* SPDX-License-Identifier: MIT */
/* AP link control: broadcast building, ground login and loss. See ar_ap.h. */
#include "ar_ap.h"
#include "ar_mac.h"
#include <string.h>

uint8_t ar_ap_checksum(const uint8_t *data, size_t length, size_t zero)
{
	uint8_t check = 0;
	for (size_t i = 0; i < length; i++) { check = ar_mac_crc8_step(check ^ (i == zero ? 0 : data[i])); }
	return check;
}

void ar_ap_init(struct ar_ap *ap, const struct ar_ap_config *config, uint8_t first_token)
{
	if (!ap || !config) { return; }
	memset(ap, 0, sizeof *ap);
	ap->config = *config;
	if (ap->config.ground_count > AR_AP_MAX_GROUNDS) { ap->config.ground_count = AR_AP_MAX_GROUNDS; }
	if (!ap->config.timeout_ms) { ap->config.timeout_ms = 1000; }
	/* Token 0 would make a login record look like an empty slot. */
	ap->token = first_token ? first_token : 1;
}

static int served(const struct ar_ap *ap, const uint8_t identity[4])
{
	if (ap->config.pairing) { return 1; }
	for (unsigned i = 0; i < ap->config.ground_count; i++) {
		if (!memcmp(ap->config.grounds[i], identity, 4)) { return 1; }
	}
	return 0;
}

static void drop(struct ar_ap *ap)
{
	ap->state = AR_AP_IDLE; ap->drops++;
	memset(ap->ground, 0, 4);
	/* That ground's width: a new link starts at 5 MHz until it asks (kept,
	 * a link lost at 40 MHz went back to 40 at the next login). */
	ap->bandwidth_request = 0;
	/* A new token for the next login: a ground still holding this one sees
	 * the change and lets go instead of keeping a stale lock. */
	ap->token = (uint8_t)(ap->token + 1) ? (uint8_t)(ap->token + 1) : 1;
}

void ar_ap_configure(struct ar_ap *ap, const struct ar_ap_config *config)
{
	if (!ap || !config) { return; }
	uint16_t timeout = ap->config.timeout_ms;
	ap->config = *config;
	if (ap->config.ground_count > AR_AP_MAX_GROUNDS) { ap->config.ground_count = AR_AP_MAX_GROUNDS; }
	if (!ap->config.timeout_ms) { ap->config.timeout_ms = timeout ? timeout : 1000; }
	if (ap->state != AR_AP_IDLE && !served(ap, ap->ground)) { drop(ap); }
}

size_t ar_ap_control(struct ar_ap *ap, uint8_t *out, size_t capacity)
{
	if (!ap || !out) { return 0; }
	uint8_t seq = ap->sequence;
	if (ap->state == AR_AP_CONNECTED) {
		if (capacity < 6) { return 0; }
		out[1] = seq; out[2] = ap->token; out[3] = 2; out[4] = ap->config.status; out[5] = 0;
		out[0] = ar_ap_checksum(out + 1, 5, 99);
		ap->sequence++;
		return 6;
	}
	/* Full form: 32 fixed bytes, then the extensions in bit order. */
	uint8_t c[200] = {0};
	size_t n = 32;
	unsigned extensions = 0;
	c[3] = 0x40;                /* mode 0, bit 6 as stock sends it */
	c[4] = 1;                   /* slot bitmap: slot 0 */
	c[6] = c[7] = ap->config.channel;
	memcpy(c + 8, ap->config.local, 4);
	/* Slot 0: available (1), or the login under way (token << 8 | 5). Pairing
	 * adds bit 3 to either: a ground in pairing mode looks for it before it
	 * logs in, and checks it again at every step of the login - dropped
	 * there, the ground tore its login down and started over. A ground not
	 * pairing refuses a slot with it. */
	uint16_t record = ap->state == AR_AP_LOGIN ? (uint16_t)(ap->token << 8 | 5) : 1;
	if (ap->config.pairing) { record |= 8; }
	c[12] = (uint8_t)record; c[13] = record >> 8;
	if (ap->state == AR_AP_LOGIN) {
		/* Extension 0: the login record for slot 0. */
		extensions |= 1;
		c[n++] = 1;
		c[n++] = ap->token; c[n++] = ap->config.login_value;
		memcpy(c + n, ap->ground, 4); n += 4;
		memcpy(c + n, ap->sync, 4); n += 4;
	} else {
		/* Extension 2: the grounds served; a ground must find itself first. */
		extensions |= 4;
		unsigned count = ap->config.ground_count;
		c[n++] = (uint8_t)count;
		for (unsigned i = 0; i < count; i++) { memcpy(c + n, ap->config.grounds[i], 4); n += 4; }
	}
	extensions |= 8;            /* extension 3: one status byte */
	c[n++] = 1; c[n++] = ap->config.status;
	c[28] = (uint8_t)extensions;
	if (capacity < n || n > 200) { return 0; }
	c[0] = (uint8_t)n; c[1] = seq;
	c[2] = ar_ap_checksum(c, n, 2);
	memcpy(out, c, n);
	ap->sequence++;
	return n;
}

int ar_ap_report(struct ar_ap *ap, const uint8_t *outband, size_t length, uint32_t now_ms)
{
	if (!ap || !outband) { return -22; }
	if (length < 16 || outband[0] != 16) { ap->bad_reports++; return -22; }
	uint8_t bytes[16];
	memcpy(bytes, outband, 16);
	uint16_t check = (uint16_t)(bytes[2] | bytes[3] << 8);
	if (ar_mac_checksum16(bytes, 16) != check) { ap->bad_reports++; return -74; }
	const uint8_t *identity = bytes + 8;
	unsigned state = bytes[4] & 3, slot = (bytes[4] >> 2) & 15;
	ap->reports++;
	ap->report_state = (uint8_t)state; ap->report_slot = (uint8_t)slot;
	ap->report_channel = bytes[5]; ap->report_value = bytes[6];
	if (ap->state != AR_AP_IDLE) {
		if (memcmp(identity, ap->ground, 4)) { ap->foreign_reports++; return 0; }
		ap->last_report_ms = now_ms;
		if (ap->state == AR_AP_LOGIN && state == 2 && slot == 0) {
			ap->state = AR_AP_CONNECTED; ap->connects++;
			return 1;
		}
		if (ap->state == AR_AP_CONNECTED && state != 2) {
			/* The ground let go (its own loss or a reset): start over. */
			drop(ap);
			return 1;
		}
		return 0;
	}
	/* Idle: a ground that found us (state 1, unassigned) and that we serve. */
	if (state != 1 || slot != 8 || bytes[5] != ap->config.channel || !served(ap, identity)) {
		if (!served(ap, identity)) { ap->foreign_reports++; }
		return 0;
	}
	memcpy(ap->ground, identity, 4);
	memset(ap->sync, 0, sizeof ap->sync);
	ap->state = AR_AP_LOGIN; ap->logins++;
	ap->last_report_ms = ap->login_ms = now_ms;
	return 1;
}

void ar_ap_command(struct ar_ap *ap, const uint8_t *envelope, size_t length, uint32_t now_ms)
{
	if (!ap || !envelope || length < 4 || envelope[0] != 0x4a) { return; }
	unsigned command = envelope[1], size = envelope[2];
	if (length < 4u + size) { return; }
	const uint8_t *payload = envelope + 4;
	if (command == 4 && size == 1) {
		/* The login ACK: the lookup checksum of the four sync bytes. */
		if (ap->state == AR_AP_IDLE) { return; }
		if (payload[0] != ar_ap_checksum(ap->sync, 4, 99)) { ap->bad_acks++; return; }
		ap->acks++; ap->last_report_ms = now_ms;
		if (ap->state == AR_AP_LOGIN) { ap->state = AR_AP_CONNECTED; ap->connects++; }
	} else if (command == 9 && size == 1 && payload[0] <= 5) {
		ap->bandwidth_request = (uint8_t)(payload[0] + 1);
	}
}

int ar_ap_poll(struct ar_ap *ap, uint32_t now_ms)
{
	if (!ap || ap->state == AR_AP_IDLE) { return 0; }
	/* A login that never completes - a ground that keeps answering state 1
	 * because its lock check refuses our record, or a stock ground that
	 * never acknowledges - would hold the slot for as long as it reported:
	 * no other ground could be served. Dropped after AR_AP_LOGIN_TIMEOUT_MS;
	 * it may log in again, with a new token. */
	if (ap->state == AR_AP_LOGIN && (uint32_t)(now_ms - ap->login_ms) >= AR_AP_LOGIN_TIMEOUT_MS) {
		drop(ap); ap->login_timeouts++;
		return 1;
	}
	if ((uint32_t)(now_ms - ap->last_report_ms) < ap->config.timeout_ms) { return 0; }
	drop(ap);
	return 1;
}
