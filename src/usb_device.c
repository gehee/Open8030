/* SPDX-License-Identifier: MIT */
/* DWC2 USB device transport for a USB-attached AR8030 (ground unit).
 *
 * Provides the same polling API as ar_sdio_device so main.c can switch
 * transports. The DWC2 OTG controller sits at 0x04000000; we enumerate as
 * VID 1d6b PID 8030 (one interface, one bulk IN + one bulk OUT endpoint) and
 * exchange host-protocol frames on the bulk pipe.
 *
 * Bring-up follows the stock firmware's own device init (its HAL_PCD_Init /
 * USB_CoreInit / USB_DevInit / HAL_PCD_Start equivalents), which is exactly
 * the sequence for taking the core over from the boot ROM's upload gadget:
 * UTMI+ HS PHY, core soft reset, buffer DMA with INCR16 bursts, VBUS sensing,
 * the stock FIFO layout with GDFIFOCFG set, PHY power on, then connect.
 * Polled, no interrupts - matches the single cooperative loop in main(). */
#include "ar_usb_device.h"
#include "ar_cache.h"
#include <string.h>

/* ---- DWC2 register map (subset) ---- */
#define USB_BASE      UINT32_C(0x04000000)

#define GOTGCTL       0x000u
#define GAHBCFG       0x008u
#define GUSBCFG       0x00Cu
#define GRSTCTL       0x010u
#define GINTSTS       0x014u
#define GINTMSK       0x018u
#define GRXFSIZ       0x024u
#define GNPTXFSIZ     0x028u  /* device mode: DIEPTXF0 */
#define GCCFG         0x038u  /* vendor core config: 16 = PHY power, 21 = VBUS B-sense */
#define GHWCFG3       0x04Cu
#define GDFIFOCFG     0x05Cu  /* [31:16] EP-info base in FIFO RAM, [15:0] FIFO depth */
#define DIEPTXF(n)    (0x104u + ((n) - 1) * 4u)

#define DCFG          0x800u
#define DCTL          0x804u
#define DSTS          0x808u
#define DIEPMSK       0x810u
#define DOEPMSK       0x814u
#define DAINT         0x818u
#define DAINTMSK      0x81Cu

#define DIEPCTL(n)    (0x900u + (n) * 0x20u)
#define DIEPINT(n)    (0x908u + (n) * 0x20u)
#define DIEPTSIZ(n)   (0x910u + (n) * 0x20u)
#define DIEPDMA(n)    (0x914u + (n) * 0x20u)
#define DOEPCTL(n)    (0xB00u + (n) * 0x20u)
#define DOEPINT(n)    (0xB08u + (n) * 0x20u)
#define DOEPTSIZ(n)   (0xB10u + (n) * 0x20u)
#define DOEPDMA(n)    (0xB14u + (n) * 0x20u)

#define PCGCCTL       0xE00u

/* ---- bits ---- */
#define GAHBCFG_GLBL_INTR_EN    (1u << 0)
#define GAHBCFG_HBSTLEN_INCR16  (7u << 1)
#define GAHBCFG_DMA_EN          (1u << 5)

#define GUSBCFG_PHYIF16         (1u << 3)
#define GUSBCFG_ULPI_UTMI_SEL   (1u << 4)
#define GUSBCFG_PHYSEL          (1u << 6)
#define GUSBCFG_SRPCAP          (1u << 8)
#define GUSBCFG_HNPCAP          (1u << 9)
#define GUSBCFG_TRDT_MASK       (0xFu << 10)
#define GUSBCFG_ULPI_CLK_SUSM   (1u << 20)
#define GUSBCFG_ULPI_AUTORES    (1u << 21)
#define GUSBCFG_ULPI_FSLS       (1u << 22)
#define GUSBCFG_FORCEHSTMODE    (1u << 29)
#define GUSBCFG_FORCEDEVMODE    (1u << 30)

#define GRSTCTL_CSFTRST         (1u << 0)
#define GRSTCTL_RXFFLSH         (1u << 4)
#define GRSTCTL_TXFFLSH         (1u << 5)
#define GRSTCTL_TXFNUM_SHIFT    6
#define GRSTCTL_AHBIDLE         (1u << 31)

