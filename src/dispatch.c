/* SPDX-License-Identifier: MIT */
#include "ar_frame.h"

void ar_dispatch(const struct ar_frame *request, uint8_t *reply, size_t *length)
{
	/* Deliberately identifies this diagnostic firmware, not a stock radio. */
	/* Matched daemon 0x406250/0x405158: LE16 at +2 counts logical
	 * radio devices. The physical build implements one slot-0 device.
	 * The UART diagnostic build does not expose a physical radio. */
#ifdef CONFIG_SOC_AR8030_BRINGUP
	static const uint8_t hello[] = {0x00, 0x10, 0x01, 0x00, 'Z', 'E', 'P', 'H'};
#else
	static const uint8_t hello[] = {0x00, 0x10, 0x00, 0x00, 'Z', 'E', 'P', 'H'};
#endif
	static const uint8_t info[] = "ar8030-zephyr/bringup;rf=unsupported;protocol=1";
	struct ar_frame response = {.reqid = request->reqid, .msgid = request->msgid};
	if (request->status != 0) {
		response.status = AR_STATUS_INVALID;
	} else if (request->reqid == AR_REQ_HELLO) {
		response.payload = hello;
		response.length = sizeof hello;
	} else if (request->reqid == AR_REQ_HEARTBEAT) {
		response.payload = request->payload;
		response.length = request->length;
	} else if (request->reqid == AR_REQ_RESET) {
		/* Link synchronization only: never reboot or touch persistent storage. */
		response.status = 0;
	} else if (ar_diagnostic_request_id(request->reqid) == AR_REQ_DIAGNOSTICS) {
		response.payload = info;
		response.length = sizeof info - 1;
	} else {
		/* Do not fabricate RF status, pairing, video, init/start, or calibration. */
		response.status = AR_STATUS_UNSUPPORTED;
	}
	*length = ar_encode(reply, AR_MAX_FRAME, &response);
}
