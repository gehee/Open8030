/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include "ar_frame.h"
#ifdef CONFIG_SOC_AR8030_BRINGUP
#include "ar_sdio_device.h"
#include "ar_rf_device.h"
#include "ar_boot.h"
#include "ar_socket_rx.h"
#include "ar_event.h"
static struct ar_socket_rx sockets;
static struct ar_events events;
/* Bounded diagnostic record of real host commands during stack integration. */
static struct {
	uint32_t count, overflow, event_drops, reserved;
	struct { uint32_t reqid, count, length; uint8_t last_prefix[16]; } entries[64];
} host_audit;
static void rf_event(unsigned id, const uint8_t *data, size_t length, void *context)
{
	(void)context;
	(void)ar_event_publish(&events, id, data, length);
}
static size_t transmit_peek(uint8_t *scratch, size_t capacity, unsigned *port, void *context)
{
	(void)context;
	return ar_socket_tx_peek(&sockets, scratch, capacity, port);
}
static void transmit_commit(unsigned port, size_t length, void *context)
{
	(void)context;
	ar_socket_tx_commit(&sockets, port, length);
}
static void received_payload(unsigned slot, unsigned port, const uint8_t *data, size_t length, void *context)
{
	(void)context;
	(void)ar_socket_rx_feed(&sockets, slot, port, data, length);
}
_Static_assert(sizeof(struct ar_rf_rx_info) <= AR_MAX_PAYLOAD, "RF diagnostics must fit one response");
#include <zephyr/fatal.h>
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	(void)esf;
	uint32_t cause, pc, value;
	__asm__ volatile("csrr %0, mcause" : "=r"(cause));
	__asm__ volatile("csrr %0, mepc" : "=r"(pc));
	__asm__ volatile("csrr %0, mtval" : "=r"(value));
	ar_sdio_record_fault(reason, cause, pc, value);
	for (;;) { arch_nop(); }
}
static uint8_t transformed[AR_MAX_PAYLOAD];
static uint32_t observed_unary(uint32_t input)
{
	/* Observed register-only instruction in socket key construction. Its
	 * arithmetic is challenged on hardware before accepting an ISA model. */
	register uint32_t value __asm__("a5") = input;
	__asm__ volatile(".4byte 0xad8787f7" : "+r"(value));
	return value;
}
static uint32_t observed_unary_s8(uint32_t input)
{
	/* Exact word at stock socket dispatch 0x2407fc. Test this register form
	 * independently before extending the partial analysis model. */
	register uint32_t value __asm__("s8") = input;
	__asm__ volatile(".4byte 0xad8c0c77" : "+r"(value));
	return value;
}
static uint32_t observed_reverse_s0(uint32_t input)
{
	/* Exact stock measurement-helper word at 0x22d28a. */
	register uint32_t value __asm__("s0") = input;
	__asm__ volatile(".4byte 0x8204140b" : "+r"(value));
	return value;
}
#else

/* UART is the emulator/diagnostic transport, not the AR8030 SDIO peripheral. */
static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif
static struct ar_parser parser;
static uint8_t reply[AR_MAX_FRAME];