#define GINTSTS_OTGINT          (1u << 2)
#define GINTSTS_USBSUSP         (1u << 11)
#define GINTSTS_USBRST          (1u << 12)
#define GINTSTS_ENUMDNE         (1u << 13)
#define GINTSTS_IEPINT          (1u << 18)
#define GINTSTS_OEPINT          (1u << 19)
#define GINTSTS_IISOIXFR        (1u << 20)
#define GINTSTS_IPXFR           (1u << 21)
#define GINTSTS_SRQINT          (1u << 30)
#define GINTSTS_WKUPINT         (1u << 31)

#define GCCFG_PWRDWN            (1u << 16)
#define GCCFG_VBUSBSEN          (1u << 21)

#define DCFG_DEVSPD_MASK        3u
#define DCFG_DEVADDR_SHIFT      4
#define DCFG_DEVADDR_MASK       (0x7Fu << 4)

#define DCTL_RWUSIG             (1u << 0)
#define DCTL_SFTDISCON          (1u << 1)
#define DCTL_CGNPINNAK          (1u << 8)
#define DCTL_CGOUTNAK           (1u << 10)

#define DEPCTL_MPS_MASK         0x7FFu
#define DEPCTL_USBACTEP         (1u << 15)
#define DEPCTL_EPTYPE_SHIFT     18
#define DEPCTL_STALL            (1u << 21)
#define DEPCTL_TXFNUM_SHIFT     22
#define DEPCTL_CNAK             (1u << 26)
#define DEPCTL_SNAK             (1u << 27)
#define DEPCTL_SETD0PID         (1u << 28)
#define DEPCTL_EPDIS            (1u << 30)
#define DEPCTL_EPENA            (1u << 31)
#define EP_TYPE_CONTROL         0u
#define EP_TYPE_BULK            2u

#define DEPINT_XFERCOMPL        (1u << 0)
#define DEPINT_EPDISD           (1u << 1)
#define DEPINT_TIMEOUT          (1u << 3)
#define DEPINT_SETUP            (1u << 3)
#define DEPINT_ALL              0xFB7Fu

#define DEPTSIZ_XFERSIZE_MASK   0x7FFFFu
#define DEPTSIZ_PKTCNT_SHIFT    19
#define DEPTSIZ_SUPCNT_SHIFT    29

/* ---- descriptors ---- */
#define USB_VID       0x1D6Bu
#define USB_PID       0x8030u
#define EP0_MPS       64u
#define BULK_MPS      512u
#define BULK_IN_EP    1u
#define BULK_OUT_EP   1u

#define USB_REQ_GET_STATUS        0
#define USB_REQ_SET_ADDRESS       5
#define USB_REQ_GET_DESCRIPTOR    6
#define USB_REQ_GET_CONFIGURATION 8
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE    10
#define USB_REQ_SET_INTERFACE    11
#define USB_DESC_DEVICE           1
#define USB_DESC_CONFIGURATION    2
#define USB_DESC_INTERFACE        4
#define USB_DESC_ENDPOINT         5
#define USB_DESC_DEVICE_QUALIFIER 6

/* ---- hardware access ---- */
#ifndef AR_USB_SIMULATED
static uint32_t read32(uint32_t address) { return *(volatile uint32_t *)(uintptr_t)address; }
static void write32(uint32_t address, uint32_t value) { *(volatile uint32_t *)(uintptr_t)address = value; }
static void barrier(void) { __asm__ volatile("fence iorw,iorw" ::: "memory"); }
static void short_delay(void)
{
	uint32_t start = read32(UINT32_C(0xe000bff8));
	for (unsigned i = 0; i < 100000; i++) {
		if ((uint32_t)(read32(UINT32_C(0xe000bff8)) - start) >= 40) { break; }
	}
}
/* Runs about 2x slow (loop overhead on top of the 40-tick wait); fine for
 * bring-up delays. */
static void delay_ms(unsigned ms)
{
	for (unsigned i = 0; i < ms * 1000; i++) { short_delay(); }
}
#else
extern uint32_t ar_test_read32(uint32_t address);
extern void ar_test_write32(uint32_t address, uint32_t value);
#define read32 ar_test_read32
#define write32 ar_test_write32
static void barrier(void) { }
static void delay_ms(unsigned ms) { (void)ms; }
#endif

static uint32_t usbreg(uint32_t offset) { return read32(USB_BASE + offset); }
static void usbwr(uint32_t offset, uint32_t value) { write32(USB_BASE + offset, value); }
static void usbset(uint32_t offset, uint32_t bits) { usbwr(offset, usbreg(offset) | bits); }
static void usbclr(uint32_t offset, uint32_t bits) { usbwr(offset, usbreg(offset) & ~bits); }

