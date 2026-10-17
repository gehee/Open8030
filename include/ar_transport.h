/* SPDX-License-Identifier: MIT */
#ifndef AR_TRANSPORT_H
#define AR_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>

struct ar_transport {
	void     (*init)(void);
	size_t   (*receive)(const uint8_t **data);
	int      (*send)(const uint8_t *data, size_t length);
	unsigned (*tx_room)(void);
	void     (*record_fault)(uint32_t reason, uint32_t cause, uint32_t pc, uint32_t value);
	void     (*record_progress)(uint32_t stage, uint32_t a, uint32_t b, uint32_t c);
	unsigned  tx_depth;
	unsigned  block;
	unsigned  chunk;           /* socket bytes per pushed frame */
	unsigned  init_after_boot; /* host gives up on a silent device (USB) */
};

/* Probe the hardware and return the right transport.
 * SDIO: the SDIO device controller at 0x04200000 has a host attached.
 * USB:  the DWC2 OTG at 0x04000000 is wired to a USB host.
 * The check is fast (register reads) and runs before either side is init'd. */
const struct ar_transport *ar_transport_detect(void);
#endif
