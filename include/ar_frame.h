/* SPDX-License-Identifier: MIT */
#ifndef AR_FRAME_H
#define AR_FRAME_H
#include <stddef.h>
#include <stdint.h>

#define AR_HEADER_SIZE 18u
#define AR_OVERHEAD 19u
#define AR_MAX_PAYLOAD 4096u
#define AR_MAX_FRAME (AR_MAX_PAYLOAD + AR_OVERHEAD)
#define AR_REQ_HELLO UINT32_C(0xff000000)
#define AR_REQ_RESET UINT32_C(0xff000001)
#define AR_REQ_HEARTBEAT UINT32_C(0xff000002)
/* Project diagnostic extension; this is not a vendor RF command. */
#define AR_REQ_DIAGNOSTICS UINT32_C(0x0c5a0000)
#define AR_REQ_TX_BURST UINT32_C(0x0c5a0001)
#define AR_REQ_OP_AD8 UINT32_C(0x0c5a0002)
#define AR_REQ_OP_AD8_S8 UINT32_C(0x0c5a0003)
#define AR_REQ_OP_REV_S0 UINT32_C(0x0c5a0004)
#define AR_REQ_RF_PHY_INFO UINT32_C(0x0c5a001b)
#define AR_REQ_RF_PROBE UINT32_C(0x0c5a0010)
#define AR_REQ_RF_DEFAULTS UINT32_C(0x0c5a0011)
#define AR_REQ_RF_CALIBRATE UINT32_C(0x0c5a0012)
#define AR_REQ_RF_PSRAM UINT32_C(0x0c5a0013)
#define AR_REQ_RF_PINS UINT32_C(0x0c5a0015)
#define AR_REQ_RF_MONITOR UINT32_C(0x0c5a0016)
#define AR_REQ_RF_RX_INFO UINT32_C(0x0c5a0017)
#define AR_REQ_RF_REGISTERS UINT32_C(0x0c5a0018)
#define AR_REQ_RF_PLL UINT32_C(0x0c5a0019)
#define AR_REQ_SOCKET_RX_INFO UINT32_C(0x0c5a001a)
#define AR_REQ_SOCKET_TX_INFO UINT32_C(0x0c5a001c)
#define AR_REQ_RF_TX_PAYLOAD_INFO UINT32_C(0x0c5a001d)
#define AR_REQ_RF_MCS_INFO UINT32_C(0x0c5a001e)
#define AR_REQ_RF_PHY_TRACE UINT32_C(0x0c5a001f)
#define AR_REQ_RF_SYNC_INFO UINT32_C(0x0c5a0020)
#define AR_REQ_RF_TRAFFIC_INFO UINT32_C(0x0c5a0021)
#define AR_REQ_RF_TX_MCS_INFO UINT32_C(0x0c5a0022)
#define AR_REQ_HOST_AUDIT UINT32_C(0x0c5a0023)
#define AR_REQ_BOOT_INFO UINT32_C(0x0c5a0024)
#define AR_REQ_RF_LIFECYCLE_INFO UINT32_C(0x0c5a0025)
#define AR_REQ_RF_RECOVERY_INFO UINT32_C(0x0c5a0026)
#define AR_REQ_RF_SCAN_INFO UINT32_C(0x0c5a0027)
#define AR_REQ_RF_TX_HISTORY_INFO UINT32_C(0x0c5a0028)
/* Project-only read diagnostics through the original Linux daemon. Its
 * 0x4030d0 dispatcher consumes group 0x0c locally; group 0x01 is forwarded.
 * These aliases are not vendor commands. Never alias configuration/start. */
#define AR_DAEMON_DIAGNOSTIC_BASE UINT32_C(0x015a0000)
static inline uint32_t ar_diagnostic_request_id(uint32_t request)
{
	if ((request & UINT32_C(0xffff0000)) == AR_DAEMON_DIAGNOSTIC_BASE) {
		switch (request & 0xffff) {
		case 0: case 0x17: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
		case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28:
			return UINT32_C(0x0c5a0000) | (request & 0xffff);
		default: break;
		}
	}
	return request;
}
/* Stable diagnostic wire statuses, independent of the libc errno namespace. */
#define AR_STATUS_INVALID (-22)
#define AR_STATUS_UNSUPPORTED (-95)

struct ar_frame {
	uint32_t reqid;
	uint32_t msgid;
	int32_t status;
	const uint8_t *payload;
	uint32_t length;
};

struct ar_parser {
	uint8_t bytes[AR_MAX_FRAME];
	size_t used;
	uint32_t rejected;
};

typedef void (*ar_frame_callback)(const struct ar_frame *, void *);
/* Payload may equal dst + AR_HEADER_SIZE for in-place framing. Other input
 * overlap with the output is not supported. */
size_t ar_encode(uint8_t *dst, size_t capacity, const struct ar_frame *frame);
void ar_feed(struct ar_parser *parser, const uint8_t *bytes, size_t length,
	     ar_frame_callback callback, void *context);
void ar_dispatch(const struct ar_frame *request, uint8_t *reply, size_t *length);
/* Project RX diagnostic: LE16 offset/count, at most 1024 bytes per read.
 * Empty direct requests retain the complete record; daemon aliases require a
 * range because the matched daemon cannot safely forward the complete record.
 * Returned storage is borrowed until the caller encodes the response. */
void ar_rx_diagnostic_read(const struct ar_frame *request, const uint8_t *record,
			  size_t size, struct ar_frame *response);
#endif