/* ---- buffers (DMA targets: word aligned, in RAM) ---- */
static uint8_t ep0_buf[EP0_MPS] __attribute__((aligned(32)));
static uint8_t ep0_tx_buf[EP0_MPS] __attribute__((aligned(32)));
static uint8_t setup_buf[64] __attribute__((aligned(32)));   /* up to 3 back-to-back SETUPs */
#include "ar_transport_storage.h"
#define receive_buffer ar_transport_dma.usb.receive
#define transmit_buffers ar_transport_dma.usb.transmit
static size_t transmit_lengths[AR_USB_TX_DEPTH];
static unsigned tx_head, tx_count;

static struct ar_usb_stats stats;
static unsigned configured;       /* SET_CONFIGURATION received, bulk endpoints active */
static unsigned rx_held;          /* caller owns OUT bytes until next receive */
static unsigned rx_armed;         /* bulk OUT DMA transfer in flight */
static unsigned tx_pending;       /* bulk IN DMA transfer in flight */
static unsigned pending_address;  /* deferred SET_ADDRESS */
static size_t last_rx_length;

static const uint8_t device_descriptor[] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0xFF, 0x00, 0x00, EP0_MPS,
	(USB_VID & 0xFF), (USB_VID >> 8), (USB_PID & 0xFF), (USB_PID >> 8),
	0x00, 0x01, 0, 0, 0, 1,
};
static const uint8_t config_descriptor[] = {
	9, USB_DESC_CONFIGURATION, 32, 0, 1, 1, 0, 0x80, 250,
	9, USB_DESC_INTERFACE, 0, 0, 2, 0xFF, 0x00, 0x00, 0,
	7, USB_DESC_ENDPOINT, 0x80 | BULK_IN_EP, 0x02, (BULK_MPS & 0xFF), (BULK_MPS >> 8), 0,
	7, USB_DESC_ENDPOINT, BULK_OUT_EP, 0x02, (BULK_MPS & 0xFF), (BULK_MPS >> 8), 0,
};
static const uint8_t qualifier_descriptor[] = {
	10, USB_DESC_DEVICE_QUALIFIER, 0x00, 0x02, 0xFF, 0x00, 0x00, EP0_MPS, 1, 0,
};

/* ---- core helpers ---- */

static void core_reset(void)
{
	for (unsigned i = 0; i < 200000 && !(usbreg(GRSTCTL) & GRSTCTL_AHBIDLE); i++) { }
	usbset(GRSTCTL, GRSTCTL_CSFTRST);
	for (unsigned i = 0; i < 200001 && (usbreg(GRSTCTL) & GRSTCTL_CSFTRST); i++) { }
	delay_ms(3);
}

static void flush_tx_fifo(unsigned n)
{
	for (unsigned i = 0; i < 200000 && !(usbreg(GRSTCTL) & GRSTCTL_AHBIDLE); i++) { }
	usbwr(GRSTCTL, (n << GRSTCTL_TXFNUM_SHIFT) | GRSTCTL_TXFFLSH);
	for (unsigned i = 0; i < 200000 && (usbreg(GRSTCTL) & GRSTCTL_TXFFLSH); i++) { }
}

static void flush_rx_fifo(void)
{
	for (unsigned i = 0; i < 200000 && !(usbreg(GRSTCTL) & GRSTCTL_AHBIDLE); i++) { }
	usbwr(GRSTCTL, GRSTCTL_RXFFLSH);
	for (unsigned i = 0; i < 200000 && (usbreg(GRSTCTL) & GRSTCTL_RXFFLSH); i++) { }
}

/* Dedicated TX FIFO n placed after the RX FIFO, FIFO 0 and FIFOs 1..n-1. */
static void set_tx_fifo(unsigned n, uint32_t words)
{
	uint32_t start = usbreg(GRXFSIZ) + (usbreg(GNPTXFSIZ) >> 16);
	for (unsigned i = 1; i < n; i++) { start += usbreg(DIEPTXF(i)) >> 16; }
	usbwr(DIEPTXF(n), (words << 16) | start);
}

/* ---- EP0 (buffer DMA) ---- */

/* EP0 control registers are written whole: the MPS field is the 2-bit code
 * (0 = 64), control type, TX FIFO 0, active. */
