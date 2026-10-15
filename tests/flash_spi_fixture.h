/* SPDX-License-Identifier: MIT */
#ifndef AR_FLASH_SPI_FIXTURE_H
#define AR_FLASH_SPI_FIXTURE_H
#include "ar_flash_spi.h"
#include <assert.h>
#include <string.h>

#define SPI 0x01e00000u
#define CLOCK 0x0a104008u
struct device {
	uint32_t regs[65], clock;
	uint64_t time;
	unsigned step, writes, commands, active, head, stale, delay, fifo_stall, idle_stall;
	unsigned end_stall, stall_command;
	const uint8_t *sfdp;
	size_t sfdp_length;
	const uint8_t *array;
	size_t array_length;
	uint8_t id[6];
};
static uint8_t data_byte(unsigned offset) { return offset * 37 + 11; }
static uint32_t read32(void *ctx, uint32_t address)
{
	struct device *d=ctx;
	if (address==CLOCK) { return d->clock; }
	assert(address>=SPI && address<=SPI+0x100 && !(address&3));
	unsigned off=address-SPI;
	if (off==0x30) { return d->idle_stall || d->active || (d->end_stall && d->commands); }
	if (off==0x34) {
		if (d->stale) { return d->stale; }
		if (d->fifo_stall || !d->active || (d->stall_command && d->commands==d->stall_command)) { return 0; }
		if (d->delay) { d->delay--; return 0; }
		return 1;
	}
	if (off==0x100) {
		if (d->stale) { d->stale--; return 0xdeadbeef; }
		assert(d->active && d->head<d->regs[0x1c/4]);
		uint32_t word=0;
		for (unsigned i=0;i<4 && d->head<d->regs[0x1c/4];i++,d->head++) {
			unsigned offset=d->regs[0x18/4]+d->head, opcode=d->regs[0x14/4]&255;
			uint8_t byte=data_byte(offset);
			if (d->sfdp && opcode==0x9f) { assert(d->head<6); byte=d->id[d->head]; }
			if (d->sfdp && opcode==0x5a) { assert(offset<d->sfdp_length); byte=d->sfdp[offset]; }
			if (d->array && opcode==3) { assert(offset<d->array_length); byte=d->array[offset]; }
			word |= (uint32_t)byte<<(8*i);
		}
		if (d->head==d->regs[0x1c/4]) { d->active=0; d->regs[0x10/4]&=~1u; }
		return word;
	}
	return d->regs[off/4];
}
static void write32(void *ctx, uint32_t address, uint32_t value)
{
	struct device *d=ctx; d->writes++;
	if (address==CLOCK) { d->clock=value; return; }
	assert(address>=SPI && address<SPI+0x100 && !(address&3));
	d->regs[(address-SPI)/4]=value;
	if (address==SPI+0x10 && (value&1)) {
		assert(!d->active && !d->stale);
		unsigned opcode=d->regs[0x14/4]&255, bytes=(d->regs[0x14/4]>>8)&7;
		unsigned dummy=(d->regs[0x14/4]>>12)&15;
		assert((opcode==3 && bytes==3 && !dummy) || (opcode==0x9f && !bytes && !dummy) ||
		       (opcode==0x5a && bytes==3 && dummy==1));
		assert(!(d->regs[0x14/4]&0x01ff0800) && !(d->regs[0x20/4]&0x80));
		d->commands++; d->active=1; d->head=0;
	}
}
static uint64_t time_us(void *ctx) { struct device *d=ctx; d->time+=d->step; return d->time; }
static const struct ar_flash_spi_io io={read32,write32,time_us};
static void reset(struct device *d)
{
	memset(d,0,sizeof *d); d->step=1; d->clock=0xa5a5a5a5;
	for (unsigned i=0;i<65;i++) { d->regs[i]=0x5a5a5a5a; }
	/* Device busy/FIFO/start are modeled independently from opaque bits. */
	d->regs[0x10/4]&=~1u;
}
#endif
