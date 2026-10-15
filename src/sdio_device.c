/* SPDX-License-Identifier: MIT */
/* Polling diagnostic endpoint, independently implemented from observed 18.21.10
 * register/descriptor contracts. Packet bring-up validated on Ascent VRX; RF,
 * queued ownership and the full interrupt/cache HAL remain incomplete. */
#include "ar_sdio_device.h"
#include <string.h>

#define SDIO_BASE UINT32_C(0x04200000)
#define GPIO_BASE UINT32_C(0x0a10a000)
#define RESET_REG UINT32_C(0x0a102004)
#define RESET_BITS ((UINT32_C(1) << 16) | (UINT32_C(1) << 25))
#define CFG 0x004u
#define IRQ_CFG 0x008u
#define CIS_COMMON 0x00cu
#define CIS_F1 0x010u
#define DMA_FEATURES 0x048u
#define TX_ADDR 0x124u
#define RX_ADDR 0x128u
#define TX_LENGTH 0x12cu
#define RX_LENGTH 0x130u
#define EVENTS 0x134u
#define EVENTS_ENABLE 0x138u
#define F1_ENABLE 0x13cu
#define F1_MODE 0x140u
#define TX_BLOCKS 0x15cu
#define RX_BLOCKS 0x160u
#define MAILBOX_EVENTS 0x164u
#define PENDING 0x168u
#ifndef AR_SDIO_DIAGNOSTIC_ID_LOW
#define AR_SDIO_DIAGNOSTIC_ID_LOW 0x31
#endif

static uint8_t common_cis[128] __attribute__((aligned(128)));
static uint8_t progress_cis[128] __attribute__((aligned(128)));
static unsigned progress_bank;
static uint8_t function_cis[128] __attribute__((aligned(128)));
static uint8_t receive_buffer[AR_SDIO_BUFFER_SIZE] __attribute__((aligned(512)));
static uint8_t transmit_buffers[AR_SDIO_TX_DEPTH][AR_SDIO_BUFFER_SIZE] __attribute__((aligned(512)));
static size_t transmit_lengths[AR_SDIO_TX_DEPTH];
static unsigned tx_head, tx_count;
static struct ar_sdio_stats stats;
static unsigned ready, rx_armed, tx_pending;
#ifdef AR_SDIO_CIS_DIAGNOSTICS
static uint32_t observed_events;
#endif

#ifdef AR_SDIO_SIMULATED
extern uint32_t ar_test_read32(uint32_t address);
extern void ar_test_write32(uint32_t address, uint32_t value);
extern void ar_test_write8(uint32_t address, uint8_t value);
extern void ar_test_submit(const uint8_t *data, size_t length);
#define read32 ar_test_read32
#define write32 ar_test_write32
#define write8 ar_test_write8
static void barrier(void) { }
static void short_delay(void) { }
#else
static uint32_t read32(uint32_t address) { return *(volatile uint32_t *)(uintptr_t)address; }
static void write32(uint32_t address, uint32_t value) { *(volatile uint32_t *)(uintptr_t)address = value; }
#ifndef AR_SDIO_WORD_CONTROL
static void write8(uint32_t address, uint8_t value) { *(volatile uint8_t *)(uintptr_t)address = value; }
#endif
static void barrier(void) { __asm__ volatile("fence rw,rw" ::: "memory"); }
static void short_delay(void)
{
	/* Stock microsecond delay uses 40 machine-timer ticks. Bound the wait so a
	 * stalled timer cannot prevent reaching the diagnostic polling loop. */
	uint32_t start = read32(UINT32_C(0xe000bff8));
	for (unsigned i = 0; i < 100000; i++) {
		if ((uint32_t)(read32(UINT32_C(0xe000bff8)) - start) >= 40) { break; }
	}
}
#endif

