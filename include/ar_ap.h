/* SPDX-License-Identifier: MIT */
#ifndef AR_AP_H
#define AR_AP_H
#include <stddef.h>
#include <stdint.h>

/* AP (transmitter) link control, mode 0, one ground in slot 0: what the AP
 * broadcasts and how it logs a ground in. Learned from what a stock 18.21.10
 * AP sends (captured by an Open8030 DEV) and from what the DEV checks
 * (dev.c): no hardware here, the radio integration feeds it and sends what
 * it builds.
 *
 * Idle: a full control (39 bytes) in every packet: slot bitmap 1, slot 0
 * available (record 0x0001), extension 2 the grounds it serves, extension 3
 * one status byte. A ground answers with a state-1 report (slot 8). Login: the
 * full control carries slot 0 as `token << 8 | 5` (AP state 2) and an
 * extension-0 record [token, value, ground identity, sync 0]. The ground
 * acknowledges with port-0 command 4 and reports state 2 in slot 0: from then
 * on every packet carries the six-byte compact control [check, sequence, token,
 * 2, status, 0]. Reports stopping for `timeout_ms` drop the ground; the next
 * login uses a new token, so a ground still locked to the old one lets go. */

#define AR_AP_MAX_GROUNDS 8
/* A login not acknowledged within this (the ground keeps reporting state 1,
 * or never acknowledges) is dropped, so the slot is offered again. A login
 * takes a few frames; two seconds is hundreds. */
#define AR_AP_LOGIN_TIMEOUT_MS 2000
struct ar_ap_config {
	uint8_t local[4];                       /* the AP's own identity */
	uint8_t grounds[AR_AP_MAX_GROUNDS][4];  /* grounds it serves, extension 2 */
	uint8_t ground_count;
	uint8_t channel;                        /* index in the channel list, both directions */
	uint8_t status;                         /* extension 3 and compact byte 4 (stock 0x50) */
	uint8_t login_value;                    /* login record byte 1 (stock 0x0f) */
	uint16_t timeout_ms;                    /* no report for this long: ground gone */
	uint8_t pairing;                        /* binding: slot 0 offered for pairing, any ground served */
};
enum ar_ap_state { AR_AP_IDLE = 0, AR_AP_LOGIN = 1, AR_AP_CONNECTED = 2 };
struct ar_ap {
	struct ar_ap_config config;
	uint8_t state, token, sequence;
	uint8_t ground[4];                      /* the ground being logged in / connected */
	uint8_t sync[4];
	uint32_t last_report_ms, login_ms;
	uint32_t reports, bad_reports, foreign_reports, logins, connects, drops, acks, bad_acks;
	uint32_t login_timeouts;                /* logins dropped unacknowledged */
	uint8_t report_state, report_slot, report_channel, report_value;
	uint8_t bandwidth_request;              /* last port-0 command 9 code + 1, 0 = none */
};
void ar_ap_init(struct ar_ap *ap, const struct ar_ap_config *config, uint8_t first_token);
/* Change what the idle broadcast offers (grounds, channel, pairing). A ground
 * already connected stays unless it is no longer served. */
void ar_ap_configure(struct ar_ap *ap, const struct ar_ap_config *config);
/* The outband for the next packet. Returns its length, or 0 if `capacity`
 * is too small. Full form while idle and logging in, compact once connected. */
size_t ar_ap_control(struct ar_ap *ap, uint8_t *out, size_t capacity);
/* A ground's uplink outband (its 16-byte report). Returns 1 if the link state
 * changed, 0 if not, negative if the report is not valid. */
int ar_ap_report(struct ar_ap *ap, const uint8_t *outband, size_t length, uint32_t now_ms);
/* One port-0 command envelope from the connected or logging-in ground. */
void ar_ap_command(struct ar_ap *ap, const uint8_t *envelope, size_t length, uint32_t now_ms);
/* Time-outs. Returns 1 if the ground was dropped. */
int ar_ap_poll(struct ar_ap *ap, uint32_t now_ms);
/* The lookup checksum (ar_mac_crc8_step) over `length` bytes, the byte at
 * index `zero` taken as 0: the full form's own check byte (index 2). Pass an
 * index past the end to take every byte. */
uint8_t ar_ap_checksum(const uint8_t *data, size_t length, size_t zero);
#endif