#define EP0_CTL_BASE (DEPCTL_USBACTEP | (EP_TYPE_CONTROL << DEPCTL_EPTYPE_SHIFT) | (0u << DEPCTL_TXFNUM_SHIFT))

/* Arm EP0 OUT for the next SETUP (or a status-stage ZLP): three back-to-back
 * setups of 8 bytes land in setup_buf by DMA. */
static void ep0_start_rx(void)
{
	usbwr(DOEPTSIZ(0), (3u << DEPTSIZ_SUPCNT_SHIFT) | (1u << DEPTSIZ_PKTCNT_SHIFT) | 24u);
	ar_cache_flush(setup_buf, sizeof setup_buf);
	usbwr(DOEPDMA(0), (uint32_t)(uintptr_t)setup_buf);
	barrier();
	usbwr(DOEPCTL(0), EP0_CTL_BASE | DEPCTL_EPENA | DEPCTL_CNAK);
}

static void ep0_start_tx(const uint8_t *data, size_t len)
{
	if (len > EP0_MPS) len = EP0_MPS;
	memcpy(ep0_tx_buf, data, len);
	ar_cache_clean(ep0_tx_buf, sizeof ep0_tx_buf);
	usbwr(DIEPTSIZ(0), (len & 0x7Fu) | (1u << DEPTSIZ_PKTCNT_SHIFT));
	usbwr(DIEPDMA(0), (uint32_t)(uintptr_t)ep0_tx_buf);
	barrier();
	usbwr(DIEPCTL(0), EP0_CTL_BASE | DEPCTL_EPENA | DEPCTL_CNAK);
}

static void ep0_zlp_in(void)
{
	usbwr(DIEPTSIZ(0), 1u << DEPTSIZ_PKTCNT_SHIFT);
	usbwr(DIEPDMA(0), (uint32_t)(uintptr_t)ep0_tx_buf);
	barrier();
	usbwr(DIEPCTL(0), EP0_CTL_BASE | DEPCTL_EPENA | DEPCTL_CNAK);
}

static void ep0_stall(void)
{
	usbset(DIEPCTL(0), DEPCTL_STALL);
	usbset(DOEPCTL(0), DEPCTL_STALL);
	ep0_start_rx();
}

/* ---- standard requests ---- */

static void handle_setup(const uint8_t *setup)
{
	uint8_t bmRequestType = setup[0];
	uint8_t bRequest = setup[1];
	uint16_t wValue = setup[2] | ((uint16_t)setup[3] << 8);
	uint16_t wLength = setup[6] | ((uint16_t)setup[7] << 8);

	stats.setup_packets++;
	if ((bmRequestType & 0x60) != 0) { ep0_stall(); return; }

	switch (bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		const uint8_t *desc; size_t desc_len;
		switch (wValue >> 8) {
		case USB_DESC_DEVICE:           desc = device_descriptor;    desc_len = sizeof device_descriptor;    break;
		case USB_DESC_CONFIGURATION:    desc = config_descriptor;    desc_len = sizeof config_descriptor;    break;
		case USB_DESC_DEVICE_QUALIFIER: desc = qualifier_descriptor; desc_len = sizeof qualifier_descriptor; break;
		default: ep0_stall(); return;
		}
		if (desc_len > wLength) desc_len = wLength;
		ep0_start_tx(desc, desc_len);
		ep0_start_rx();
		break;
	}
	case USB_REQ_SET_ADDRESS:
		/* Program the address now, before the status stage, as every stock
		 * DWC2 driver does; the core only answers at it once the status ZLP
		 * has gone out. Deferring it to that ZLP's XFERCOMPL left the device
		 * answering at address 0 only (usbmon, 2026-10-15). */
		usbwr(DCFG, (usbreg(DCFG) & ~DCFG_DEVADDR_MASK) | ((wValue & 0x7Fu) << DCFG_DEVADDR_SHIFT));
		ep0_zlp_in();
		ep0_start_rx();
		break;
	case USB_REQ_SET_CONFIGURATION: {
		unsigned config_val = wValue & 0xFF;
		if (config_val == 1 && !configured) {
			usbwr(DIEPCTL(BULK_IN_EP), (BULK_MPS & DEPCTL_MPS_MASK) | DEPCTL_USBACTEP | DEPCTL_SETD0PID |
				(EP_TYPE_BULK << DEPCTL_EPTYPE_SHIFT) | (BULK_IN_EP << DEPCTL_TXFNUM_SHIFT));
			usbwr(DOEPCTL(BULK_OUT_EP), (BULK_MPS & DEPCTL_MPS_MASK) | DEPCTL_USBACTEP | DEPCTL_SETD0PID |
				(EP_TYPE_BULK << DEPCTL_EPTYPE_SHIFT));
			usbset(DAINTMSK, (1u << BULK_IN_EP) | (1u << (BULK_OUT_EP + 16)));
			configured = 1; rx_armed = 0; tx_pending = 0;
			stats.enumerated++;
		} else if (config_val == 0 && configured) {
			usbclr(DIEPCTL(BULK_IN_EP), DEPCTL_USBACTEP);
			usbclr(DOEPCTL(BULK_OUT_EP), DEPCTL_USBACTEP);
			configured = 0;
		}
		ep0_zlp_in();
		ep0_start_rx();
		break;
	}
	case USB_REQ_GET_CONFIGURATION:
		ep0_buf[0] = configured ? 1 : 0; ep0_start_tx(ep0_buf, 1); ep0_start_rx(); break;
	case USB_REQ_GET_STATUS:
		ep0_buf[0] = 0; ep0_buf[1] = 0; ep0_start_tx(ep0_buf, 2); ep0_start_rx(); break;
	case USB_REQ_SET_INTERFACE:
		ep0_zlp_in(); ep0_start_rx(); break;
	case USB_REQ_GET_INTERFACE:
		ep0_buf[0] = 0; ep0_start_tx(ep0_buf, 1); ep0_start_rx(); break;
	default:
		ep0_stall(); break;
	}
}

