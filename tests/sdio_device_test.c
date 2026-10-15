/* SPDX-License-Identifier: MIT */
/* Simulated register contract tests; not a hardware-boot claim. */
#include "ar_sdio_device.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct { uint32_t address, value; } registers[128];
static unsigned used;
static uint8_t submitted[16][AR_SDIO_BUFFER_SIZE];
static size_t submitted_lengths[16];
static unsigned submissions;
static uint32_t *reg(uint32_t address)
{
	for (unsigned i = 0; i < used; i++) {
		if (registers[i].address == address) { return &registers[i].value; }
	}
	assert(used < 128);
	registers[used].address = address;
	return &registers[used++].value;
}
uint32_t ar_test_read32(uint32_t address) { return *reg(address); }
void ar_test_write32(uint32_t address, uint32_t value) { *reg(address) = value; }
void ar_test_write8(uint32_t address, uint8_t value) { *reg(address) = value; }
void ar_test_submit(const uint8_t *data, size_t length)
{
	assert(submissions < 16 && length <= AR_SDIO_BUFFER_SIZE);
	memcpy(submitted[submissions], data, length);
	submitted_lengths[submissions++] = length;
}

int main(void)
{
	*reg(0x0a10a038) = 0xabcd5678;
	*reg(0x0a10a03c) = 0xabcdef00;
	ar_sdio_init();
	assert(*reg(0x0a10a038) == 0x11115678);
	assert(*reg(0x0a10a03c) == 0xabcdef11);
	assert((*reg(0x0a102004) & ((1u << 16) | (1u << 25))) == ((1u << 16) | (1u << 25)));
	assert(*reg(0x04200004) == 0x23f232);
	assert(*reg(0x0420013c) == 0x1fff);
	const uint8_t *received = NULL;
	assert(ar_sdio_receive(&received) == 0 && *reg(0x04200160) == 0);
	*reg(0x04200134) = 0x800;
	/* IRQ enable precedes the host mask write. A grant here would be lost. */
	assert(ar_sdio_receive(&received) == 0 && *reg(0x04200160) == 0);
	assert(*reg(0x04200134) == 0);
	*reg(0x04200168) = 0xf0;
	assert(ar_sdio_receive(&received) == 0 && *reg(0x04200160) == 9);
	*reg(0x04200130) = 19;
	*reg(0x04200134) = 1;
	assert(ar_sdio_receive(&received) == 19 && received != NULL);
	assert(((uintptr_t)received & 511) == 0);
	uint8_t hello[27] = {0xaa};
	*reg(0xe000bff8) = 0xfffffff0;
	assert(ar_sdio_send(hello, sizeof hello) == 0);
	assert(*reg(0x0420015c) == 1 && (*reg(0x0420012c) & 0x1fffff) == 512);
	*reg(0xe000bff8) = 0x90;
	assert(ar_sdio_active_ticks() == 160); /* Timer wrap during ownership. */
	*reg(0x04200134) = 2;
	assert(ar_sdio_receive(&received) == 0);
	assert(!ar_sdio_active_ticks());
	assert(ar_sdio_statistics()->tx_finished == 1);
	assert(ar_sdio_statistics()->tx_last_ticks == 160 && ar_sdio_statistics()->tx_max_ticks == 160);
	assert(ar_sdio_statistics()->tx_wire_bytes == 512);
	static uint8_t maximum[4115];
	assert(ar_sdio_send(maximum, sizeof maximum) == 0);
	assert(*reg(0x0420015c) == 9 && (*reg(0x0420012c) & 0x1fffff) == 4608);
	*reg(0x04200134) = 2;
	ar_sdio_receive(&received);
	*reg(0x04200130) = 7000;
	*reg(0x04200134) = 1;
	assert(ar_sdio_receive(&received) == 0);
	assert(ar_sdio_statistics()->invalid_rx == 1);
	*reg(0x04200134) = 8;
	ar_sdio_receive(&received);
	assert(ar_sdio_statistics()->errors == 1);
	/* Caller reuse cannot mutate a queued packet, and completion preserves FIFO. */
	unsigned start = submissions;
	uint8_t chunk[777];
	for (unsigned packet = 0; packet < AR_SDIO_TX_DEPTH; packet++) {
		memset(chunk, 0x31 + packet, sizeof chunk);
		assert(ar_sdio_send(chunk, sizeof chunk) == 0);
		if (packet == 0) {
			*reg(0x04200130) = 19;
			*reg(0x04200134) = 1;
			assert(ar_sdio_receive(&received) == 19); /* RX is valid while TX owns a buffer. */
		}
	}
	assert(submissions == start + 1);
	memset(chunk, 0xfe, sizeof chunk);
	assert(ar_sdio_send(chunk, sizeof chunk) < 0);
	assert(ar_sdio_statistics()->tx_backpressure == 1);
	assert(ar_sdio_statistics()->tx_queue_high_water == AR_SDIO_TX_DEPTH);
	for (unsigned packet = 0; packet < AR_SDIO_TX_DEPTH; packet++) {
		assert(submitted_lengths[start + packet] == 1024);
		for (unsigned byte = 0; byte < 1024; byte++) {
			assert(submitted[start + packet][byte] == (byte < sizeof chunk ? 0x31 + packet : 0));
		}
		*reg(0x04200134) = 2;
		ar_sdio_receive(&received);
	}
	assert(submissions == start + AR_SDIO_TX_DEPTH);
	assert(ar_sdio_send(chunk, sizeof chunk) == 0); /* Ring reuse after draining. */
	assert(ar_sdio_send(hello, sizeof hello) == 0);
	*reg(0x04200134) = 4; /* Abort releases only the active entry, then submits the next. */
	ar_sdio_receive(&received);
	assert(ar_sdio_statistics()->errors == 2);
	assert(submitted_lengths[submissions - 1] == 512);
	assert(submitted[submissions - 1][0] == 0xaa);
	puts("simulated SDIO register-contract tests passed");
	return 0;
}
