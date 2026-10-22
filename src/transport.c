/* SPDX-License-Identifier: MIT */
#include "ar_transport.h"
#include "ar_frame.h"
#include "ar_sdio_device.h"
#include "ar_usb_device.h"

static const struct ar_transport sdio_transport = {
	.init            = ar_sdio_init,
	.receive         = ar_sdio_receive,
	.send            = ar_sdio_send,
	.tx_room         = ar_sdio_tx_room,
	.record_fault    = ar_sdio_record_fault,
	.record_progress = ar_sdio_record_progress,
	.tx_depth        = AR_SDIO_TX_DEPTH,
	.block           = AR_SDIO_BLOCK,
	/* Four blocks amortize host descriptor overhead during video bursts.
	 * RF is polled between copying, framing and submitting each packet. */
	.chunk           = 4 * AR_SDIO_BLOCK - AR_OVERHEAD,
};

static const struct ar_transport usb_transport = {
	.init            = ar_usb_init,
	.receive         = ar_usb_receive,
	.send            = ar_usb_send,
	.tx_room         = ar_usb_tx_room,
	.record_fault    = ar_usb_record_fault,
	.record_progress = ar_usb_record_progress,
	.tx_depth        = AR_USB_TX_DEPTH,
	.block           = 512u,
	/* The host reads 8 KiB bulk URBs; a 1 KiB frame per loop pass drained
	 * only ~5 Mbit/s and the socket ring dropped the rest at 20 MHz. */
	.chunk           = AR_MAX_PAYLOAD,
	.init_after_boot = 1,
};

#define SDIO_BASE  UINT32_C(0x04200000)
#define USB_BASE   UINT32_C(0x04000000)

static uint32_t peek(uint32_t address)
{
	return *(volatile uint32_t *)(uintptr_t)address;
}

const struct ar_transport *ar_transport_detect(void)
{
#ifdef AR_FORCE_USB
	return &usb_transport;
#endif
	/* The SDIO device controller's CFG register at +0x004 is non-zero when
	 * the block has been clocked and a host is driving the bus (the reset
	 * value is 0, and the host's CMD5 sets its function-ready bits).
	 * On a USB board the SDIO block is either held in reset or
	 * reads back zero because no host is clocking it.
	 *
	 * Conversely, on an SDIO board the USB OTG block at 0x04000000 is
	 * either in reset or has no VBUS, so GOTGCTL bit 19 (B-session valid)
	 * stays low.
	 *
	 * Check SDIO first: if there is any sign of a host on that bus, use it.
	 * Otherwise fall back to USB. */
	/* A boot ROM that was enumerated over USB leaves the host-assigned
	 * address in DCFG.DevAddr; on an SDIO board the USB core is idle (0). */
	if ((peek(USB_BASE + 0x800u) >> 4) & 0x7fu)
		return &usb_transport;
	uint32_t sdio_cfg = peek(SDIO_BASE + 0x004u);
	if (sdio_cfg != 0)
		return &sdio_transport;
	return &usb_transport;
}
