/* SPDX-License-Identifier: MIT */
#ifndef AR_SCAN_H
#define AR_SCAN_H
#include <stddef.h>
#include <stdint.h>
#include "ar_command.h"
/* Native catalog words retain frequency[23:0], enable[24] and band[28:27].
 * They are not declarations of the original eight-byte catalog records. */
int ar_scan_advance(const uint32_t *rows, size_t count, uint16_t *channel,
	unsigned sub_enabled, unsigned sub_count);
/* Measurement user 11 RX: caller supplies the already calculated PLL word.
 * Writes five PLL bytes followed by receive band controls. Does not tune the
 * link's RX/TX users or implement scan scoring/automatic channel selection. */
int ar_scan_tune(uint32_t frequency, uint64_t pll, unsigned dual_band,
	ar_command_write write_field, ar_command_write write_byte, void *context);
#endif
