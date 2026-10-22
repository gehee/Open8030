/* SPDX-License-Identifier: MIT */
#include "ar_transport_storage.h"

_Static_assert(AR_SDIO_BUFFER_SIZE % 512 == 0, "SDIO DMA slot alignment");
_Static_assert(AR_USB_BUFFER_SIZE % 512 == 0, "USB DMA slot alignment");
union ar_transport_storage ar_transport_dma __attribute__((aligned(512)));