/* ---- bulk endpoints (buffer DMA) ---- */

static void arm_bulk_rx(void)
{
	if (!configured || rx_armed || rx_held) return;
	uint32_t pktcnt = AR_USB_BUFFER_SIZE / BULK_MPS;
	usbwr(DOEPTSIZ(BULK_OUT_EP), (AR_USB_BUFFER_SIZE & DEPTSIZ_XFERSIZE_MASK) | (pktcnt << DEPTSIZ_PKTCNT_SHIFT));
	ar_cache_flush(receive_buffer, AR_USB_BUFFER_SIZE);
	usbwr(DOEPDMA(BULK_OUT_EP), (uint32_t)(uintptr_t)receive_buffer);
	barrier();
	usbwr(DOEPCTL(BULK_OUT_EP), usbreg(DOEPCTL(BULK_OUT_EP)) | DEPCTL_EPENA | DEPCTL_CNAK);
	rx_armed = 1;
}

static void start_bulk_tx(void)
{
	if (!configured || tx_pending || !tx_count) return;
	size_t len = transmit_lengths[tx_head];
	uint32_t pktcnt = (len + BULK_MPS - 1) / BULK_MPS;
	if (!pktcnt) pktcnt = 1;
	usbwr(DIEPTSIZ(BULK_IN_EP), (len & DEPTSIZ_XFERSIZE_MASK) | (pktcnt << DEPTSIZ_PKTCNT_SHIFT));
	ar_cache_clean(transmit_buffers[tx_head], len);
	usbwr(DIEPDMA(BULK_IN_EP), (uint32_t)(uintptr_t)transmit_buffers[tx_head]);
	barrier();
	usbwr(DIEPCTL(BULK_IN_EP), usbreg(DIEPCTL(BULK_IN_EP)) | DEPCTL_EPENA | DEPCTL_CNAK);
	tx_pending = 1;
	stats.tx_packets++;
}

static void finish_bulk_tx(void)
{
	if (!tx_pending) return;
	tx_pending = 0;
	tx_head = (tx_head + 1) % AR_USB_TX_DEPTH;
	tx_count--;
}

/* EP0 interrupt masks, as the stock reset handler sets them. */
static void ep0_masks(void)
{
	usbwr(DAINTMSK, (1u << 0) | (1u << 16));
	usbwr(DOEPMSK, DEPINT_SETUP | DEPINT_XFERCOMPL | DEPINT_EPDISD);
	usbwr(DIEPMSK, DEPINT_XFERCOMPL | DEPINT_EPDISD | DEPINT_TIMEOUT);
}

/* ---- public API ---- */

