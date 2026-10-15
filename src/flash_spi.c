/* SPDX-License-Identifier: MIT */
#include "ar_flash_spi.h"
#include <string.h>

#define SPI UINT32_C(0x01e00000)
#define CLOCK UINT32_C(0x0a104008)
#define TIMEOUT_US UINT64_C(50000)
#define MAX_POLLS 1000000u
struct deadline { uint64_t start; unsigned polls; };
static uint32_t read_reg(struct ar_flash_spi *s, unsigned offset)
{
	return s->io->read32(s->context, SPI + offset);
}
static void write_reg(struct ar_flash_spi *s, unsigned offset, uint32_t v)
{
	s->io->write32(s->context, SPI + offset, v);
}
static void update(struct ar_flash_spi *s, unsigned offset, uint32_t mask, uint32_t v)
{
	write_reg(s, offset, (read_reg(s, offset) & ~mask) | v);
}
static int failed(struct ar_flash_spi *s, int error) { s->error = error; s->ready = 0; return error; }
static int expired(struct ar_flash_spi *s, struct deadline *d)
{
	return ++d->polls >= MAX_POLLS || s->io->time_us(s->context) - d->start >= TIMEOUT_US;
}
static int idle(struct ar_flash_spi *s, struct deadline *d)
{
	while (read_reg(s, 0x30) & 1) { if (expired(s, d)) { return -110; } }
	return 0;
}
int ar_flash_spi_init(struct ar_flash_spi *s, const struct ar_flash_spi_io *io,
	void *context, uint32_t capacity)
{
	if (!s || !io || !io->read32 || !io->write32 || !io->time_us ||
	    (capacity && capacity < 0xa000) || capacity > 0x1000000) { return -22; }
	*s = (struct ar_flash_spi){.io=io, .context=context, .capacity=capacity};
	struct deadline d = {.start=io->time_us(context)};
	int result = idle(s, &d);
	if (result) { return failed(s, result); }
	/* An idle status does not authorize rewriting an asserted start bit. */
	if (read_reg(s, 0x10) & 1) { return failed(s, -16); }
	/* Stale FIFO bytes cannot become the first bytes of a new record. */
	while (read_reg(s, 0x34) & 0x7f) {
		(void)read_reg(s, 0x100);
		if (expired(s, &d)) { return failed(s, -110); }
	}
	/* Original 2,000,000-rate request maps to source0/divider5, with clock
	 * gating around the update. Actual SCLK still requires hardware evidence. */
	uint32_t clock = io->read32(context, CLOCK);
	io->write32(context, CLOCK, clock & ~(1u << 12));
	clock &= ~UINT32_C(0x137f); clock |= 4;
	io->write32(context, CLOCK, clock);
	io->write32(context, CLOCK, clock | 1u << 12);
	write_reg(s, 0x44, 1); write_reg(s, 0x40, 0);
	update(s, 0x20, 0x8080, 0); /* No DMA or receive interrupt trigger. */
	update(s, 8, 0x3c00, 0); /* Matched initial sampling delay. */
	update(s, 4, 0x80000, 0); /* Single data rate. */
	update(s, 0x10, 0x3fc, 0); /* Standard frame; command/address/data one lane. */
	if (read_reg(s, 0x10) & 1) { return failed(s, -16); }
	s->ready = 1; return 0;
}
static int transfer(struct ar_flash_spi *s, unsigned opcode, unsigned address_bytes, unsigned dummy_bytes,
	uint32_t offset, uint8_t *out, size_t length)
{
	if (!s || !out || !length || length > AR_FLASH_TABLE_BYTES) { return -22; }
	if (s->error) { return s->error; }
	if (!s->ready || !s->io) { return -11; }
	struct deadline d = {.start=s->io->time_us(s->context)};
	int result = idle(s, &d);
	if (result) { return failed(s, result); }
	if ((read_reg(s, 0x34) & 0x7f) || (read_reg(s, 0x10) & 1)) { return failed(s, -16); }
	s->last_offset = offset; s->last_opcode = opcode;
	update(s, 0x20, 0x80, 0);
	/* Clear direction/dummy/address/continuous-read controls before setting
	 * the read command. Address is the little-endian scalar used by the
	 * original controller helper, not a software-swapped SPI byte stream. */
	uint32_t command = read_reg(s, 0x14) & ~UINT32_C(0x01ffffff);
	write_reg(s, 0x14, command | opcode | address_bytes << 8 | dummy_bytes << 12);
	write_reg(s, 0x18, offset); write_reg(s, 0x1c, length);
	update(s, 0x10, 1, 1);
	for (size_t done = 0; done < length;) {
		if (expired(s, &d)) { return failed(s, -110); }
		if (!(read_reg(s, 0x34) & 0x7f)) { continue; }
		uint32_t word = read_reg(s, 0x100);
		for (unsigned byte = 0; byte < 4 && done < length; byte++, done++) { out[done] = word >> (byte * 8); }
	}
	result = idle(s, &d);
	if (result) { return failed(s, result); }
	s->transfers++; s->bytes += length; return 0;
}
int ar_flash_spi_read_id(struct ar_flash_spi *s, uint8_t out[6])
{
	if (s) { s->id_valid = 0; memset(s->observed_id, 0, sizeof s->observed_id); }
	int result = transfer(s, 0x9f, 0, 0, 0, out, 6);
	if (!result) { memcpy(s->observed_id, out, sizeof s->observed_id); s->id_valid = 1; }
	return result;
}
static int read_sfdp(void *context, uint32_t offset, uint8_t *out, size_t length)
{
	if (offset > 0xffffff || length > 0x1000000u - offset) { return -22; }
	return transfer(context, 0x5a, 3, 1, offset, out, length);
}
int ar_flash_spi_discover(struct ar_flash_spi *s, uint8_t *scratch, size_t length,
	uint8_t id[6], struct ar_sfdp_info *info)
{
	if (!id || !info) { return -22; }
	struct ar_flash_identity identity;
	int result = ar_flash_spi_identify(s, NULL, 0, scratch, length, &identity);
	if (result) { return result; }
	memcpy(id, identity.id, sizeof identity.id); *info = identity.sfdp; return 0;
}
int ar_flash_parts_validate(const struct ar_flash_part *parts, size_t count)
{
	if ((count && !parts) || count > 256) { return -22; }
	for (size_t i = 0; i < count; i++) {
		if (!parts[i].id_length || parts[i].id_length > 6 || parts[i].reserved ||
		    parts[i].capacity < 0xa000 || parts[i].capacity > 0x1000000 ||
		    !parts[i].id[0] || parts[i].id[0] == 0xff) { return -22; }
	}
	return 0;
}
int ar_flash_spi_identify(struct ar_flash_spi *s, const struct ar_flash_part *parts,
	size_t count, uint8_t *scratch, size_t length, struct ar_flash_identity *identity)
{
	if (!s || !scratch || length < AR_SFDP_SCRATCH_BYTES || !identity ||
	    ar_flash_parts_validate(parts, count)) { return -22; }
	s->capacity = 0;
	struct ar_flash_identity found = {.part_index=UINT32_MAX};
	int result = ar_flash_spi_read_id(s, found.id);
	if (result) { return result; }
	/* No capacity is inferred from a JEDEC byte. Floating/absent responses
	 * cannot identify a flash device even if stale SFDP bytes look plausible. */
	if (!found.id[0] || found.id[0] == 0xff) { return -19; }
	for (size_t i = 0; i < count; i++) {
		if (!memcmp(found.id, parts[i].id, parts[i].id_length)) {
			found.capacity = parts[i].capacity; found.part_index = i; found.source = AR_FLASH_ID_TABLE;
			s->capacity = found.capacity; *identity = found; return 0;
		}
	}
	result = ar_sfdp_discover(read_sfdp, s, scratch, length, &found.sfdp);
	if (result) { return result; }
	found.capacity = found.sfdp.readable_bytes; found.source = AR_FLASH_ID_SFDP;
	s->capacity = found.capacity; *identity = found;
	return 0;
}
int ar_flash_spi_read(void *context, uint32_t offset, uint8_t *out, size_t length)
{
	struct ar_flash_spi *s = context;
	if (s && !s->capacity) { return s->error ? s->error : -11; }
	if (!s || offset > 0xffffff || offset >= s->capacity ||
	    length > s->capacity - offset) { return -22; }
	return transfer(s, 3, 3, 0, offset, out, length);
}
