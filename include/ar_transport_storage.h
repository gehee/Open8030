/* SPDX-License-Identifier: MIT */
#ifndef AR_TRANSPORT_STORAGE_H
#define AR_TRANSPORT_STORAGE_H
#include "ar_sdio_device.h"
#include "ar_usb_device.h"

/* Transport detection selects one controller for the lifetime of the image.
 * Its DMA payload storage can therefore overlap the inactive controller's.
 * Controller metadata and USB endpoint-zero buffers remain independent. */
union ar_transport_storage {
	struct {
		uint8_t receive[AR_SDIO_BUFFER_SIZE];
		uint8_t transmit[AR_SDIO_TX_DEPTH][AR_SDIO_BUFFER_SIZE];
	} sdio;
	struct {
		uint8_t receive[AR_USB_BUFFER_SIZE];
		uint8_t transmit[AR_USB_TX_DEPTH][AR_USB_BUFFER_SIZE];
	} usb;
};
extern union ar_transport_storage ar_transport_dma;
#endif
