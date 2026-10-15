/* SPDX-License-Identifier: MIT */
#include "ar_host_telemetry.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static uint32_t crc32c(const uint8_t *p, size_t n)
{
	/* Application CRC32C uses zero initial value and no final complement. */
	uint32_t crc = 0;
	while (n--) {
		crc ^= *p++;
		for (unsigned i = 0; i < 8; i++) { crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78u : 0); }
	}
	return crc;
}
static uint8_t crc8(const uint8_t *p, size_t n)
{
	uint8_t crc = 0;
	while (n--) {
		crc ^= *p++;
		for (unsigned i = 0; i < 8; i++) { crc = (crc << 1) ^ ((crc & 128) ? 0xd5 : 0); }
	}
	return crc;
}
static void consume(uint8_t *p, size_t *used, size_t n)
{
	*used -= n; memmove(p, p + n, *used);
}
static void record(struct ar_host_telemetry *s, unsigned index, const uint8_t *p,
	size_t n, size_t minimum, uint32_t now)
{
	if (n < minimum) { s->info.short_records++; return; }
	struct ar_host_telemetry_record *r = &s->info.records[index];
	r->updated_ms = now; r->count++;
	r->length = n < sizeof r->data ? n : sizeof r->data;
	memset(r->data, 0, sizeof r->data); memcpy(r->data, p, r->length);
	s->info.seen |= 1u << index;
}
static void msp_record(struct ar_host_telemetry *s, unsigned command, const uint8_t *p,
	size_t n, uint32_t now)
{
	s->info.msp_frames++;
	switch (command) {
	case 2: record(s, 1, p, n, 4, now); break;
	case 3: record(s, 2, p, n, 3, now); break;
	case 101: record(s, 3, p, n, 11, now); break;
	case 110: record(s, 4, p, n, 7, now); break;
	default: s->info.ignored++; break;
	}
}
static void msp_feed(struct ar_host_telemetry *s, const uint8_t *p, size_t n, uint32_t now)
{
	if (s->msp_used && (uint32_t)(now - s->last_msp_ms) > 2000) {
		s->msp_used = 0; s->info.partial_resets++;
	}
	s->last_msp_ms = now;
	while (n--) {
		s->msp[s->msp_used++] = *p++;
		while (s->msp_used) {
			uint8_t *b = s->msp; size_t used = s->msp_used;
			if (b[0] != '$' || (used > 1 && b[1] != 'M' && b[1] != 'X') ||
				(used > 2 && b[2] != '>')) { consume(b, &s->msp_used, 1); continue; }
			unsigned v2 = used > 1 && b[1] == 'X';
			size_t header = v2 ? 8 : 5;
			if (used < header) { break; }
			size_t length = v2 ? le16(b + 6) : b[3];
			if (length > 512 || (!v2 && length == 255)) {
				s->info.msp_oversize++; consume(b, &s->msp_used, 1); continue;
			}
			size_t total = header + length + 1;
			if (used < total) { break; }
			uint8_t check = 0;
			if (v2) { check = crc8(b + 3, length + 5); }
			else { for (size_t i = 3; i < total - 1; i++) { check ^= b[i]; } }
			if (check != b[total - 1]) { s->info.msp_bad++; consume(b, &s->msp_used, 1); continue; }
			unsigned command = v2 ? le16(b + 4) : b[4];
			if (!v2 && command == 255) {
				/* MSPv2 inside an MSPv1 envelope has both checksums. */
				if (length < 6 || le16(b + 8) != length - 6 ||
					crc8(b + 5, length - 1) != b[5 + length - 1]) { s->info.msp_bad++; }
				else { msp_record(s, le16(b + 6), b + 10, length - 6, now); }
			} else { msp_record(s, command, b + header, length, now); }
			consume(b, &s->msp_used, total);
		}
	}
}
void ar_host_telemetry_init(struct ar_host_telemetry *s)
{
	memset(s, 0, sizeof *s); s->info.version = 1;
}
void ar_host_telemetry_link(struct ar_host_telemetry *s, int linked)
{
	if (s->info.linked == (uint32_t)!!linked) { return; }
	if (s->used || s->msp_used) { s->info.partial_resets++; }
	s->used = s->msp_used = 0;
	s->info.seen = 0; memset(s->info.records, 0, sizeof s->info.records);
	s->info.linked = !!linked;
	if (linked) { s->info.sessions++; }
}
void ar_host_telemetry_feed(struct ar_host_telemetry *s, const uint8_t *p, size_t n, uint32_t now)
{
	if (!s || !s->info.linked || (!p && n)) { return; }
	s->info.now_ms = now; s->info.bytes += n;
	if (s->used && (uint32_t)(now - s->last_outer_ms) > 2000) {
		s->used = s->msp_used = 0; s->info.partial_resets++;
	}
	s->last_outer_ms = now;
	while (n--) {
		s->outer[s->used++] = *p++;
		while (s->used) {
			uint8_t *b = s->outer; size_t used = s->used;
			if (b[0] != 0xfe || (used > 1 && b[1] != 0xa5) ||
				(used >= 6 && b[2] != (uint8_t)(b[3] + b[4] + b[5]))) {
				s->info.bad_header++; s->msp_used = 0; consume(b, &s->used, 1); continue;
			}
			if (used < 6) { break; }
			size_t length = le16(b + 4) >> 4, total = length + 11;
			if (used < total) { break; }
			if (crc32c(b, total - 4) != le32(b + total - 4)) {
				s->info.bad_crc++; s->msp_used = 0; consume(b, &s->used, 1); continue;
			}
			s->info.frames++;
			if ((b[4] & 15) || !length) { s->info.ignored++; }
			else if (b[6] == 5) { record(s, 0, b + 6, length, 15, now); }
			else if (b[6] == 2) { msp_feed(s, b + 7, length - 1, now); }
			else { s->info.ignored++; }
			consume(b, &s->used, total);
		}
	}
}
