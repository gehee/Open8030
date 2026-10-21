/* SPDX-License-Identifier: MIT */
/* The USB device (DWC2, buffer DMA) against a synthetic register file:
 * stock's core/FIFO bring-up, EP0 standard requests (the address written at
 * SET_ADDRESS itself), bulk IN queueing and completion, bulk OUT, bus reset.
 * Real enumeration still needs hardware. */
#include "ar_usb_device.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE 0x04000000u
#define GAHBCFG 0x008u
#define GUSBCFG 0x00Cu
#define GRSTCTL 0x010u
#define GINTSTS 0x014u
#define GRXFSIZ 0x024u
#define GNPTXFSIZ 0x028u
#define GCCFG 0x038u
#define GHWCFG3 0x04Cu
#define GDFIFOCFG 0x05Cu
#define DIEPTXF(n) (0x104u + ((n) - 1) * 4u)
#define DCFG 0x800u
#define DCTL 0x804u
#define DIEPCTL(n) (0x900u + (n) * 0x20u)
#define DIEPINT(n) (0x908u + (n) * 0x20u)
#define DIEPTSIZ(n) (0x910u + (n) * 0x20u)
#define DIEPDMA(n) (0x914u + (n) * 0x20u)
#define DOEPCTL(n) (0xB00u + (n) * 0x20u)
#define DOEPINT(n) (0xB08u + (n) * 0x20u)
#define DOEPTSIZ(n) (0xB10u + (n) * 0x20u)
#define DOEPDMA(n) (0xB14u + (n) * 0x20u)

static struct { uint32_t address, value; } regs[512];
static unsigned used;
static uint32_t *reg(uint32_t address)
{
	for (unsigned i = 0; i < used; i++) if (regs[i].address == address) return &regs[i].value;
	assert(used < 512);
	regs[used].address = address; regs[used].value = 0;
	return &regs[used++].value;
}
static uint32_t *usb(uint32_t offset) { return reg(BASE + offset); }
static unsigned w1c(uint32_t offset)
{
	if (offset == GINTSTS) return 1;
	for (unsigned n = 0; n < 16; n++) if (offset == DIEPINT(n) || offset == DOEPINT(n)) return 1;
	return 0;
}
uint32_t ar_test_read32(uint32_t address)
{
	uint32_t v = *reg(address);
	/* Core reset and FIFO flushes finish at once; the AHB master is idle. */
	if (address == BASE + GRSTCTL) { v = (v & ~0x31u) | 1u << 31; }
	return v;
}
void ar_test_write32(uint32_t address, uint32_t value)
{
	assert((address >= BASE && address < BASE + 0x1000) || (address >= 0xeffff000 && address < 0xeffff040));
	if (address >= BASE && w1c(address - BASE)) { *reg(address) &= ~value; return; }
	*reg(address) = value;
}
static void *dma(uint32_t offset) { return (void *)(uintptr_t)*usb(offset); }

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t length)
{
	uint8_t packet[8] = {type, request, (uint8_t)value, (uint8_t)(value >> 8), 0, 0, (uint8_t)length, (uint8_t)(length >> 8)};
	assert(dma(DOEPDMA(0)));
	memcpy(dma(DOEPDMA(0)), packet, 8);
	*usb(DOEPINT(0)) |= 1u << 3;           /* SETUP done */
	*usb(GINTSTS) |= 1u << 19;             /* OEPINT */
	const uint8_t *data;
	assert(ar_usb_receive(&data) == 0);
	assert(!(*usb(DOEPINT(0)) & (1u << 3)));
}