static uint32_t pointer(const void *data) { return (uint32_t)(uintptr_t)data; }
static void update(uint32_t address, uint32_t clear, uint32_t set)
{
	write32(address, (read32(address) & ~clear) | set);
}
static void control_byte(uint32_t address, uint8_t value)
{
#ifdef AR_SDIO_WORD_CONTROL
	/* Word-access diagnostic variant: preserve non-count bits while avoiding
	 * dependence on the reset-time memory-attribute subword-access policy. */
	update(address, 0xff, value);
#else
	/* The stock grant stores are byte accesses. Their bus strobes can differ
	 * from a word RMW even when the register reads back the same low byte. */
	write8(address, value);
#endif
}

static void arm_receive(void)
{
	if (!ready || rx_armed || tx_count == AR_SDIO_TX_DEPTH) { return; }
	barrier();
	write32(SDIO_BASE + RX_ADDR, pointer(receive_buffer));
	control_byte(SDIO_BASE + RX_BLOCKS, AR_SDIO_BUFFER_SIZE / AR_SDIO_BLOCK);
	barrier();
	rx_armed = 1;
}

static void start_transmit(void)
{
	if (!ready || tx_pending || !tx_count) { return; }
	barrier();
	write32(SDIO_BASE + TX_ADDR, pointer(transmit_buffers[tx_head]));
	update(SDIO_BASE + TX_LENGTH, 0x1fffff, (uint32_t)transmit_lengths[tx_head]);
	control_byte(SDIO_BASE + TX_BLOCKS, (uint8_t)(transmit_lengths[tx_head] >> 9));
	barrier();
	tx_pending = 1;
	stats.tx_packets++;
#ifdef AR_SDIO_SIMULATED
	ar_test_submit(transmit_buffers[tx_head], transmit_lengths[tx_head]);
#endif
}

static void finish_transmit(void)
{
	if (!tx_pending) { return; }
	tx_pending = 0;
	tx_head = (tx_head + 1) % AR_SDIO_TX_DEPTH;
	tx_count--;
}

