/* SPDX-License-Identifier: MIT */
#include "ar_factory.h"

static uint8_t scratch[AR_FLASH_TABLE_BYTES];
static uint32_t read32(void *context, uint32_t address)
{
	(void)context; return *(volatile uint32_t *)(uintptr_t)address;
}
static void write32(void *context, uint32_t address, uint32_t value)
{
	(void)context; *(volatile uint32_t *)(uintptr_t)address = value;
	__asm__ volatile("fence rw,rw" ::: "memory");
}
static uint64_t time_us(void *context)
{
	uint32_t high, low, again;
	do {
		high = read32(context, 0xe000bffc); low = read32(context, 0xe000bff8);
		again = read32(context, 0xe000bffc);
	} while (high != again);
	return ((uint64_t)high << 32 | low) / 40;
}
int ar_factory_device_read(const struct ar_flash_part *parts, size_t count,
	uint8_t record[8], struct ar_factory_info *info)
{
	if (!record || !info || ar_flash_parts_validate(parts, count)) { return -22; }
	/* Original 0x2146a6(45) masks this controller's CLIC interrupt. The
 * native driver polls; it neither installs a handler nor enables interrupts. */
	volatile uint8_t *enable = (volatile uint8_t *)(uintptr_t)(0xe0801001u + 45 * 4);
	*enable &= (uint8_t)~1u;
	__asm__ volatile("fence rw,rw" ::: "memory");
	static const struct ar_flash_spi_io io = {read32, write32, time_us};
	return ar_factory_read(&io, NULL, parts, count, scratch, sizeof scratch, record, info);
}

/* Diagnostic, read-only: length bytes of the SPI flash at offset. */
int ar_factory_device_flash_read(uint32_t capacity, uint32_t offset, uint8_t *out, size_t length)
{
	static struct ar_flash_spi spi;
	static unsigned ready;
	static const struct ar_flash_spi_io io = {read32, write32, time_us};
	if (!out || !capacity || offset >= capacity || length > capacity - offset) { return -22; }
	if (!ready) {
		volatile uint8_t *enable = (volatile uint8_t *)(uintptr_t)(0xe0801001u + 45 * 4);
		*enable &= (uint8_t)~1u;
		__asm__ volatile("fence rw,rw" ::: "memory");
		int result = ar_flash_spi_init(&spi, &io, NULL, capacity);
		if (result) { return result; }
		ready = 1;
	}
	return ar_flash_spi_read(&spi, offset, out, length);
}
