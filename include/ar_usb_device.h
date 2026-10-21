/* SPDX-License-Identifier: MIT */
#ifndef AR_USB_DEVICE_H
#define AR_USB_DEVICE_H
#include <stddef.h>
#include <stdint.h>

#define AR_USB_BUFFER_SIZE 8192u
#define AR_USB_TX_DEPTH 4u
struct ar_usb_stats {
	uint32_t polls, enumerated, rx_packets, tx_packets, errors, invalid_rx;
	uint32_t tx_backpressure, tx_queue_high_water;
	uint32_t setup_packets, resets;
};
void ar_usb_init(void);
/* Returned bytes remain valid until the next receive call or reinitialization.
 * The consumer may send replies before it finishes reading those bytes. */
size_t ar_usb_receive(const uint8_t **data);
int ar_usb_send(const uint8_t *data, size_t length);
unsigned ar_usb_tx_room(void);
const struct ar_usb_stats *ar_usb_statistics(void);
void ar_usb_record_fault(uint32_t reason, uint32_t cause, uint32_t pc, uint32_t value);
void ar_usb_record_progress(uint32_t stage, uint32_t a, uint32_t b, uint32_t c);
#ifdef AR_USB_PULSE_DIAG
void ar_usb_pulse_diag(void);
#endif
#endif