void ar_usb_init(void)
{
	memset(&stats, 0, sizeof stats);
	configured = rx_armed = rx_held = tx_pending = 0;
	tx_head = tx_count = 0;
	pending_address = 0;

	/* The CPU memory-attribute table the stock firmware programs at boot
	 * (observed 18.21.10). Under the reset-default policy sub-word stores
	 * reach RAM with different strobes, and a byte-wise memcpy of a
	 * descriptor into a DMA buffer then transmits corrupt bytes (measured
	 * 2026-10-15: EP0 IN completes, host rejects the content with -EPROTO).
	 * The SDIO transport programs the same table for the same reason. */
	static const uint32_t attributes[16] = {
		0x200, 0x10, 0x300, 0xc, 0x10000, 0x10, 0x14000, 0x10,
		0xf0000, 0x10, 0xfffff, 0x10, 0xfffff, 0x10, 0xfffff, 0x10
	};
	for (unsigned i = 0; i < 16; i++) { write32(UINT32_C(0xeffff000) + i * 4, attributes[i]); }
	barrier();

	/* Off the bus while the boot ROM's gadget is dismantled. */
	usbset(DCTL, DCTL_SFTDISCON);
	delay_ms(100);
	usbclr(GAHBCFG, GAHBCFG_GLBL_INTR_EN);

	/* USB_CoreInit, UTMI+ high-speed PHY. The PHY data width and turnaround
	 * time the ROM runs with are known-good; carry them across the reset. */
	uint32_t phy_bits = usbreg(GUSBCFG) & (GUSBCFG_PHYIF16 | GUSBCFG_TRDT_MASK);
	usbclr(GCCFG, GCCFG_PWRDWN);
	usbclr(GUSBCFG, GUSBCFG_ULPI_FSLS | GUSBCFG_PHYSEL);
	usbclr(GUSBCFG, GUSBCFG_ULPI_AUTORES | GUSBCFG_ULPI_CLK_SUSM);
	usbclr(GUSBCFG, GUSBCFG_ULPI_UTMI_SEL);
	core_reset();
	usbwr(GUSBCFG, (usbreg(GUSBCFG) & ~(GUSBCFG_PHYIF16 | GUSBCFG_TRDT_MASK)) | phy_bits);
	usbset(GAHBCFG, GAHBCFG_HBSTLEN_INCR16 | GAHBCFG_DMA_EN);

	/* USB_SetCurrentMode(device). */
	usbwr(GUSBCFG, (usbreg(GUSBCFG) & ~(GUSBCFG_FORCEHSTMODE | GUSBCFG_HNPCAP | GUSBCFG_SRPCAP)) | GUSBCFG_FORCEDEVMODE);
	delay_ms(50);

	/* USB_DevInit. */
	for (unsigned n = 1; n <= 15; n++) { usbwr(DIEPTXF(n), 0); }
	usbset(GCCFG, GCCFG_VBUSBSEN);          /* vbus_sensing = 1: no GOTGCTL overrides */
	usbwr(PCGCCTL, 0);
	usbwr(DCFG, usbreg(DCFG) & ~(DCFG_DEVSPD_MASK | DCFG_DEVADDR_MASK));   /* HS, address 0 */
	flush_tx_fifo(0x10);
	flush_rx_fifo();
	usbwr(DIEPMSK, 0); usbwr(DOEPMSK, 0); usbwr(DAINTMSK, 0);
	for (unsigned ep = 0; ep < 10; ep++) {
		uint32_t ctl = usbreg(DIEPCTL(ep));
		usbwr(DIEPCTL(ep), (ctl & DEPCTL_EPENA) ? (ep ? DEPCTL_EPDIS | DEPCTL_SNAK : DEPCTL_SNAK) : 0);
		usbwr(DIEPTSIZ(ep), 0);
		usbwr(DIEPINT(ep), DEPINT_ALL);
		ctl = usbreg(DOEPCTL(ep));
		usbwr(DOEPCTL(ep), (ctl & DEPCTL_EPENA) ? (ep ? DEPCTL_EPDIS | DEPCTL_SNAK : DEPCTL_SNAK) : 0);
		usbwr(DOEPTSIZ(ep), 0);
		usbwr(DOEPINT(ep), DEPINT_ALL);
	}
	usbwr(GINTMSK, 0);
	usbwr(GINTSTS, 0xBFFFFFFFu);
	usbwr(GINTMSK, GINTSTS_WKUPINT | GINTSTS_IPXFR | GINTSTS_IISOIXFR | GINTSTS_OEPINT | GINTSTS_IEPINT |
		GINTSTS_ENUMDNE | GINTSTS_USBRST | GINTSTS_USBSUSP | GINTSTS_SRQINT | GINTSTS_OTGINT);

	/* Stock FIFO layout: RX 1024, FIFO0 128 @1024, FIFO1 768, FIFO2/3 256,
	 * FIFO4..10 64; the endpoint-info area follows and ends at DfifoDepth. */
	usbwr(GRXFSIZ, 0x400u);
	usbwr(GNPTXFSIZ, (0x80u << 16) | 0x400u);
	set_tx_fifo(1, 0x300); set_tx_fifo(2, 0x100); set_tx_fifo(3, 0x100);
	for (unsigned n = 4; n <= 10; n++) { set_tx_fifo(n, 0x40); }
	uint32_t ep_info = usbreg(GRXFSIZ) + (usbreg(GNPTXFSIZ) >> 16);
	for (unsigned n = 1; n <= 10; n++) { ep_info += usbreg(DIEPTXF(n)) >> 16; }
	usbwr(GDFIFOCFG, (ep_info << 16) | (usbreg(GHWCFG3) >> 16));

	/* HAL_PCD_Start: PHY power, clocks, EP0 ready, connect. */
	usbset(GCCFG, GCCFG_PWRDWN);
	usbwr(PCGCCTL, 0);
	ep0_masks();
	ep0_start_rx();
	usbclr(DCTL, DCTL_SFTDISCON);
	barrier();
}