void ar_sdio_init(void)
{
	memset(&stats, 0, sizeof stats);
	ready = rx_armed = tx_pending = 0;
	tx_head = tx_count = 0;
#ifdef AR_SDIO_STOCK_MEMORY_ATTRIBUTES
	/* Observed 18.21.10 CPU attribute table; experimental, board-specific. */
	static const uint32_t attributes[16] = {
		0x200, 0x10, 0x300, 0xc, 0x10000, 0x10, 0x14000, 0x10,
		0xf0000, 0x10, 0xfffff, 0x10, 0xfffff, 0x10, 0xfffff, 0x10
	};
	for (unsigned i = 0; i < 16; i++) {
		write32(UINT32_C(0xeffff000) + i * 4, attributes[i]);
	}
	barrier();
#endif
	update(RESET_REG, RESET_BITS, 0);
	short_delay();
	/* The identified D4..D9 selector rows all map to mux value 1. */
	update(GPIO_BASE + 0x38, 0xffff0000, 0x11110000);
	update(GPIO_BASE + 0x3c, 0x000000ff, 0x00000011);
	for (unsigned pin = 4; pin < 10; pin++) {
		uint32_t address = GPIO_BASE + (pin < 8 ? 0x20 : 0x24);
		unsigned shift = (pin < 8 ? pin - 4 : pin - 8) * 8;
		uint32_t previous = read32(address);
		uint32_t value = pin == 4 ? 1 : 0x11;
		value |= (previous >> shift) & 0xcc;
		write32(address, (previous & ~(0x3fu << shift)) | (value << shift));
	}
	/* Bank-D enable is active-low in the observed GPIO helper. */
	update(GPIO_BASE, 4, 0);
	short_delay();
	write32(UINT32_C(0x0a106084), 0);
	update(RESET_REG, 0, RESET_BITS);
	short_delay();

	memset(common_cis, 0xff, sizeof common_cis);
	common_cis[0] = 0x20; common_cis[1] = 4;
	common_cis[2] = 0x52; common_cis[3] = 0x41; /* vendor 4152 */
	common_cis[4] = AR_SDIO_DIAGNOSTIC_ID_LOW; common_cis[5] = 0x80;
#ifdef CONFIG_SOC_AR8030_BRINGUP
	common_cis[6] = 0x80; common_cis[7] = 40;
	memset(common_cis + 8, 0, 40);
	memcpy(common_cis + 8, "LIVE", 4);
#endif
#ifdef AR_SDIO_CIS_DIAGNOSTICS
	/* Private vendor tuple for reading CPU-side observations through CMD52.
	 * This is an instrumentation channel, not application packet exchange. */
	common_cis[6] = 0x80; common_cis[7] = 40;
	memset(common_cis + 8, 0, 40);
	memcpy(common_cis + 8, "SDIO", 4);
	observed_events = 0;
#endif
	memset(function_cis, 0xff, sizeof function_cis);
	function_cis[0] = 0x22; function_cis[1] = 42; function_cis[2] = 1;
	function_cis[14] = 0; function_cis[15] = 2; /* 512-byte blocks */
	function_cis[30] = 100; function_cis[31] = 0;
	barrier();
	write32(SDIO_BASE + CFG, 0);
	update(SDIO_BASE + DMA_FEATURES, 0, 7);
	write32(SDIO_BASE + CIS_COMMON, pointer(common_cis));
	write32(SDIO_BASE + CIS_F1, pointer(function_cis));
	write32(SDIO_BASE + RX_ADDR, 0);
	write32(SDIO_BASE + F1_ENABLE, 0x1fff);
	write32(SDIO_BASE + MAILBOX_EVENTS, 0xf0);
	write32(SDIO_BASE + F1_MODE, 1);
	write32(SDIO_BASE + IRQ_CFG, 0x17);
	update(SDIO_BASE + EVENTS_ENABLE, 0, 0x100fff);
	barrier();
	/* Publish the enabled function only after its event mask, CIS and DMA
	 * control are ready. The short CIS allows the host to enable IRQs quickly. */
	write32(SDIO_BASE + CFG, 0x232 | (0x3fu << 12) | (1u << 21));
	barrier();
	/* The stock host first enables IRQs, then writes 0xf0 to its event mask.
	 * Publishing a grant between those operations loses its notification. */
	control_byte(SDIO_BASE + 0x154, 0xa1);
#ifdef AR_SDIO_FINAL_DIAGNOSTIC_ID_LOW
	common_cis[4] = AR_SDIO_FINAL_DIAGNOSTIC_ID_LOW;
	barrier();
	write32(SDIO_BASE + CIS_COMMON, pointer(common_cis));
#endif
}

