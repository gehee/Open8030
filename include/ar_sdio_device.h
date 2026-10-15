/* SPDX-License-Identifier: MIT */
#ifndef AR_SDIO_DEVICE_H
#define AR_SDIO_DEVICE_H
#include <stddef.h>
#include <stdint.h>

#define AR_SDIO_BLOCK 512u
#define AR_SDIO_BUFFER_SIZE 4608u
#define AR_SDIO_TX_DEPTH 4u
struct ar_sdio_stats {
	uint32_t polls, host_enabled, rx_packets, tx_packets, errors, invalid_rx;
	uint32_t tx_backpressure, tx_queue_high_water;
	/* Poll-observed descriptor lifetime, in 40 MHz machine-timer ticks.
	 * Includes host and local polling delay; aborts also finish a descriptor. */
	uint32_t tx_finished, tx_last_ticks, tx_max_ticks, tx_wire_bytes;
};
void ar_sdio_init(void);
size_t ar_sdio_receive(const uint8_t **data);
int ar_sdio_send(const uint8_t *data, size_t length);
unsigned ar_sdio_tx_room(void);
const struct ar_sdio_stats *ar_sdio_statistics(void);
uint32_t ar_sdio_active_ticks(void);
void ar_sdio_record_fault(uint32_t reason, uint32_t cause, uint32_t pc, uint32_t value);
void ar_sdio_record_progress(uint32_t stage, uint32_t a, uint32_t b, uint32_t c);
#endif