size_t ar_usb_receive(const uint8_t **data)
{
	/* The cooperative consumer has finished the previous returned buffer.
	 * Sending replies during consumption must not release it to DMA. */
	rx_held = 0;
	barrier();
	stats.polls++;
	uint32_t gintsts = usbreg(GINTSTS);

	if (gintsts & GINTSTS_USBRST) {
		usbwr(GINTSTS, GINTSTS_USBRST);
		stats.resets++;
		configured = 0; rx_armed = 0; tx_pending = 0; tx_head = tx_count = 0; pending_address = 0;
		usbclr(DCTL, DCTL_RWUSIG);
		flush_tx_fifo(0x10);
		for (unsigned ep = 0; ep < 10; ep++) {
			usbwr(DIEPINT(ep), DEPINT_ALL);
			usbclr(DIEPCTL(ep), DEPCTL_STALL);
			usbwr(DOEPINT(ep), DEPINT_ALL);
			usbwr(DOEPCTL(ep), (usbreg(DOEPCTL(ep)) & ~DEPCTL_STALL) | DEPCTL_SNAK);
		}
		ep0_masks();
		usbclr(DCFG, DCFG_DEVADDR_MASK);
		ep0_start_rx();
	}

	if (gintsts & GINTSTS_ENUMDNE) {
		usbwr(GINTSTS, GINTSTS_ENUMDNE);
		usbclr(DIEPCTL(0), 3u);            /* EP0 MPS code 0 = 64 (HS and FS) */
		usbset(DCTL, DCTL_CGNPINNAK);
		/* Turnaround time as stock sets it for high speed. */
		usbwr(GUSBCFG, (usbreg(GUSBCFG) & ~GUSBCFG_TRDT_MASK) | (9u << 10));
		ep0_start_rx();
	}

	if (gintsts & GINTSTS_OEPINT) {
		uint32_t oep0 = usbreg(DOEPINT(0));
		if (oep0 & DEPINT_SETUP) {
			usbwr(DOEPINT(0), DEPINT_SETUP | DEPINT_XFERCOMPL);
			ar_cache_invalidate(setup_buf, sizeof setup_buf);
			handle_setup(setup_buf);        /* re-arms EP0 OUT itself */
		} else if (oep0 & DEPINT_XFERCOMPL) {
			usbwr(DOEPINT(0), DEPINT_XFERCOMPL);
			ep0_start_rx();
		}
		if (configured) {
			uint32_t oep1 = usbreg(DOEPINT(BULK_OUT_EP));
			if (oep1 & DEPINT_XFERCOMPL) {
				usbwr(DOEPINT(BULK_OUT_EP), DEPINT_XFERCOMPL);
				rx_armed = 0;
				barrier();
				uint32_t remaining = usbreg(DOEPTSIZ(BULK_OUT_EP)) & DEPTSIZ_XFERSIZE_MASK;
				size_t length = AR_USB_BUFFER_SIZE - remaining;
				if (length && length <= AR_USB_BUFFER_SIZE) {
					ar_cache_invalidate(receive_buffer, length);
					*data = receive_buffer;
					stats.rx_packets++;
					last_rx_length = length;
					rx_held = 1;
					return length;
				}
				stats.invalid_rx++;
			}
		}
	}

	if (gintsts & GINTSTS_IEPINT) {
		uint32_t iep0 = usbreg(DIEPINT(0));
		if (iep0 & DEPINT_XFERCOMPL) {
			usbwr(DIEPINT(0), DEPINT_XFERCOMPL);
			if (pending_address) {
				usbwr(DCFG, (usbreg(DCFG) & ~DCFG_DEVADDR_MASK) | (pending_address << DCFG_DEVADDR_SHIFT));
				pending_address = 0;
			}
		}
		if (configured) {
			uint32_t iep1 = usbreg(DIEPINT(BULK_IN_EP));
			if (iep1 & DEPINT_XFERCOMPL) {
				usbwr(DIEPINT(BULK_IN_EP), DEPINT_XFERCOMPL);
				finish_bulk_tx();
			}
		}
	}

	start_bulk_tx();
	arm_bulk_rx();
	return 0;
}