static void receive(const struct ar_frame *frame, void *context)
{
	(void)context;
#ifdef CONFIG_SOC_AR8030_BRINGUP
	struct ar_frame socket_response;
	uint32_t diagnostic_reqid = ar_diagnostic_request_id(frame->reqid);
	if (diagnostic_reqid == AR_REQ_RF_SCAN_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_scan_information(), .length = sizeof(struct ar_rf_scan_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_RECOVERY_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_recovery_information(), .length = sizeof(struct ar_rf_recovery_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_LIFECYCLE_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_lifecycle_information(), .length = sizeof(struct ar_rf_lifecycle_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_BOOT_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_boot_information(), .length = sizeof(struct ar_boot_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_HOST_AUDIT && !frame->status && !frame->length) {
		host_audit.event_drops = events.dropped;
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)&host_audit, .length = sizeof host_audit};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid >> 24 != 0x0c) {
		unsigned i;
		for (i = 0; i < host_audit.count; i++) { if (host_audit.entries[i].reqid == frame->reqid) { break; } }
		if (i == 64) { host_audit.overflow++; }
		else {
			if (i == host_audit.count) { host_audit.count++; host_audit.entries[i].reqid = frame->reqid; }
			host_audit.entries[i].count++; host_audit.entries[i].length = frame->length;
			memset(host_audit.entries[i].last_prefix, 0, 16);
			if (frame->length) { memcpy(host_audit.entries[i].last_prefix, frame->payload, frame->length < 16 ? frame->length : 16); }
		}
	}
	if ((frame->reqid >> 24) == 0x0b) {
		int status = frame->status ? AR_STATUS_INVALID : ar_rf_host_control(frame->reqid & 0xffffff, frame->length);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == UINT32_C(0x02000004)) {
		int status = frame->status ? AR_STATUS_INVALID : ar_rf_host_candidates(frame->payload, frame->length);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (ar_event_request(&events, frame, &socket_response)) {
		size_t size = ar_encode(reply, sizeof reply, &socket_response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == UINT32_C(0x01000064)) {
		unsigned malformed = frame->status || frame->length != 4;
		/* Stock allocates the declared 256-byte reply even on a range error.
		 * Initialize it so errors cannot return a previous response's bytes. */
		memset(transformed, 0, 256);
		int status = malformed ? AR_STATUS_INVALID :
			ar_rf_host_registers(frame->payload[0] | (unsigned)frame->payload[1] << 8,
				frame->payload[2] | (unsigned)frame->payload[3] << 8, transformed);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.status = status, .payload = transformed, .length = malformed ? 0 : 256};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == UINT32_C(0x0100000c)) {
		uint8_t output[4];
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid};
		if (frame->status || frame->length) { response.status = AR_STATUS_INVALID; }
		else {
			uint32_t value = ar_rf_host_ap_time();
			for (unsigned i = 0; i < 4; i++) { output[i] = value >> (8 * i); }
			response.payload = output; response.length = sizeof output;
		}
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == UINT32_C(0x01000000)) {
		int status = frame->status || frame->length != 2 ? AR_STATUS_INVALID :
			ar_rf_host_status(frame->payload[0] | (uint16_t)frame->payload[1] << 8, transformed);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.status = status, .payload = transformed, .length = status ? 0 : 300};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_TX_PAYLOAD_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_tx_payload_information(), .length = sizeof(struct ar_rf_tx_payload_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_SOCKET_TX_INFO && !frame->status && !frame->length) {
		uint32_t words[AR_SOCKET_RX_PORTS * 12];
		for (unsigned port = 0; port < AR_SOCKET_RX_PORTS; port++) {
			const struct ar_socket_rx_port *s = &sockets.ports[port];
			uint32_t values[] = {s->flags, s->tx_capacity, s->tx_used, s->pending_count,
				(uint32_t)s->position, (uint32_t)(s->position >> 32), s->enqueued, s->completed,
				s->submitted, s->rejected, s->tx_head, s->msgid};
			for (unsigned i = 0; i < 12; i++) { words[port * 12 + i] = values[i]; }
		}
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)words, .length = sizeof words};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_MCS_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_mcs_information(), .length = sizeof(struct ar_rf_mcs_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_SYNC_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_sync_information(), .length = sizeof(struct ar_rf_sync_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_TRAFFIC_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_traffic_information(), .length = sizeof(struct ar_rf_traffic_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_TX_MCS_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_tx_mcs_information(), .length = sizeof(struct ar_rf_tx_mcs_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_TX_HISTORY_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_tx_history_information(), .length = sizeof(struct ar_rf_tx_history_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_PHY_TRACE && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_phy_trace_information(), .length = sizeof(struct ar_rf_phy_trace)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_PHY_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)ar_rf_phy_information(), .length = sizeof(struct ar_rf_phy_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	int socket_handled = ar_socket_rx_request(&sockets, frame, &socket_response);
	if (socket_handled) {
		if (socket_handled == 1) {
			size_t size = ar_encode(reply, sizeof reply, &socket_response);
			(void)ar_sdio_send(reply, size);
		}
		return;
	}
	if (diagnostic_reqid == AR_REQ_SOCKET_CLOSE_INFO && !frame->status && !frame->length) {
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)&sockets.close_info, .length = sizeof sockets.close_info};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_SOCKET_RX_INFO && !frame->status && !frame->length) {
		uint32_t words[AR_SOCKET_RX_PORTS * 8];
		for (unsigned port = 0; port < AR_SOCKET_RX_PORTS; port++) {
			const struct ar_socket_rx_port *s = &sockets.ports[port];
			uint32_t values[] = {s->capacity, s->used, s->msgid, s->offered,
				s->accepted, s->delivered, s->dropped, s->head};
			for (unsigned i = 0; i < 8; i++) { words[port * 8 + i] = values[i]; }
		}
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid,
			.payload = (const uint8_t *)words, .length = sizeof words};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == AR_REQ_RF_REGISTERS && frame->status == 0 && frame->length == 4) {
		unsigned offset = frame->payload[0] | (unsigned)frame->payload[1] << 8;
		unsigned length = frame->payload[2] | (unsigned)frame->payload[3] << 8;
		int status = ar_rf_read_registers(offset, length, transformed);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status,
			.payload = transformed, .length = status ? 0 : length};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == AR_REQ_RF_PLL && frame->status == 0 && frame->length == 4) {
		uint32_t frequency = 0;
		for (unsigned byte = 0; byte < 4; byte++) frequency |= (uint32_t)frame->payload[byte] << (8 * byte);
		uint64_t pll = 0;
		int status = ar_rf_pll_word(frequency, &pll);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status,
			.payload = (const uint8_t *)&pll, .length = sizeof pll};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (diagnostic_reqid == AR_REQ_RF_RX_INFO) {
		struct ar_frame response;
		ar_rx_diagnostic_read(frame, (const uint8_t *)ar_rf_receive_information(),
				      sizeof(struct ar_rf_rx_info), &response);
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid >= AR_REQ_RF_PROBE && frame->reqid <= AR_REQ_RF_MONITOR) {
		ar_sdio_record_progress(0xb000 | (frame->reqid & 255), frame->length, frame->msgid, 0);
	}
	if (frame->reqid == AR_REQ_RF_MONITOR && frame->status == 0 && frame->length >= 16) {
		uint32_t frequency = 0, milliseconds = 0;
		for (unsigned byte = 0; byte < 4; byte++) {
			frequency |= (uint32_t)frame->payload[byte] << (8 * byte);
			milliseconds |= (uint32_t)frame->payload[byte + 4] << (8 * byte);
		}
		int status = frame->payload[12] == 76 ?
			ar_rf_prepare_host(frequency, milliseconds, frame->payload[8], frame->payload + 16, frame->length - 16) :
			ar_rf_monitor_plan(frequency, milliseconds, frame->payload[8], frame->payload[12],
				frame->payload + 16, frame->length - 16);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status,
			.payload = (const uint8_t *)ar_rf_receive_information(), .length = sizeof(struct ar_rf_rx_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid == AR_REQ_RF_PINS && frame->status == 0) {
		int status = ar_rf_setup_pins(frame->payload, frame->length);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status,
			.payload = (const uint8_t *)ar_rf_information(), .length = sizeof(struct ar_rf_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size); return;
	}
	if (frame->reqid >= AR_REQ_RF_PROBE && frame->reqid <= AR_REQ_RF_PSRAM && frame->status == 0) {
		int status;
		if (frame->reqid == AR_REQ_RF_PROBE && frame->length == 0) { status = ar_rf_probe(); }
		else if (frame->reqid == AR_REQ_RF_DEFAULTS) { status = ar_rf_load_defaults(frame->payload, frame->length); }
		else if (frame->reqid == AR_REQ_RF_CALIBRATE && frame->length == 0) { status = ar_rf_calibrate(); }
		else if (frame->reqid == AR_REQ_RF_PSRAM && frame->length == 0) { status = ar_rf_setup_psram(); }
		else { status = AR_STATUS_INVALID; }
		ar_sdio_record_progress(0xb100 | (frame->reqid & 255), status, ar_rf_information()->phase, 0);
		struct ar_frame response = {.reqid = frame->reqid, .msgid = frame->msgid, .status = status,
			.payload = (const uint8_t *)ar_rf_information(), .length = sizeof(struct ar_rf_info)};
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size);
		return;
	}
	if ((frame->reqid == AR_REQ_OP_AD8 || frame->reqid == AR_REQ_OP_AD8_S8 || frame->reqid == AR_REQ_OP_REV_S0) &&
	    frame->status == 0 && frame->length % 4 == 0) {
		for (uint32_t i = 0; i < frame->length; i += 4) {
			uint32_t value = frame->payload[i] | (uint32_t)frame->payload[i + 1] << 8 |
				(uint32_t)frame->payload[i + 2] << 16 | (uint32_t)frame->payload[i + 3] << 24;
			value = frame->reqid == AR_REQ_OP_AD8 ? observed_unary(value) :
				frame->reqid == AR_REQ_OP_AD8_S8 ? observed_unary_s8(value) : observed_reverse_s0(value);
			for (unsigned byte = 0; byte < 4; byte++) { transformed[i + byte] = value >> (8 * byte); }
		}
		struct ar_frame response = *frame;
		response.payload = transformed;
		size_t size = ar_encode(reply, sizeof reply, &response);
		(void)ar_sdio_send(reply, size);
		return;
	}
	if (frame->reqid == AR_REQ_TX_BURST && frame->status == 0) {
		/* Project-only transport test: four independently queued nonce echoes.
		 * It injects no RF/video data and does not implement a vendor socket. */
		struct ar_frame response = *frame;
		for (unsigned i = 0; i < AR_SDIO_TX_DEPTH; i++) {
			response.msgid = frame->msgid + i;
			size_t size = ar_encode(reply, sizeof reply, &response);
			if (ar_sdio_send(reply, size)) { break; }
		}
		return;
	}
#endif
	size_t length;
	ar_dispatch(frame, reply, &length);
#ifdef CONFIG_SOC_AR8030_BRINGUP
	(void)ar_sdio_send(reply, length);
#else
	for (size_t i = 0; i < length; i++) { uart_poll_out(uart, reply[i]); }
#endif
}

int main(void)
{
#ifdef CONFIG_SOC_AR8030_BRINGUP
	/* Diagnostic polling must not enter an unported CLIC ISR when host-side
	 * function enable or DMA completion asserts the controller's CPU IRQ. */
	(void)arch_irq_lock();
	ar_sdio_init();
	ar_socket_rx_init(&sockets);
	ar_events_init(&events);
	ar_rf_set_event_callback(rf_event, NULL);
	ar_rf_set_payload_callback(received_payload, NULL);
	ar_rf_set_tx_callbacks(transmit_peek, transmit_commit, NULL);
#ifdef AR_BOOT_EMBEDDED
	int boot_status = ar_boot_embedded_profile.plan ? ar_boot_prepare(&ar_boot_embedded_profile) :
		ar_boot_prepare_loader(&ar_boot_embedded_profile, (const char *)(uintptr_t)0x200000, 16384);
	ar_sdio_record_progress(0xc000, ar_boot_information()->stage, boot_status,
		ar_boot_information()->default_bytes);
#endif
	unsigned completion_first = 1;
	for (;;) {
		const uint8_t *data;
		size_t length = ar_sdio_receive(&data);
		if (length) { ar_feed(&parser, data, length, receive, NULL); }
		else { arch_nop(); }
		ar_socket_tx_service(&sockets);
		ar_socket_close_service(&sockets);
		ar_rf_poll();
		struct ar_frame frame;
		/* Leave one entry for a reply to an already granted host request. */
		if (ar_sdio_tx_room() > 1) {
			int ready;
			/* Bound copy/framing work between RF polls. Socket data is a
			 * stream; two SDIO blocks balance draining against RF latency.
			 * A one-block trial overflowed the host queue under video load. */
			size_t chunk = 2 * AR_SDIO_BLOCK - AR_OVERHEAD;
			/* Copy directly to the final frame payload. Its contents remain
			 * owned here while RF appends to the independent endpoint queue. */
			uint8_t *payload = reply + AR_HEADER_SIZE;
			unsigned event = ar_event_peek(&events, payload, &frame);
			unsigned close = !event && ar_socket_close_peek(&sockets, &frame);
			if (event || close) { ready = 1; }
			else if (completion_first) {
				ready = ar_socket_tx_completion(&sockets, payload, &frame) ||
					ar_socket_rx_peek(&sockets, payload, chunk, &frame);
			} else {
				ready = ar_socket_rx_peek(&sockets, payload, chunk, &frame) ||
					ar_socket_tx_completion(&sockets, payload, &frame);
			}
			if (ready) {
				/* The peek owns an immutable scratch copy. RF may append to
				 * the endpoint while we prepare SDIO's copy, but cannot move
				 * its head; only the commit below consumes these bytes. */
				ar_rf_poll();
				size_t size = ar_encode(reply, sizeof reply, &frame);
				ar_rf_poll();
				if (!ar_sdio_send(reply, size)) {
					unsigned completion = (frame.reqid >> 16 & 255) == 1;
					if (event) { ar_event_commit(&events); }
					else if (close) { ar_socket_close_commit(&sockets, &frame); }
					else if (completion) { ar_socket_tx_complete(&sockets, &frame); }
					else { ar_socket_rx_commit(&sockets, &frame); }
					if (!event && !close) { completion_first = !completion; }
				}
			}
		}
	}
#else
	if (!device_is_ready(uart)) { return -ENODEV; }
	for (;;) {
		unsigned char byte;
		if (uart_poll_in(uart, &byte) == 0) {
			ar_feed(&parser, &byte, 1, receive, NULL);
		} else {
			/* Polling bring-up also works before a hardware timer is ported. */
			arch_nop();
		}
	}
#endif
	return 0;
}
