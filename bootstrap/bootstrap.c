/* SPDX-License-Identifier: MIT */
#include "bootstrap.h"

/* Ascent VRX 18.21.10 bootstrap byte-bound contracts. Original instructions and
 * uncertainty are recorded privately; these are independently authored register
 * operations. Do not infer physical bit names beyond the observed handshake. */
#define CONTROL UINT32_C(0x0a090000)
#define UNLOCK UINT32_C(0x0a106100)
#define BB UINT32_C(0x0a110000)
#define SCRATCH UINT32_C(0x0a098ff8)
#define COUNTER UINT32_C(0xe000bff8)
#define COMPARE UINT32_C(0xe000c000)
#define ENTRY UINT32_C(0x0a098a00)
#ifdef AR_BOOTSTRAP_SIMULATED
uint32_t ar_bootstrap_read(uint32_t address, unsigned width);
void ar_bootstrap_write(uint32_t address, uint32_t value, unsigned width);
#define read32(a) ar_bootstrap_read((a), 4)
#define read16(a) ar_bootstrap_read((a), 2)
#define read8(a) ar_bootstrap_read((a), 1)
#define write32(a,v) ar_bootstrap_write((a),(v),4)
#define write16(a,v) ar_bootstrap_write((a),(v),2)
#define write8(a,v) ar_bootstrap_write((a),(v),1)
#else
static uint32_t read32(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static uint16_t read16(uint32_t a) { return *(volatile uint16_t *)(uintptr_t)a; }
static uint8_t read8(uint32_t a) { return *(volatile uint8_t *)(uintptr_t)a; }
static void write32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static void write16(uint32_t a, uint16_t v) { *(volatile uint16_t *)(uintptr_t)a = v; }
static void write8(uint32_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t)a = v; }
#endif
static void update(uint32_t address, uint32_t clear, uint32_t set)
{
	write32(address, (read32(address) & ~clear) | set);
}
static void unlock(uint32_t mask)
{
	write32(UNLOCK, 0xacce55);
	write32(UNLOCK + 4, 0x3ffff);
	write32(UNLOCK + 4, mask);
}

int8_t ar_bootstrap_trim(int8_t previous, uint32_t compare, uint32_t counter, uint32_t reference)
{
	uint32_t difference = compare || counter > reference ? counter - reference : reference - counter;
	if (difference > 2000000) { return 0; }
	if (!compare && counter < reference) {
		return previous >= -9 ? previous - 1 : previous;
	}
	return difference > 2000 && previous <= 9 ? previous + 1 : previous;
}

enum ar_bootstrap_result ar_bootstrap_run(void)
{
	static const uint32_t attributes[16] = {
		0x10,0xc,0x200,0x10,0x300,0x10,0xa098,0x10,
		0xa099,0xc,0xf0000,0x10,0xfffff,0x10,0xfffff,0x10
	};
	for (unsigned i = 0; i < 16; i++) { write32(0xeffff000u + i * 4, attributes[i]); }
	unlock(0xffff);
	write32(CONTROL + 0x100, 32);
	unsigned locked = 0;
	for (unsigned remaining = 0x1000000;; remaining--) {
		if (read32(0x0a100284) & 0x400000) { locked = 1; break; }
		if (!remaining) { break; }
	}
	if (locked) {
		update(BB + 0x300, 0, 16); update(BB + 0x300, 16, 0);
		write32(CONTROL + 0x100, 0);
		update(BB + 8, 0x800000, 0);
		update(BB + 0x1c, 0x1000000, 0);
		update(BB + 0x328, 0xff0000, 0);
		update(BB + 0xbb4, 0xe00000, 0);
		update(BB + 0xbb0, 0, 128);
		update(BB + 0xcd0, 0, 1);
		update(BB + 0x800, 9, 0);
		update(BB + 0x3b0, 0, 128);
		/* Original waits for this indication while printing UART dots. Retain
		 * the hardware gate; omit UART debug output and bound a failed wait. */
		locked = 0;
		for (unsigned remaining = 0x1000000;; remaining--) {
			if (read32(BB + 0xf00) & 8) { locked = 1; break; }
			if (!remaining) { break; }
		}
	}
	if (read8(SCRATCH + 2) & 1) {
		/* The original halfword mask also clears the signed adjustment byte. */
		write16(SCRATCH + 2, read16(SCRATCH + 2) & 0xfe);
	} else {
		uint32_t compare, counter, again, high;
		unsigned stable = 0;
		for (unsigned remaining = 0x1000000;; remaining--) {
			compare = read32(COMPARE); (void)read32(COMPARE + 4);
			counter = read32(COUNTER); (void)read32(COUNTER + 4);
			again = read32(COMPARE); high = read32(COMPARE + 4);
			if (compare == again && !high) { stable = 1; break; }
			if (!remaining) { break; }
		}
		if (!stable) { write32(CONTROL + 0x134, 0); return AR_BOOTSTRAP_ROM_FALLBACK; }
		int8_t previous = (int8_t)read8(SCRATCH + 3);
		int8_t next = ar_bootstrap_trim(previous, compare, counter, read32(SCRATCH + 4));
		if (next != previous) { write8(SCRATCH + 3, (uint8_t)next); }
	}
	update(CONTROL + 0x104, 0, 8);
	int adjustment = (int8_t)read8(SCRATCH + 3);
	uint32_t value = read16(SCRATCH) + adjustment;
	(void)read32(CONTROL + 0x18);
	write32(CONTROL + 8, 0); write32(CONTROL + 4, value);
	write32(CONTROL + 0x20, 1); write32(CONTROL + 8, 0);
	write32(CONTROL + 0xc, 5);
	if (!locked) { write32(CONTROL + 0x134, 0); return AR_BOOTSTRAP_ROM_FALLBACK; }
	write32(CONTROL + 0x134, ENTRY | 1);
	unlock(0x3fff0);
	write32(CONTROL + 0x100, 1);
	return AR_BOOTSTRAP_RESET;
}

#ifndef AR_BOOTSTRAP_SIMULATED
__attribute__((noreturn)) void ar_bootstrap_main(void)
{
	if (ar_bootstrap_run() == AR_BOOTSTRAP_ROM_FALLBACK) {
		__asm__ volatile ("jr zero");
	}
	for (;;) { __asm__ volatile ("nop"); }
}
#endif