size_t ar_sdio_receive(const uint8_t **data)
{
#ifdef AR_SDIO_POLL_PAUSE
	/* Experimental alternative to the slower instrumented polling loop. */
	for (unsigned i = 0; i < 40; i++) { short_delay(); }
#endif
	/* Synchronize each polling iteration, including iterations with no new
	 * descriptor. The instrumented successful trial fenced on every poll. */
	barrier();
	stats.polls++;
	/* Diagnostic liveness marker, readable through the physical function-1
	 * mailbox. It is deliberately distinct from the stock protocol payload. */
	if (stats.polls == 1) { control_byte(SDIO_BASE + 0x154, 0xa5); }
#ifdef AR_SDIO_POLL_DIAGNOSTIC_ID
	if (stats.polls == 1) {
		common_cis[4] = 0x50 | (read32(SDIO_BASE + RX_BLOCKS) & 0x0f);
		barrier();
		write32(SDIO_BASE + CIS_COMMON, pointer(common_cis));
	}
#endif
	uint32_t events = read32(SDIO_BASE + EVENTS);
	uint32_t pending = read32(SDIO_BASE + PENDING);
#ifdef AR_SDIO_CIS_DIAGNOSTICS
	observed_events |= events;
	uint32_t snapshot[9] = { stats.polls, observed_events,
		read32(SDIO_BASE + RX_BLOCKS), read32(SDIO_BASE + TX_BLOCKS),
		read32(SDIO_BASE + PENDING), read32(SDIO_BASE + CFG),
		read32(SDIO_BASE + F1_MODE), read32(SDIO_BASE + EVENTS_ENABLE),
		stats.host_enabled };
	memcpy(common_cis + 12, snapshot, sizeof snapshot);
	barrier();
#endif
	if (events & 0x800) {
		ready = 0; rx_armed = 0; stats.host_enabled++;
	}
	if (!ready && stats.host_enabled && (pending & 0xf0) == 0xf0) {
		ready = 1; rx_armed = 0;
	}
	if (events & (2 | 4)) { finish_transmit(); }
	if (events & (4 | 8 | 16)) {
		stats.errors++;
		if (events & (8 | 16)) { rx_armed = 0; }
	}
	if (events) { update(SDIO_BASE + EVENTS, events, 0); }
	/* Clear the previous completion before publishing the next descriptor. */
	start_transmit();
	if (events & 1) {
		update(SDIO_BASE + CFG, 1u << 29, 0);
		rx_armed = 0;
		barrier();
		size_t length = read32(SDIO_BASE + RX_LENGTH) & 0xffff;
		if (!(events & (8 | 16)) && length && length <= sizeof receive_buffer &&
		    read32(SDIO_BASE + RX_ADDR) == pointer(receive_buffer)) {
			*data = receive_buffer;
			stats.rx_packets++;
			return length;
		}
		stats.invalid_rx++;
	}
	if (rx_armed && read32(SDIO_BASE + RX_BLOCKS) == 0 && !(pending & 8)) {
		rx_armed = 0;
	}
	arm_receive();
	return 0;
}

unsigned ar_sdio_tx_room(void) { return ready ? AR_SDIO_TX_DEPTH - tx_count : 0; }

int ar_sdio_send(const uint8_t *data, size_t length)
{
	if (!ready || !length || length > AR_SDIO_BUFFER_SIZE) { return -1; }
	if (tx_count == AR_SDIO_TX_DEPTH) { stats.tx_backpressure++; return -1; }
	size_t transfer = (length + AR_SDIO_BLOCK - 1) & ~(AR_SDIO_BLOCK - 1);
	unsigned tail = (tx_head + tx_count) % AR_SDIO_TX_DEPTH;
	memcpy(transmit_buffers[tail], data, length);
	memset(transmit_buffers[tail] + length, 0, transfer - length);
	transmit_lengths[tail] = transfer;
	tx_count++;
	if (tx_count > stats.tx_queue_high_water) { stats.tx_queue_high_water = tx_count; }
	start_transmit();
	arm_receive();
	return 0;
}

const struct ar_sdio_stats *ar_sdio_statistics(void) { return &stats; }

void ar_sdio_record_fault(uint32_t reason, uint32_t cause, uint32_t pc, uint32_t value)
{
	common_cis[6] = 0x80; common_cis[7] = 40;
	memcpy(common_cis + 8, "FAIL", 4);
	uint32_t fields[4] = {reason, cause, pc, value};
	memcpy(common_cis + 12, fields, sizeof fields);
	barrier();
	write32(SDIO_BASE + CIS_COMMON, pointer(common_cis));
}

void ar_sdio_record_progress(uint32_t stage, uint32_t a, uint32_t b, uint32_t c)
{
	memcpy(common_cis + 8, "PROG", 4);
	uint32_t fields[4] = {stage, a, b, c};
	memcpy(common_cis + 12, fields, sizeof fields);
	memcpy(progress_cis, common_cis, sizeof progress_cis);
	barrier();
	progress_bank ^= 1;
	write32(SDIO_BASE + CIS_COMMON, pointer(progress_bank ? progress_cis : common_cis));
}
