/* SPDX-License-Identifier: MIT */
#ifndef AR_HOST_TELEMETRY_H
#define AR_HOST_TELEMETRY_H
#include <stddef.h>
#include <stdint.h>

/* Passive observer of MAC port 3. No queue ownership or transmit operations. */
#define AR_HOST_TELEMETRY_RECORDS 5
#define AR_HOST_TELEMETRY_RECORD_BYTES 64
struct ar_host_telemetry_record {
	uint32_t updated_ms, count, length;
	uint8_t data[AR_HOST_TELEMETRY_RECORD_BYTES];
};
/* LE32 wire snapshot: VTX type 5, MSP variant(2), version(3), status(101),
 * analog(110), in that order. Length is the stored prefix, at most 64 bytes.
 * Seen distinguishes a valid zero value/timestamp from absence. */
struct ar_host_telemetry_info {
	uint32_t version, linked, now_ms, seen;
	uint32_t sessions, bytes, frames, bad_header;
	uint32_t bad_crc, ignored, msp_frames, msp_bad;
	uint32_t short_records, partial_resets, msp_oversize, reserved;
	struct ar_host_telemetry_record records[AR_HOST_TELEMETRY_RECORDS];
};
struct ar_host_telemetry {
	struct ar_host_telemetry_info info;
	uint32_t last_outer_ms, last_msp_ms;
	size_t used, msp_used;
	uint8_t outer[4106], msp[521];
};
void ar_host_telemetry_init(struct ar_host_telemetry *);
void ar_host_telemetry_link(struct ar_host_telemetry *, int linked);
void ar_host_telemetry_feed(struct ar_host_telemetry *, const uint8_t *, size_t, uint32_t now_ms);
#endif