int main(void)
{
	*usb(GUSBCFG) = 1u << 3 | 9u << 10;    /* the ROM's PHY width and turnaround */
	*usb(GHWCFG3) = 2984u << 16;
	ar_usb_init();
	/* Stock's bring-up: buffer DMA with INCR16, device mode with the ROM's PHY
	 * bits kept, PHY power and VBUS sensing on, the FIFO map, connected. */
	assert((*usb(GAHBCFG) & 0x2e) == 0x2e && !(*usb(GAHBCFG) & 1));
	assert((*usb(GUSBCFG) & (1u << 30)) && (*usb(GUSBCFG) & (1u << 3)) && ((*usb(GUSBCFG) >> 10) & 15) == 9);
	assert((*usb(GCCFG) & (1u << 16)) && (*usb(GCCFG) & (1u << 21)));
	assert(*usb(GRXFSIZ) == 0x400 && *usb(GNPTXFSIZ) == (0x80u << 16 | 0x400));
	assert(*usb(DIEPTXF(1)) == (0x300u << 16 | 0x480) && *usb(DIEPTXF(2)) == (0x100u << 16 | 0x780));
	assert(*usb(DIEPTXF(3)) == (0x100u << 16 | 0x880) && *usb(DIEPTXF(10)) == (0x40u << 16 | 0xb00));
	assert(*usb(GDFIFOCFG) == (2880u << 16 | 2984));
	assert(!(*usb(DCTL) & 2) && *reg(0xeffff000) == 0x200);   /* connected; memory attributes set */
	assert(dma(DOEPDMA(0)) && (*usb(DOEPCTL(0)) & (1u << 31)));
	assert(ar_usb_tx_room() == 0 && ar_usb_send((const uint8_t *)"x", 1) == -1);   /* not configured */

	/* Bus reset and enumeration done. */
	*usb(DCFG) = 7u << 4; *usb(GINTSTS) |= 1u << 12;
	const uint8_t *data;
	assert(ar_usb_receive(&data) == 0 && !(*usb(DCFG) & (0x7fu << 4)));
	*usb(DIEPCTL(0)) |= 3; *usb(GINTSTS) |= 1u << 13;
	assert(ar_usb_receive(&data) == 0 && !(*usb(DIEPCTL(0)) & 3));

	/* GET_DESCRIPTOR device: 18 bytes, 1d6b:8030; a short wLength clips it. */
	setup(0x80, 6, 0x0100, 64);
	const uint8_t *d = dma(DIEPDMA(0));
	assert((*usb(DIEPTSIZ(0)) & 0x7f) == 18 && d[0] == 18 && d[1] == 1);
	assert(d[8] == 0x6b && d[9] == 0x1d && d[10] == 0x30 && d[11] == 0x80);
	setup(0x80, 6, 0x0100, 8);
	assert((*usb(DIEPTSIZ(0)) & 0x7f) == 8);
	setup(0x80, 6, 0x0200, 255);           /* configuration: 32 bytes, two bulk endpoints */
	d = dma(DIEPDMA(0));
	assert((*usb(DIEPTSIZ(0)) & 0x7f) == 32 && d[1] == 2 && d[2] == 32 && d[20] == 0x81 && d[27] == 0x01);
	setup(0x80, 6, 0x0600, 10);            /* device qualifier */
	assert((*usb(DIEPTSIZ(0)) & 0x7f) == 10);
	*usb(DIEPCTL(0)) &= ~(1u << 21);
	setup(0x80, 6, 0x0300, 4);             /* no string descriptors: stall */
	assert(*usb(DIEPCTL(0)) & (1u << 21));
	*usb(DIEPCTL(0)) &= ~(1u << 21);
	setup(0x40, 1, 0, 0);                  /* vendor requests: stall */
	assert(*usb(DIEPCTL(0)) & (1u << 21));
	*usb(DIEPCTL(0)) &= ~(1u << 21);

	/* SET_ADDRESS: the address is in DCFG at once, before the status ZLP. */
	setup(0x00, 5, 23, 0);
	assert(((*usb(DCFG) >> 4) & 0x7f) == 23 && (*usb(DIEPTSIZ(0)) & 0x7f) == 0);

	/* SET_CONFIGURATION 1: both bulk endpoints live, OUT armed. */
	setup(0x00, 9, 1, 0);
	assert(ar_usb_tx_room() == AR_USB_TX_DEPTH);
	assert((*usb(DIEPCTL(1)) & (1u << 15)) && (*usb(DOEPCTL(1)) & (1u << 15)));
	setup(0x80, 8, 0, 1);                  /* GET_CONFIGURATION */
	assert(*(const uint8_t *)dma(DIEPDMA(0)) == 1);

	/* Bulk IN: one transfer in flight, the rest queued in order. */
	static uint8_t frames[AR_USB_TX_DEPTH + 1][1000];
	for (unsigned i = 0; i <= AR_USB_TX_DEPTH; i++) memset(frames[i], 0x30 + i, sizeof frames[i]);
	assert(ar_usb_send(frames[0], 0) == -1 && ar_usb_send(frames[0], AR_USB_BUFFER_SIZE + 1) == -1);
	assert(!ar_usb_send(frames[0], 1000));
	assert((*usb(DIEPTSIZ(1)) & 0x7ffff) == 1000 && (*usb(DIEPTSIZ(1)) >> 19 & 0x3ff) == 2);
	assert(!memcmp(dma(DIEPDMA(1)), frames[0], 1000));
	for (unsigned i = 1; i < AR_USB_TX_DEPTH; i++) assert(!ar_usb_send(frames[i], 512));
	assert(ar_usb_tx_room() == 0 && ar_usb_send(frames[AR_USB_TX_DEPTH], 10) == -1);
	assert(ar_usb_statistics()->tx_backpressure == 1);
	for (unsigned i = 1; i < AR_USB_TX_DEPTH; i++) {
		*usb(DIEPINT(1)) |= 1; *usb(GINTSTS) |= 1u << 18;   /* XFERCOMPL */
		assert(ar_usb_receive(&data) == 0);
		assert(ar_usb_tx_room() == i && !memcmp(dma(DIEPDMA(1)), frames[i], 512));
		assert((*usb(DIEPTSIZ(1)) & 0x7ffff) == 512 && (*usb(DIEPTSIZ(1)) >> 19 & 0x3ff) == 1);
	}
	*usb(DIEPINT(1)) |= 1; *usb(GINTSTS) |= 1u << 18;
	assert(ar_usb_receive(&data) == 0 && ar_usb_tx_room() == AR_USB_TX_DEPTH);

	/* Bulk OUT: the bytes the host sent, from the armed buffer. */
	uint8_t *out = dma(DOEPDMA(1));
	assert(out && ((*usb(DOEPTSIZ(1)) & 0x7ffff) == AR_USB_BUFFER_SIZE));
	memset(out, 0xa5, 100);
	*usb(DOEPCTL(1)) &= ~(1u << 31); /* hardware clears EPENA on completion */
	*usb(DOEPTSIZ(1)) = AR_USB_BUFFER_SIZE - 100;
	*usb(DOEPINT(1)) |= 1; *usb(GINTSTS) |= 1u << 19;
	assert(ar_usb_receive(&data) == 100 && data == out && data[99] == 0xa5);
	/* A queued host packet must not overwrite bytes still being parsed. */
	assert(!(*usb(DOEPCTL(1)) & (1u << 31)));
	assert(!ar_usb_send(frames[0], 100)); /* parser callback sends a reply */
	assert(!(*usb(DOEPCTL(1)) & (1u << 31)) && data[0] == 0xa5);
	assert(ar_usb_receive(&data) == 0); /* consumer releases the buffer */
	assert((*usb(DOEPCTL(1)) & (1u << 31)) && dma(DOEPDMA(1)) == out);
	/* Also release a held buffer correctly when the next poll sees reset. */
	memset(out, 0x5a, 100);
	*usb(DOEPCTL(1)) &= ~(1u << 31);
	*usb(DOEPTSIZ(1)) = AR_USB_BUFFER_SIZE - 100;
	*usb(DOEPINT(1)) |= 1;
	assert(ar_usb_receive(&data) == 100 && data[0] == 0x5a);

	/* A bus reset drops the configuration and the queue. */
	assert(!ar_usb_send(frames[0], 100));
	*usb(GINTSTS) |= 1u << 12;
	assert(ar_usb_receive(&data) == 0 && ar_usb_tx_room() == 0 && ar_usb_send(frames[0], 1) == -1);
	puts("USB device bring-up, EP0 requests, bulk queueing and reset passed");
	return 0;
}