unsigned ar_usb_tx_room(void) { return configured ? AR_USB_TX_DEPTH - tx_count : 0; }

int ar_usb_send(const uint8_t *data, size_t length)
{
	if (!configured || !length || length > AR_USB_BUFFER_SIZE) return -1;
	if (tx_count == AR_USB_TX_DEPTH) { stats.tx_backpressure++; return -1; }
	unsigned tail = (tx_head + tx_count) % AR_USB_TX_DEPTH;
	memcpy(transmit_buffers[tail], data, length);
	transmit_lengths[tail] = length;
	tx_count++;
	if (tx_count > stats.tx_queue_high_water) stats.tx_queue_high_water = tx_count;
	start_bulk_tx();
	arm_bulk_rx();
	return 0;
}

const struct ar_usb_stats *ar_usb_statistics(void) { return &stats; }

void ar_usb_record_fault(uint32_t reason, uint32_t cause, uint32_t pc, uint32_t value)
{
	(void)reason; (void)cause; (void)pc; (void)value;
}

void ar_usb_record_progress(uint32_t stage, uint32_t a, uint32_t b, uint32_t c)
{
	(void)stage; (void)a; (void)b; (void)c;
}

#ifdef AR_USB_PULSE_DIAG
/* Register read-out with no working EP0: run the service loop ~2.5 s, then
 * clock four words out by pulsing soft-disconnect. The host logs each
 * connect ("new high-speed USB device") with a timestamp, so the interval
 * between consecutive connects carries one bit (delay_ms runs 2x slow):
 *   bit 0 = 2.0 s period, bit 1 = 3.2 s; preamble 3 x 3.8 s; +3 s between
 * words. MSB first. */
static void pulse_bit(unsigned one)
{
	usbclr(DCTL, DCTL_SFTDISCON);
	delay_ms(one ? 1200 : 600);
	usbset(DCTL, DCTL_SFTDISCON);
	delay_ms(400);
}
void ar_usb_pulse_diag(void)
{
	uint32_t t0 = read32(UINT32_C(0xe000bff8));
	while ((uint32_t)(read32(UINT32_C(0xe000bff8)) - t0) < 100000000u) {
		const uint8_t *d; (void)ar_usb_receive(&d);
	}
	uint32_t regs[4] = {
		((stats.polls & 0xffffu) << 16) | ((stats.resets & 0xfu) << 8) | ((stats.enumerated & 0xfu) << 4) |
			(stats.setup_packets & 0xfu),
		usbreg(GINTSTS), usbreg(DIEPTSIZ(0)), usbreg(DIEPINT(0)) };
	usbset(DCTL, DCTL_SFTDISCON);
	delay_ms(1000);
	for (unsigned i = 0; i < 3; i++) {
		usbclr(DCTL, DCTL_SFTDISCON); delay_ms(1500);
		usbset(DCTL, DCTL_SFTDISCON); delay_ms(400);
	}
	for (unsigned r = 0; r < 4; r++) {
		for (int b = 31; b >= 0; b--) { pulse_bit((regs[r] >> b) & 1u); }
		delay_ms(1500);
	}
	for (;;) { barrier(); }
}
#endif
