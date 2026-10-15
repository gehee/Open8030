/* SPDX-License-Identifier: MIT */
/* Synthetic peripheral behavior: real RF acceptance still requires hardware. */
#include "ar_rf_device.h"
#include "ar_mac.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct { uint32_t address, value; } registers[1024];
static unsigned used, writes;
static unsigned compensation_writes;
static uint64_t ticks;
static unsigned emulate_otp;
static uint32_t otp_words[64];
static unsigned tear_timer_read;
static unsigned inject_receive, injected, inject_command, inject_header;
static unsigned check_header_bank, header_bank_reads, header_bank0_reads;
static unsigned inject_valid;
static unsigned delay_dma;
static unsigned complete_dma_reads;
static unsigned measurement_banks;
static unsigned trace_register_reads, volatile_register_reads, register_reads;
static uint32_t register_read_addresses[256];
static uint8_t valid_packet[256];
static unsigned forwarded_count, forwarded_slot, forwarded_port;
static size_t forwarded_length;
static uint8_t forwarded[256];
static unsigned event_count, event_ids[32];
static uint8_t event_data[32][4];
static void receive_event(unsigned id, const uint8_t *data, size_t length, void *context)
{
	assert(context == event_data && id < 2 && length == (id ? 4u : 3u) && event_count < 32);
	event_ids[event_count] = id;
	memcpy(event_data[event_count++], data, length);
}
static void receive_payload(unsigned slot, unsigned port, const uint8_t *data, size_t length, void *context)
{
	assert(context == forwarded && length <= sizeof forwarded);
	forwarded_count++; forwarded_slot = slot; forwarded_port = port; forwarded_length = length;
	memcpy(forwarded, data, length);
}
static unsigned transmit_calls, transmit_commits;
static size_t transmit_remaining;
static uint8_t transmit_source[140];
static size_t transmit_peek(uint8_t *scratch, size_t capacity, unsigned *port, void *context)
{
	assert(context == transmit_source); transmit_calls++;
	size_t count = transmit_remaining < capacity ? transmit_remaining : capacity;
	memcpy(scratch, transmit_source + sizeof transmit_source - transmit_remaining, count);
	*port = 3;
	return count;
}
static void transmit_commit(unsigned port, size_t length, void *context)
{
	assert(port == 3 && context == transmit_source && length <= transmit_remaining);
	transmit_commits++; transmit_remaining -= length;
}
static void put32(uint8_t *data, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++) { data[i] = value >> (i * 8); }
}
static uint32_t *reg(uint32_t address)
{
	for (unsigned i = 0; i < used; i++) if (registers[i].address == address) return &registers[i].value;
	assert(used < 1024);
	registers[used].address = address;
	return &registers[used++].value;
}
uint32_t ar_rf_test_read(uint32_t address)
{
	if (trace_register_reads && address >= 0x0a110000 && address < 0x0a111000) {
		assert(register_reads < 256);
		register_read_addresses[register_reads++] = address;
		if (volatile_register_reads) { return *reg(address) ^ (register_reads * 0x01010101u); }
	}
	/* Model a DMA that finishes only while its RX producer is not reset.
	 * This distinguishes drain-before-reset from the old shutdown order. */
	if (address == 0xe0801164 && complete_dma_reads && !(*reg(0x0a11080c)&4)) {
		if (!--complete_dma_reads) { *reg(address) |= 1; }
	}
	if (measurement_banks && (address == 0x0a110e30 || address == 0x0a110e34)) {
		if (*reg(0x0a110808) & (1u << 27)) {
			return address == 0x0a110e30 ? 0x1234aaaa : 0xbbbb5678;
		}
		return address == 0x0a110e30 ? 0x07051111 : 0x22220309;
	}
	if (check_header_bank && ((address >= 0x0a110f04 && address <= 0x0a110f10) ||
	    (address >= 0x0a110f34 && address <= 0x0a110f3c))) {
		if (!(*reg(0x0a110bb0) & 0x80)) {
			header_bank0_reads++;
			return *reg(address) ^ 0xffffffff;
		}
		header_bank_reads++;
	}
	if (address == 0xe000bffc) {
		if (tear_timer_read) { ticks = (ticks | UINT32_MAX) - 20; tear_timer_read = 0; }
		return ticks >> 32;
	}
	if (address == 0xe000bff8) { ticks += 40; return (uint32_t)ticks; }
	if (address == 0xe0801170 && inject_receive && !injected && ar_rf_information()->phase == AR_RF_RECEIVING) {
		*reg(address) |= 1; injected = 1;
	}
	if (address == 0xe080114c && inject_command && ar_rf_information()->phase == AR_RF_RECEIVING) {
		*reg(address) |= 1; inject_command = 0;
	}
	if (address == 0xe0801168 && inject_header && ar_rf_information()->phase == AR_RF_RECEIVING) {
		*reg(address) |= 1; inject_header = 0;
	}
	return *reg(address);
}
void ar_rf_test_write(uint32_t address, uint32_t value)
{
	if (address == 0x0a110480 || address == 0x0a11048c ||
	    address == 0x0a1104a0 || address == 0x0a1104a4 || address == 0x0a1104a8) {
		compensation_writes++;
	}
	/* Reject an accidentally enlarged encoded BB offset, rather than treating
	 * unmapped MMIO as a harmless synthetic register. */
	assert((address >= 0x0a110000 && address < 0x0a111000) ||
	       (address >= 0x0a104000 && address < 0x0a104204) ||
	       address == 0x0a1060c8 ||
	       (address >= 0x0a10a02c && address < 0x0a10a034) || address == 0x0a090140 ||
	       (address >= 0x00140000 && address < 0x00140300) ||
	       address == 0xe0800008 || (address >= 0xe0801120 && address < 0xe0801180));
	*reg(address) = value; writes++;
	if (emulate_otp && address == 0x00140098) {
		assert(value < 64); *reg(0x0014009c) = otp_words[value]; *reg(0x001400a4) = 4;
	}
}
void ar_rf_test_start_dma(uint8_t *buffer, unsigned length)
{
	for (unsigned i = 0; i < length; i++) buffer[i] = 0x58 + i;
	if (inject_valid) { assert(length <= sizeof valid_packet); memcpy(buffer, valid_packet, length); }
	if (!delay_dma) { *reg(0xe0801164) |= 1; } /* RX DMA completion: source 13, CLIC vector 89. */
}
static void assert_channel_plls(uint32_t frequency)
{
	const struct ar_command_program *commands = ar_rf_command_information();
	assert(commands->count == 3 && commands->cursor == -1);
	const unsigned users[] = {8,11,0};
	for (unsigned i = 0; i < 3; i++) {
		assert(commands->entries[i].frequency == frequency);
		assert((commands->entries[i].bits >> 13 & 3) == 2);
		uint32_t profile = *reg(0x0a110000 + (i == 2 ? 0xb00 : 0x9a0 + users[i] * 8));
		assert((commands->entries[i].bits >> 10 & 7) == (i == 2 ? profile & 7 : profile >> 8 & 7));
	}
	uint64_t expected; assert(!ar_rf_pll_word(frequency, &expected));
	const unsigned offsets[] = {0x900,0x905,0x90a,0x910,0x915,0x91a,0x920,0x925,
		0x92a,0x930,0x935,0x93a,0x940,0x945,0x94a,0x950,0x955,0x95a,0x960,0x965,0x96a};
	for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
		uint64_t actual = 0;
		for (unsigned b = 0; b < 5; b++) {
			unsigned offset = offsets[i] + b;
			actual = actual << 8 | ((*reg(0x0a110000 + (offset & ~3u)) >> (8 * (offset & 3))) & 255);
		}
		assert(actual == expected);
	}
}
static void receive_sync(uint8_t control[7], unsigned seal)
{
	if (seal) { control[0] = ar_rf_control_checksum(control); }
	uint32_t words[3] = {0};
	for (unsigned i = 0; i < 7; i++) {
		unsigned bit = 28 + i * 8, word = bit / 32, shift = bit % 32;
		words[word] |= (uint32_t)control[i] << shift;
		if (shift > 24) { words[word + 1] |= (uint32_t)control[i] >> (32 - shift); }
	}
	for (unsigned i = 0; i < 3; i++) { *reg(0x0a110f04 + 4 * i) = words[i]; }
	*reg(0x0a110f00) = 0x1096; *reg(0xe0801168) |= 1; ar_rf_poll();
}

int main(void)
{
	assert(ar_rf_host_ap_time() == UINT32_MAX);
	uint8_t register_output[256];
	assert(ar_rf_host_registers(0, 1, register_output) == -11);
	uint8_t host_status[300];
	assert(ar_rf_host_status(0x3ff, NULL) == -22);
	assert(ar_rf_host_status(0x3ff, host_status) == -11);
	*reg(0x0a104080) = 0x80000000;
	*reg(0x001400a4) = 4;
	*reg(0x0014009c) = 0x00010000;
	*reg(0x0a1060c8) = 0x80000000;
	assert(ar_rf_probe() == 0);
	assert(*reg(0x0a1060c8) == 0x80000003);
	assert(ar_rf_information()->phase == AR_RF_PROBED && ar_rf_information()->chip_profile == 1);
	for (unsigned i = 0; i < 64; i++) { *reg(0x0a110100 + i * 4) = 0x03020100 + i * 0x04040404u; }
	unsigned register_writes_before = writes;
	trace_register_reads = 1;
	assert(!ar_rf_host_registers(0x103, 7, register_output) && register_reads == 7);
	for (unsigned i = 0; i < 256; i++) { assert(register_output[i] == (i < 7 ? i + 3 : 0)); }
	for (unsigned i = 0; i < 7; i++) { assert(register_read_addresses[i] == 0x0a110000 + ((0x103 + i) & ~3u)); }
	register_reads = 0; volatile_register_reads = 1;
	assert(!ar_rf_host_registers(0x103, 7, register_output) && register_reads == 7);
	for (unsigned i = 0; i < 7; i++) { assert(register_output[i] == ((i + 3) ^ (i + 1))); }
	register_reads = 0; volatile_register_reads = 0;
	assert(!ar_rf_host_registers(0x100, 256, register_output) && register_reads == 256);
	for (unsigned i = 0; i < 256; i++) { assert(register_output[i] == i); }
	register_reads = 0; *reg(0x0a110ffc) = 0xa5000000;
	assert(!ar_rf_host_registers(4095, 1, register_output) && register_reads == 1 && register_output[0] == 0xa5);
	assert(register_read_addresses[0] == 0x0a110ffc);
	register_reads = 0;
	assert(!ar_rf_host_registers(4096, 0, register_output) && !register_reads);
	for (unsigned i = 0; i < 256; i++) { assert(!register_output[i]); }
	const unsigned bad_windows[][2] = {{4096, 1}, {4095, 2}, {0, 257}, {65535, 65535}, {UINT32_MAX, 1}, {1, UINT32_MAX}};
	memset(register_output, 0xa5, sizeof register_output);
	for (unsigned i = 0; i < sizeof bad_windows / sizeof bad_windows[0]; i++) {
		assert(ar_rf_host_registers(bad_windows[i][0], bad_windows[i][1], register_output) == -6);
		assert(!register_reads);
		for (unsigned j = 0; j < 256; j++) { assert(register_output[j] == 0xa5); }
	}
	assert(ar_rf_host_registers(0, 1, NULL) == -22 && !register_reads);
	assert(writes == register_writes_before); trace_register_reads = 0;
	assert(*reg(0x0a104080) == 0x80000002 && *reg(0x00140098) == 2);
	/* OTP identity takes byte14 and unaligned bytes26..29, not a stored MAC. */
	emulate_otp = 1; otp_words[3] = 0xa5260000; otp_words[6] = 0x1234abcd; otp_words[7] = 0xabcd5678;
	uint8_t hardware_mac[4];
	assert(ar_rf_board_identity(hardware_mac) == 0);
	uint32_t expected_mac = (UINT32_C(0x56781234) << 6) | 0x26;
	for (unsigned i=0;i<4;i++) { assert(hardware_mac[i] == (uint8_t)(expected_mac >> (8*i))); }
	memset(otp_words,0,sizeof otp_words); assert(ar_rf_board_identity(hardware_mac) == -2);
	emulate_otp = 0;

	uint8_t pins[40] = {0};
	for (unsigned i = 0; i < 10; i++) { pins[i * 4] = i / 5; pins[i * 4 + 1] = i % 5; pins[i * 4 + 2] = 3; }
	assert(ar_rf_setup_pins(pins, sizeof pins) == 0);
	uint64_t pll;
	assert(ar_rf_pll_word(5770000, &pll) == 0 && pll == UINT64_C(0x0120800000));
	assert(ar_rf_pll_word(5839000, &pll) == 0 && pll == UINT64_C(0x0123f33333));
	assert(ar_rf_pll_word(2100000, &pll) == 0 && pll == UINT64_C(0x92d2000000));
	assert(ar_rf_pll_word(0, &pll) == -22);
	uint8_t records[704 * 8] = {0};
	for (unsigned i = 0; i < 704; i++) {
		records[i * 8] = i * 4; records[i * 8 + 1] = (i * 4) >> 8;
		records[i * 8 + 4] = 0x5a;
	}
	unsigned before = writes;
	records[6] = 0xfe;
	records[8] = 1; /* Second record is unaligned: reject before writing the first. */
	assert(ar_rf_load_defaults(records, 16) == -22 && writes == before);
	records[8] = 4;
	assert(ar_rf_load_defaults(records, sizeof records) == 0);
	assert(ar_rf_information()->default_writes == 704 && *reg(0x0a110000) == 0xfe005a);
	assert(ar_rf_load_defaults(records, 8) == -22);
	*reg(0x0a1100e0) = 0xff00006f;
	*reg(0x0a110fa8) = 0x300;
	*reg(0x0a110360) = 6;
	*reg(0x0a1104f0) = 0x8483;
	*reg(0x0a1104f4) = 0x868500;
	assert(ar_rf_calibrate() == 0);
	assert(ar_rf_information()->phase == AR_RF_CALIBRATED && ar_rf_information()->calibration_rounds == 3);
	*reg(0x0a110fa0) = 0xf700;
	*reg(0x0a110fa4) = 0xaa991007;
	assert(ar_rf_setup_psram() == 0 && ar_rf_information()->phase == AR_RF_PSRAM);
	uint8_t readback[256];
	memset(readback, 0x5a, sizeof readback);
	assert(ar_rf_read_registers(0xcd0, 4, readback) == 0);
	assert(readback[0] == (*reg(0x0a110cd0) & 255) && readback[4] == 0x5a);
	assert(ar_rf_read_registers(0xcd1, 4, readback) == -22);
	assert(ar_rf_read_registers(0xdfc, 8, readback) == -22);
	assert(ar_rf_read_registers(0, 260, readback) == -22);
	assert(ar_rf_read_registers(0, 0, readback) == -22);
	assert(ar_rf_read_registers(0, 4, NULL) == -22);
	assert(ar_rf_read_registers(UINT32_MAX, 4, readback) == -22);
	assert((*reg(0x0a110cd4) & 0xff0000) == 0x8b0000);
	*reg(0x0a110f88) = 1u << 8;
	*reg(0x0a110f90) = 32;
	*reg(0x0a110bb0) = 0x102080;
	*reg(0x0a110f00) = 0x1080;
	*reg(0x0a110bac) = 0xffffc0c0 | 0x1107;
	*reg(0x0a110bd4) = 0xa5000042 | 0x3ff00;
	before = writes;
	assert(ar_rf_monitor(5770000, 0, 0xcb, 0) == -22 && writes == before);
	assert(ar_rf_monitor(5770000, 2, 0xcb, 39) == -22 && writes == before);
	inject_receive = 1;
	inject_header = 1;
	assert(ar_rf_monitor(5770000, 2, 0xcb, 0) == 0);
	assert(ar_rf_receive_information()->before_monitor_header == 0x1080);
	assert(ar_rf_receive_information()->before_release_header == 0x1080);
	assert(ar_rf_receive_information()->fch_locked == 0); /* Baselines never count as events. */
	assert(ar_rf_information()->phase == AR_RF_RECEIVING);
	assert(ar_rf_read_registers(0, 4, readback) == -22);
	assert((*reg(0x0a110800) & 0x80000) != 0 && (*reg(0x0a110a60) & 16) == 0);
	assert((*reg(0x0a1109e0) & 0xffe7) == 0xc4a5);
	/* Slot-0 RX starts at byte 0x93a; slot-1 starts at 0x940. A uniform
	 * five-byte stride or ignoring the field shift tunes the wrong words. */
	assert((*reg(0x0a110938) & 0xffff0000) == 0x20010000);
	assert((*reg(0x0a11093c) & 0x00ffffff) == 0x80);
	assert(*reg(0x0a110940) == 0x00802001);
	assert(*reg(0x0a110bac) == 0x1107 && *reg(0x0a110bd4) == 0xa5000042);
	assert(*reg(0x0a110910) == 0x00802001);
	assert((*reg(0x0a110914) & 0xffffff00) == 0x80200100);
	assert((*reg(0x0a110b00) & 0xffffff) == 0x086021);
	assert((*reg(0x0a110b20) & 0x40ffffff) == 0x40496032);
	for (unsigned iteration = 0; iteration < 3000 && ar_rf_information()->phase == AR_RF_RECEIVING; iteration++) { ar_rf_poll(); }
	assert(ar_rf_receive_information()->fch_locked == 1 && ar_rf_receive_information()->fch_last == 0x1080);
	assert(*reg(0x0a110bb0) == 0x102080); /* FCH read restores the selector. */
	assert(ar_rf_receive_information()->receive_events == 1);
	assert(ar_rf_receive_information()->dma_completions == 1);
	assert(ar_rf_receive_information()->packets == 1 && ar_rf_receive_information()->bytes == 32);
	assert(ar_rf_receive_information()->first_bytes[0] == 0x58);
	assert(ar_rf_receive_information()->mac_invalid_packets == 1);
	assert(ar_rf_receive_information()->mac_valid_packets == 0);
	assert(ar_rf_receive_information()->dev_control_valid == 0);
	assert(offsetof(struct ar_rf_rx_info, mac_valid_packets) == 525 * 4);
	assert(offsetof(struct ar_rf_rx_info, first_packet_elapsed_us) == 590 * 4);
	assert(offsetof(struct ar_rf_rx_info, admission_attempts) == 593 * 4);
	assert(offsetof(struct ar_rf_rx_info, irq_last_elapsed_us) == 619 * 4);
	assert(sizeof(struct ar_rf_rx_info) == 1021 * 4);
	assert(offsetof(struct ar_rf_rx_info, lock_messages) == 662 * 4);
	assert(offsetof(struct ar_rf_rx_info, first_login_control) == 685 * 4);
	assert(offsetof(struct ar_rf_rx_info, last_login_sync) == 789 * 4);
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert(ar_rf_monitor(5770000, 2, 0xcb, 15) == 0);
	assert(*reg(0x0a110810) == 0x0080133a && *reg(0x0a110814) == 0);
	assert(*reg(0x0a110870) == 0xf00);
	for (unsigned iteration = 0; iteration < 3000 && ar_rf_information()->phase == AR_RF_RECEIVING; iteration++) { ar_rf_poll(); }
	*reg(0x0a110e6c) = 0; /* Prepare/transmit command loads cached compensation. */
	*reg(0x0a11048c) = 0xa5000033;
	inject_command = 1;
	assert(ar_rf_monitor(5770000, 2, 0xcb, 1) == 0);
	for (unsigned iteration = 0; iteration < 3000 && ar_rf_information()->phase == AR_RF_RECEIVING; iteration++) { ar_rf_poll(); }
	assert(ar_rf_receive_information()->command_modes[0] == 1);
	assert(*reg(0x0a11048c) == 0xa50000ff);
	assert((*reg(0x0a1104a0) & 0xffff) == 0x8483);
	assert((*reg(0x0a1104a4) >> 16) == 0x8685);
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	*reg(0x001400a4) = 0;
	*reg(0x0a110cf4) = 0x20200003;
	*reg(0x0a110bf0) = 0x61612121;
	assert(ar_rf_monitor(5770000, 2, 0xcb, 35) == 0);
	assert(*reg(0x0a110cf4) == 0x20000003);
	assert((*reg(0x0a110de0) & 255) == 0 && (*reg(0x0a110da0) & 255) == 0xcb);
	assert((*reg(0x0a110bf0) & 3) == 0 && (*reg(0x0a110bf0) & 0xfffffffc) == 0x41410100);
	assert(*reg(0x0a110810) == 0x183a113a && *reg(0x0a110870) == 0x0f00dd88);
	for (unsigned iteration = 0; iteration < 3000 && ar_rf_information()->phase == AR_RF_RECEIVING; iteration++) { ar_rf_poll(); }
	/* Known six-step frame: three 100 us prepares, 5331 us broadcast RX,
	 * 100 us special-user slot and 3696 us TX. Cross the 32-bit clock wrap. */
	*reg(0x0a110984) = 0x2210009c;
	*reg(0x0a110a00) = 0x168f;
	*reg(0x0a110df0) = 0xaabbcc55;
	ticks = UINT32_MAX - 200000;
	assert(ar_rf_monitor(5770000, 100, 0xcb, 36) == 0);
	assert((*reg(0x0a110df0) & 0x00ffffff) == 0x00bbcc00);
	assert((*reg(0x0a110de0) & 255) == 0 && (*reg(0x0a110da0) & 255) == 0xcb);
	assert(ar_rf_receive_information()->search_frame_us == 9427);
	assert(ar_rf_receive_information()->search_dwell_ms == 11);
	ticks += 10 * 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 0);
	ticks += 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1);
	assert(ar_rf_receive_information()->search_dwell_ms == 20);
	ticks += 19 * 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1);
	ticks += 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 2);
	assert(ar_rf_receive_information()->search_dwell_ms == 11);
	ticks += 50 * 40000; /* Late polling does one restart, not catch-up bursts. */
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 3);
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	ticks += 20 * 40000;
	ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert(ar_rf_receive_information()->search_restarts == 3); /* Deadline wins. */
	assert(ar_rf_receive_information()->monitor_elapsed_us >= 100000);
	assert(ar_rf_monitor(5770000, 100, 0xcb, 36) == 0);
	inject_header = 1;
	ticks += 12 * 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_suspended == 1);
	assert(ar_rf_receive_information()->search_restarts == 0);
	assert(ar_rf_receive_information()->packets == 0); /* Header is not a packet. */
	ticks += 100 * 40000;
	ar_rf_poll();
	/* DEV startup holds TX across initial release, search restarts and
	 * header detection. A header alone must not enable transmission. */
	*reg(0x0a110800) &= ~4u;
	assert(ar_rf_monitor(5770000, 100, 0xcb, 37) == 0);
	assert((*reg(0x0a110800) & 7) == 4);
	ticks += 12 * 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1);
	assert((*reg(0x0a110800) & 7) == 4);
	inject_header = 1;
	ticks += 21 * 40000;
	ar_rf_poll();
	assert(ar_rf_receive_information()->search_suspended == 1);
	assert(ar_rf_receive_information()->search_restarts == 1);
	assert(ar_rf_receive_information()->packets == 0);
	assert((*reg(0x0a110800) & 7) == 4);
	ticks += 100 * 40000;
	ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert((*reg(0x0a110800) & 7) == 5);
	assert(ar_rf_monitor(5770000, 100, 0xcb, 38) == 0);
	assert(*reg(0x0a110810) == 0x1c3a183a && *reg(0x0a110870) == 0x000feedd);
	assert((*reg(0x0a110988) & 0xffff) == 2500 && (*reg(0x0a11098c) >> 28) == 0);
	assert(*reg(0x0a1108a4) == 0 && (*reg(0x0a110800) & 7) == 4);
	static const uint32_t powers[] = {0x44332211, 0, 0xffffff7f, 0x01000000};
	for (unsigned sample = 0; sample < 4; sample++) {
		*reg(0x0a110e70) = powers[sample];
		*reg(0xe0801144) |= 1; /* IRQ5, CLIC vector 81. */
		ar_rf_poll();
	}
	assert(ar_rf_receive_information()->power_samples == 4);
	assert(ar_rf_receive_information()->power_last == 1);
	assert(ar_rf_receive_information()->power_min == 0);
	assert(ar_rf_receive_information()->power_max == 0x7fffffff);
	ticks += 100 * 40000;
	ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert(ar_rf_receive_information()->search_restarts == 0);
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	uint8_t channels[12];
	/* Configured startup bandwidth must survive channel retune/restart;
	 * search, identity and TX hold retain the operation-39 contract. */
	put32(channels, 5770000); put32(channels + 4, 5839000);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 41, channels, 8) == 0);
	assert(((*reg(0x0a1109e0) >> 8) & 7) == 2);
	assert(((*reg(0x0a1109f8) >> 8) & 7) == 4); /* Power-search user unchanged. */
	assert((*reg(0x0a110800) & 7) == 4 && (*reg(0x0a110df0) & 255) == 0);
	assert(ar_rf_receive_information()->search_frame_us == 9427);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->frequency_khz == 5839000);
	assert((*reg(0x0a110a90) & 0xffff) == 5839000 / 2000);
	assert(((*reg(0x0a1109e0) >> 8) & 7) == 2);
	assert(ar_rf_receive_information()->search_restarts == 1);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 42, channels, 8) == 0);
	assert((*reg(0x0a1109e0) & 0xe707) == 0xa204);
	assert((*reg(0x0a110970) >> 16) == 4688);
	assert(ar_rf_receive_information()->search_frame_us == 7096);
	assert(ar_rf_receive_information()->search_dwell_ms == 9);
	ticks += 9 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->frequency_khz == 5839000);
	assert(ar_rf_receive_information()->search_dwell_ms == 9);
	ticks += 9 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->channel_wraps == 1);
	assert(ar_rf_receive_information()->search_dwell_ms == 16);
	assert((*reg(0x0a1109e0) & 0xe707) == 0xa204);
	inject_header = 1; ticks += 16 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_suspended == 1 && ar_rf_receive_information()->search_restarts == 2);
	assert(ar_rf_receive_information()->packets == 0 && (*reg(0x0a110800) & 7) == 4);
	ticks += 100 * 40000; ar_rf_poll();
	uint32_t old_profile = *reg(0x0a1109e0);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 43, channels, 8) == 0);
	assert(*reg(0x0a1109e0) == ((old_profile & 0xffffff) | 0x3c000000));
	/* Source 2 is only diagnostic evidence; every raw user/mode is bounded,
	 * first/last are distinct, selector state survives, and retries continue. */
	assert(offsetof(struct ar_rf_rx_info, source2_users) == 355 * 4);
	uint32_t old_selector = *reg(0x0a110c00);
	for (unsigned user = 0; user < 16; user++) {
		*reg(0x0a110e6c) = 0xa5001234 | user << 16 | (user & 3) << 20;
		*reg(0xe0801128) |= 1; ar_rf_poll();
		assert(ar_rf_receive_information()->source2_users[user] == 1);
		assert(*reg(0x0a110c00) == old_selector);
	}
	assert(ar_rf_receive_information()->irq_counts[2] == 16);
	for (unsigned mode = 0; mode < 4; mode++) {
		assert(ar_rf_receive_information()->source2_modes[mode] == 4);
	}
	assert(ar_rf_receive_information()->source2_command_first == 0xa5001234);
	assert(ar_rf_receive_information()->source2_command_last == 0xa53f1234);
	assert(ar_rf_receive_information()->fch_locked == 0 && ar_rf_receive_information()->receive_events == 0);
	assert(ar_rf_receive_information()->dma_completions == 0 && ar_rf_receive_information()->packets == 0);
	ticks += 9 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1 && (*reg(0x0a1109e0) >> 24) == 0x3c);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	/* Restore startup compensation fields after an operation that applied
	 * calibration. Preserve adjacent fields through the next command/retry. */
	*reg(0x0a110480) = 0x654321ff; *reg(0x0a1104a0) = 0x9876abcd;
	*reg(0x0a1104a4) = 0x1234fedc; *reg(0x0a11048c) = 0x876543ff;
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 44, channels, 8) == 0);
	assert(*reg(0x0a110480) == 0x654321fd && *reg(0x0a1104a0) == 0x98760000);
	assert(*reg(0x0a1104a4) == 0xfedc && *reg(0x0a11048c) == 0x87654333);
	assert((*reg(0x0a1109e0) >> 24) == 0x3c);
	assert(ar_rf_receive_information()->irq_counts[2] == 0 && ar_rf_receive_information()->source2_command_first == 0);
	*reg(0x0a110e6c) = 0x80000; inject_command = 1;
	ticks += 9 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1);
	assert(*reg(0x0a110480) == 0x654321fd && *reg(0x0a1104a0) == 0x98760000);
	assert(*reg(0x0a1104a4) == 0xfedc && *reg(0x0a11048c) == 0x87654333);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	*reg(0x0a110b4c) = 0x765432ff;
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 45, channels, 8) == 0);
	assert(*reg(0x0a110b4c) == 0x7654320f);
	assert((*reg(0x0a110480) & 2) == 0 && (*reg(0x0a1109e0) >> 24) == 0x3c);
	ticks += 9 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1 && *reg(0x0a110b4c) == 0x7654320f);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 46, channels, 8) == 0);
	assert((*reg(0x0a1109e0) & 0xff00e707) == 0x3c00c405);
	assert((*reg(0x0a110970) >> 16) == 8330 && ar_rf_receive_information()->search_frame_us == 9427);
	assert(ar_rf_receive_information()->search_dwell_ms == 11 && (*reg(0x0a110480) & 2) == 0);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1 && (*reg(0x0a1109e0) & 0xff00e707) == 0x3c00c405);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 47, channels, 8) == 0);
	assert((*reg(0x0a110de0) & 255) == 0xcb && (*reg(0x0a110df0) & 255) == 0xcb);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1 && (*reg(0x0a110de0) & 255) == 0xcb);
	assert(offsetof(struct ar_rf_rx_info, header_users) == 377 * 4);
	assert(offsetof(struct ar_rf_rx_info, first_valid_control) == 429 * 4);
	assert(offsetof(struct ar_rf_rx_info, eop_snapshots) == 436 * 4);
	assert(offsetof(struct ar_rf_rx_info, before_monitor_header) == 480 * 4);
	assert(offsetof(struct ar_rf_rx_info, first_selector_old) == 489 * 4);
	static const uint16_t control_offsets[] = {0xf04, 0xf08, 0xf0c, 0xf10, 0xf34, 0xf38, 0xf3c};
	for (unsigned i = 0; i < 7; i++) { *reg(0x0a110000 + control_offsets[i]) = 0x12345670 + i; }
	check_header_bank = 1;
	uint32_t old_header_selector = *reg(0x0a110bb0);
	*reg(0x0a110e6c) = 0x180000;
	for (unsigned user = 0; user < 16; user++) {
		*reg(0x0a110f00) = user << 9 | 0x80;
		inject_header = 1; ar_rf_poll();
		for (unsigned i = 0; i < 7; i++) { *reg(0x0a110000 + control_offsets[i]) = 0xffffffff; }
		*reg(0x0a110f00) = user << 9;
		inject_header = 1; ar_rf_poll();
		*reg(0x0a110fbc) = user << 20 | 0x12345;
		*reg(0xe0801160) |= 1; ar_rf_poll();
		assert(ar_rf_receive_information()->header_users[user] == 2);
		assert(ar_rf_receive_information()->header_valid_users[user] == 1);
		assert(ar_rf_receive_information()->eop_users[user] == 1);
	}
	assert(*reg(0x0a110bb0) == old_header_selector);
	assert(header_bank_reads == 42 && header_bank0_reads == 7);
	for (unsigned i = 0; i < 7; i++) { assert(ar_rf_receive_information()->first_valid_control[i] == 0x12345670 + i); }
	assert(ar_rf_receive_information()->first_selector_old == old_header_selector);
	assert(ar_rf_receive_information()->first_selector_set == (old_header_selector | 0x80));
	assert(ar_rf_receive_information()->first_selector_delayed == (old_header_selector | 0x80));
	assert(ar_rf_receive_information()->first_bank_delay_status == 0);
	for (unsigned i = 0; i < 7; i++) {
		assert(ar_rf_receive_information()->first_control_bank0[i] == ((0x12345670 + i) ^ 0xffffffff));
		assert(ar_rf_receive_information()->first_control_delayed[i] == 0x12345670 + i);
	}
	for (unsigned i = 0; i < 4; i++) {
		const struct ar_rf_eop_snapshot *snapshot = &ar_rf_receive_information()->eop_snapshots[i];
		assert(snapshot->status == (i << 20 | 0x12345));
		assert(snapshot->header == i << 9 && snapshot->valid_indications == i + 1);
		assert(snapshot->elapsed_us >= 11000);
		for (unsigned j = 0; j < 7; j++) { assert(snapshot->control[j] == 0xffffffff); }
	}
	check_header_bank = 0;
	assert(ar_rf_receive_information()->first_valid_header == 0x80);
	assert(ar_rf_receive_information()->first_valid_command == 0x180000);
	assert(ar_rf_receive_information()->first_valid_elapsed_us >= 11000);
	assert(ar_rf_receive_information()->search_suspended == 1 && ar_rf_receive_information()->search_restarts == 1);
	assert(ar_rf_receive_information()->receive_events == 0 && ar_rf_receive_information()->packets == 0);
	ticks += 100 * 40000; ar_rf_poll();
	*reg(0x0a110f00) = 0x1080; /* Restore the shared synthetic header fixture. */
	uint32_t prior_rx8 = *reg(0x0a1109e0);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 48, channels, 8) == 0);
	assert(*reg(0x0a1109e0) == ((prior_rx8 & 0xffffff) | 0x20000000));
	assert((*reg(0x0a110de0) & 255) == 0 && (*reg(0x0a110df0) & 255) == 0);
	assert(ar_rf_receive_information()->search_dwell_ms == 11);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1 && (*reg(0x0a1109e0) >> 24) == 0x20);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 0);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 49, channels, 8) == 0);
	assert(*reg(0x0a110a48) == 0 && (*reg(0x0a1109e0) >> 24) == 0x3c);
	assert((*reg(0x0a110de0) & 255) == 0 && (*reg(0x0a110df0) & 255) == 0);
	assert((*reg(0x0a110800) & 4) == 4 && ar_rf_receive_information()->search_dwell_ms == 11);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1 && *reg(0x0a110a48) == 0);
	*reg(0x0a110f00) = 0x101096;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->mcs_buffer_transitions == 0);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->packets == 0 && ar_rf_receive_information()->fch_locked == 1);
	assert(offsetof(struct ar_rf_rx_info, mcs_buffer_transitions) == 507 * 4);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 50, channels, 8) == 0);
	assert(*reg(0x0a110a48) == 0 && (*reg(0x0a1109e0) >> 24) == 0x3c);
	*reg(0x0a110bf0) = 0xa5a5a5a4;
	*reg(0x0a110d08) = 0x5a5b5a5a;
	/* Invalid status, wrong user, and each nonmatching MCS-key component. */
	static const uint32_t rejected_headers[] = {
		0x101016, 0x100e96, 0x1010a6, 0x103096, 0x101097, 0x1010b6
	};
	for (unsigned i = 0; i < sizeof rejected_headers / sizeof rejected_headers[0]; i++) {
		*reg(0x0a110f00) = rejected_headers[i];
		inject_header = 1; ar_rf_poll();
		assert(ar_rf_receive_information()->mcs_buffer_transitions == 0);
		assert(*reg(0x0a110bf0) == 0xa5a5a5a4 && *reg(0x0a110d08) == 0x5a5b5a5a);
	}
	*reg(0x0a110f00) = 0x101096;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->mcs_buffer_transitions == 1);
	assert(ar_rf_receive_information()->mcs_buffer_header == 0x101096);
	assert(ar_rf_receive_information()->mcs_buffer_bf0_before == 0xa5a5a5a4);
	assert(ar_rf_receive_information()->mcs_buffer_bf0_after == 0xa5a5a5a5);
	assert(ar_rf_receive_information()->mcs_buffer_d08_before == 0x5a5b5a5a);
	assert(ar_rf_receive_information()->mcs_buffer_d08_after == 0x5a5a5a5a);
	assert(*reg(0x0a110bf0) == 0xa5a5a5a5 && *reg(0x0a110d08) == 0x5a5a5a5a);
	/* Repeat eligibility must not reapply or replace the recorded transition. */
	*reg(0x0a110bf0) = 0xa5a5a5a4;
	*reg(0x0a110d08) = 0x5a5b5a5a;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->mcs_buffer_transitions == 1);
	assert(*reg(0x0a110bf0) == 0xa5a5a5a4 && *reg(0x0a110d08) == 0x5a5b5a5a);
	assert(ar_rf_receive_information()->receive_events == 0 && ar_rf_receive_information()->packets == 0);
	ticks += 100 * 40000; ar_rf_poll();
	*reg(0x0a110f00) = 0x1080;
	assert(offsetof(struct ar_rf_rx_info, empty_control_headers) == 513 * 4);
	for (unsigned nonzero_word = 0; nonzero_word < 3; nonzero_word++) {
		assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 51, channels, 8) == 0);
		assert(*reg(0x0a110a48) == 0 && (*reg(0x0a1109e0) >> 24) == 0x3c);
		for (unsigned i = 0; i < 7; i++) { *reg(0x0a110000 + control_offsets[i]) = 0; }
		/* Bits outside the seven extracted bytes must not count as content. */
		*reg(0x0a110f04) = 0x0fffffff;
		*reg(0x0a110f0c) = 0xfff00000;
		*reg(0x0a110f00) = 0x101016;
		inject_header = 1; ar_rf_poll();
		assert(ar_rf_receive_information()->empty_control_headers == 0);
		*reg(0x0a110f00) = 0x101096;
		inject_header = 1; ar_rf_poll();
		assert(ar_rf_receive_information()->empty_control_headers == 1);
		assert(ar_rf_receive_information()->search_suspended == 0);
		ticks += 11 * 40000; ar_rf_poll();
		assert(ar_rf_receive_information()->search_restarts == 1);
		inject_header = 1; ar_rf_poll();
		assert(ar_rf_receive_information()->empty_control_headers == 2);
		ticks += 11 * 40000; ar_rf_poll();
		assert(ar_rf_receive_information()->search_restarts == 2);
		*reg(0x0a110000 + control_offsets[nonzero_word]) |=
			nonzero_word == 0 ? 0x10000000 : nonzero_word == 1 ? 0x80000000 : 0x80000;
		inject_header = 1; ar_rf_poll();
		assert(ar_rf_receive_information()->empty_control_headers == 2);
		assert(ar_rf_receive_information()->fch_locked == 3);
		assert(ar_rf_receive_information()->last_control_header == 0x101096);
		assert(ar_rf_receive_information()->last_control_words[nonzero_word] ==
		       *reg(0x0a110000 + control_offsets[nonzero_word]));
		assert(ar_rf_receive_information()->search_suspended == 1);
		ticks += 21 * 40000; ar_rf_poll();
		assert(ar_rf_receive_information()->search_restarts == 2);
		assert(ar_rf_receive_information()->mcs_buffer_transitions == 0);
		assert(ar_rf_receive_information()->receive_events == 0 && ar_rf_receive_information()->packets == 0);
		ticks += 100 * 40000; ar_rf_poll();
	}
	*reg(0x0a110f00) = 0x1080;
	put32(channels, 5770000); put32(channels + 4, 5839000); put32(channels + 8, 2100000);
	assert(offsetof(struct ar_rf_rx_info, bad_control_headers) == 522 * 4);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 52, channels, 8) == 0);
	for (unsigned i = 0; i < 7; i++) { *reg(0x0a110000 + control_offsets[i]) = 0; }
	*reg(0x0a110f00) = 0x101096;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->last_control_check == 0x1e);
	assert(ar_rf_receive_information()->bad_control_headers == 1);
	assert(ar_rf_receive_information()->search_suspended == 0);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 1);
	/* Trial 88's nonzero but invalid seven-byte control contents. */
	*reg(0x0a110f04) = 0x371610;
	*reg(0x0a110f0c) = 0x4000;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->last_control_check == 0x0e);
	assert(ar_rf_receive_information()->bad_control_headers == 2);
	assert(ar_rf_receive_information()->search_suspended == 0);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 2);
	/* Synthetic matching checksum suspends retries but creates no packet. */
	*reg(0x0a110f04) |= 0xe0000000;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->bad_control_headers == 2);
	assert(ar_rf_receive_information()->matching_control_headers == 1);
	assert(ar_rf_receive_information()->empty_control_headers == 1);
	assert(ar_rf_receive_information()->search_suspended == 1);
	ticks += 21 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_restarts == 2);
	assert(ar_rf_receive_information()->receive_events == 0 && ar_rf_receive_information()->packets == 0);
	ticks += 100 * 40000; ar_rf_poll();
	*reg(0x0a110f00) = 0x1080;
	before = writes;
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 54, channels, 8) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 10001, 0xcb, 53, channels, 8) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 1501, 0xcb, 52, channels, 8) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 0, 0xcb, 53, channels, 8) == -22 && writes == before);
	/* Longer configured-profile search crosses timer wrap and must survive
	 * the historical 1.5-second boundary. Accepted stock cache bytes from
	 * trial 92 pass the same check without synthesizing a received packet. */
	ticks = UINT32_MAX - 40000000;
	assert(ar_rf_monitor_plan(5770000, 10000, 0xcb, 53, channels, 4) == 0);
	assert((*reg(0x0a1109e0) & 0xff00e707) == 0x3c00a204);
	assert((*reg(0x0a110970) >> 16) == 0x1250);
	assert(*reg(0x0a110a48) == 0x80808080);
	assert(ar_rf_receive_information()->search_frame_us == 7096);
	assert(ar_rf_receive_information()->search_dwell_ms == 9);
	for (unsigned i = 0; i < 7; i++) { *reg(0x0a110000 + control_offsets[i]) = 0; }
	*reg(0x0a110f00) = 0x101096;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->bad_control_headers == 1);
	ticks += 1500 * 40000; ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_RECEIVING);
	assert(ar_rf_receive_information()->search_restarts == 1);
	assert(ar_rf_receive_information()->search_suspended == 0);
	*reg(0x0a110f0c) = 0xc000;
	inject_header = 1; ar_rf_poll();
	assert(ar_rf_receive_information()->last_control_check == 0);
	assert(ar_rf_receive_information()->matching_control_headers == 1);
	assert(ar_rf_receive_information()->search_suspended == 1);
	assert(ar_rf_receive_information()->packets == 0);
	ticks += 8500 * 40000; ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert(ar_rf_receive_information()->monitor_elapsed_us >= 10000000);
	assert(ar_rf_receive_information()->monitor_elapsed_us < 10000100);
	*reg(0x0a110f00) = 0x1080;
	before = writes;
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 39, channels, sizeof channels) == -22);
	assert(writes == before); /* Validate the last entry before programming any PLL. */
	put32(channels + 8, 5770000);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 39, channels, sizeof channels) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 39, NULL, 4) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 39, channels, 11) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 39, channels, 172) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5839000, 100, 0xcb, 39, channels, 8) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 38, channels, 8) == -22 && writes == before);
	put32(channels + 8, 5740000);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 39, channels, sizeof channels) == 0);
	memset(channels, 0, sizeof channels); /* Request buffer may be reused immediately. */
	assert(ar_rf_receive_information()->channel_count == 3 && ar_rf_receive_information()->channels[0].visits == 1);
	assert(ar_rf_receive_information()->channels[1].frequency_khz == 5839000);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->channel_index == 1 && ar_rf_receive_information()->frequency_khz == 5839000);
	assert((*reg(0x0a110904) & 0xffffff00) == 0xf3230100 && (*reg(0x0a110908) & 0xffff) == 0x3333);
	assert((*reg(0x0a110a90) & 0xffff) == 5839000 / 2000);
	assert(ar_rf_receive_information()->search_dwell_ms == 11 && ar_rf_receive_information()->channel_wraps == 0);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->channel_index == 2 && ar_rf_receive_information()->channels[2].visits == 1);
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->channel_index == 0 && ar_rf_receive_information()->channel_wraps == 1);
	assert(ar_rf_receive_information()->search_dwell_ms == 20 && ar_rf_receive_information()->channels[0].visits == 2);
	inject_header = 1; ticks += 21 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->search_suspended == 1 && ar_rf_receive_information()->channel_index == 0);
	assert(ar_rf_receive_information()->search_restarts == 3 && ar_rf_receive_information()->packets == 0);
	ticks += 100 * 40000; ar_rf_poll();
	put32(channels, 5770000); put32(channels + 4, 5839000);
	assert(ar_rf_monitor_plan(5770000, 100, 0xcb, 40, channels, 8) == 0);
	*reg(0x0a110e70) = 0x01000000; *reg(0xe0801144) |= 1; ar_rf_poll();
	ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_receive_information()->channel_index == 1);
	assert(ar_rf_receive_information()->channels[0].power_samples == 1 && ar_rf_receive_information()->channels[0].power_min == 1);
	*reg(0x0a110e70) = 0x03000000; *reg(0xe0801144) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->channels[1].power_samples == 1 && ar_rf_receive_information()->channels[1].power_max == 3);
	ticks += 30 * 40000; ar_rf_poll(); /* Delayed polling advances once. */
	assert(ar_rf_receive_information()->channel_index == 0 && ar_rf_receive_information()->channel_wraps == 1);
	assert(ar_rf_receive_information()->search_restarts == 2 && ar_rf_receive_information()->search_dwell_ms == 10);
	uint32_t old_pll = *reg(0x0a11090c);
	*reg(0x0a110fa8) = 0; ticks += 11 * 40000; ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM && ar_rf_receive_information()->search_error == (uint32_t)-110);
	assert(ar_rf_receive_information()->channel_index == 0 && ar_rf_receive_information()->search_restarts == 2);
	assert(*reg(0x0a11090c) == old_pll && (*reg(0x0a110800) & 9) == 9); /* Failed hold never retunes or releases. */
	*reg(0x0a110fa8) = 0x300;
	uint8_t maximum_channels[AR_RF_MAX_CHANNELS * 4];
	for (unsigned i = 0; i < AR_RF_MAX_CHANNELS; i++) { put32(maximum_channels + 4 * i, 4900000 + i * 30000); }
	assert(ar_rf_monitor_plan(4900000, 1500, 0xcb, 39, maximum_channels, sizeof maximum_channels) == 0);
	for (unsigned i = 1; i < AR_RF_MAX_CHANNELS; i++) { ticks += 11 * 40000; ar_rf_poll(); }
	assert(ar_rf_receive_information()->channel_index == 41 && ar_rf_receive_information()->channels[41].visits == 1);
	ticks += 1500 * 40000; ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM && ar_rf_receive_information()->search_restarts == 41);
	assert(ar_rf_monitor(5770000, 100, 0xcb, 36) == 0);
	*reg(0x0a110fa8) = 0; /* Failed periodic reset must stop within its deadline. */
	ticks += 12 * 40000;
	ar_rf_poll();
	assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert(ar_rf_receive_information()->search_error == (uint32_t)-110);
	assert(ar_rf_information()->error == (uint32_t)-110);
	assert(ar_rf_receive_information()->search_restarts == 0);
	assert((*reg(0x0a110800) & 1) != 0);
	/* Operation 54 consumes only validated, configured mode-0 controls. */
	*reg(0x0a110fa8) = 0x300;
	uint8_t dev_plan[28] = {'D','E','V','0',1,2,3,4, 0,0,0,0, 5,6,7,8};
	dev_plan[20] = 1; dev_plan[22] = 2; dev_plan[23] = 2;
	put32(dev_plan + 24, 5770000);
	uint8_t control[37] = {37,1,0,0x40,1,0,0,0,5,6,7,8,1};
	control[28] = 4; control[32] = 1; memcpy(control + 33, dev_plan + 4, 4);
	uint8_t checksum = 0;
	for (unsigned i = 0; i < sizeof control; i++) { checksum = ar_mac_crc8_step(checksum ^ control[i]); }
	control[2] = checksum;
	size_t packet_size;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 0, 0, control, sizeof control, NULL, 0, &packet_size) == 0);
	assert(packet_size == 48);
	unsigned before_writes = writes;
	assert(ar_rf_monitor_plan(5770000, 100, 5, 54, dev_plan, sizeof dev_plan) == -22);
	assert(writes == before_writes); /* Wrong local identity cannot reach MMIO. */
	assert(ar_rf_monitor_plan(5770000, 100, 4, 54, dev_plan, sizeof dev_plan) == 0);
	assert(ar_rf_receive_information()->sync_control_words[0] == 0x1e);
	assert(*reg(0x0a110b8c) == 0x1e && *reg(0x0a110b90) == 0);
	*reg(0x0a110f88) = 1; *reg(0x0a110f90) = 48;
	inject_valid = inject_receive = 1; injected = 0;
	ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->admission_attempts == 1);
	assert(ar_rf_receive_information()->acquisition_ready == 1);
	assert(ar_rf_receive_information()->transition_status == 1);
	assert(ar_rf_receive_information()->assigned_slot == 8);
	assert((*reg(0x0a110800) & 4) == 0 && (*reg(0x0a110bf0) & 3) == 1);
	assert(((*reg(0x0a110b7c) >> 16) & 255) == 12);
	assert((*reg(0x0a110a48)) == 0);
	/* First TX command loads the cached band compensation. Repeated commands
	 * at this fixed frequency must not reapply the first-band handshake. */
	*reg(0x0a110e6c) = 0; *reg(0xe080114c) |= 1; ar_rf_poll();
	assert((*reg(0x0a1104a0) & 0xff0000) == 0xff0000);
	*reg(0x0a1104a0) &= ~0xff0000u;
	*reg(0xe080114c) |= 1; ar_rf_poll();
	assert((*reg(0x0a1104a0) & 0xff0000) == 0);
	*reg(0x0a110f70) = 0; *reg(0x0a110f5c) = 64 << 16; *reg(0x0a110f58) = 0x30;
	*reg(0xe080117c) |= 1; ar_rf_poll(); /* Source 19. */
	assert(ar_rf_receive_information()->tx_packets == 1 && ar_rf_receive_information()->tx_owned == 1);
	assert(ar_rf_receive_information()->last_tx_request == 3);
	struct ar_mac_frame encoded;
	assert(ar_mac_decode(ar_rf_receive_information()->last_tx_packet, 24, &encoded) == 0);
	assert(encoded.outband_length == 16 && encoded.outband[4] == 0x21);
	assert(!memcmp(encoded.outband + 8, dev_plan + 4, 4));
	assert((*reg(0x0a110d40) & 0xff00ffff) == 16);
	*reg(0xe080115c) |= 1; ar_rf_poll(); /* Source 11. */
	assert(ar_rf_receive_information()->tx_end == 1 && ar_rf_receive_information()->tx_owned == 0);
	ticks += 100 * 40000; ar_rf_poll(); assert((*reg(0x0a110800) & 4) != 0);
	/* Unknown peer cannot release TX; a power handshake failure also leaves
	 * it held even when packet admission succeeded. */
	dev_plan[20] = 0;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 54, dev_plan, sizeof dev_plan) == 0);
	injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->admission_rejects == 1);
	assert(!ar_rf_receive_information()->acquisition_ready && (*reg(0x0a110800) & 4));
	ticks += 100 * 40000; ar_rf_poll(); dev_plan[20] = 1;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 54, dev_plan, sizeof dev_plan) == 0);
	*reg(0x0a110fa8) = 0x100; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->acquisition_ready == 1);
	assert(ar_rf_receive_information()->transition_status == (uint32_t)-110);
	assert(!ar_rf_receive_information()->tx_packets && (*reg(0x0a110800) & 4));
	ticks += 100 * 40000; ar_rf_poll(); inject_valid = 0;
	/* Operation 54 observes source 18 without changing its user field. */
	*reg(0x0a110fa8) = 0x300; inject_receive = 0;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 54, dev_plan, sizeof dev_plan) == 0);
	*reg(0x0a110b4c) = 0xa55a00ad; *reg(0x0a110f68) = 0x80;
	*reg(0xe0801178) |= 1; ar_rf_poll();
	assert(*reg(0x0a110b4c) == 0xa55a00ad);
	assert(ar_rf_receive_information()->interleaver_users[8] == 1);
	assert(ar_rf_receive_information()->interleaver_updates == 0);
	ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_monitor_plan(5770000, 100, 4, 55, dev_plan, sizeof dev_plan) == 0);
	assert(ar_rf_receive_information()->search_frame_us == 7096);
	for (unsigned user = 0; user < 16; user++) {
		*reg(0x0a110b4c) = 0xa55a00ad;
		*reg(0x0a110f68) = 0xabc00001 | user << 4;
		*reg(0x0a110f64) = 0x43210000 | user;
		*reg(0xe0801178) |= 1; ar_rf_poll();
		assert(*reg(0x0a110b4c) == (0xa55a0005 | ((user * 2 + 1) << 3)));
		assert(ar_rf_receive_information()->interleaver_users[user] == 1);
	}
	assert(ar_rf_receive_information()->interleaver_updates == 16);
	assert(ar_rf_receive_information()->interleaver_first_f68 == 0xabc00001);
	assert(ar_rf_receive_information()->interleaver_last_f68 == 0xabc000f1);
	assert(ar_rf_receive_information()->interleaver_last_f64 == 0x4321000f);
	assert(ar_rf_receive_information()->irq_last_elapsed_us[18] > 0);
	assert(!ar_rf_receive_information()->acquisition_ready && (*reg(0x0a110800) & 4));
	inject_valid = inject_receive = 1; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->transition_status == 1);
	*reg(0xe080117c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->tx_packets == 1);
	*reg(0xe080115c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->tx_owned == 0);
	ticks += 100 * 40000; ar_rf_poll(); inject_valid = 0;
	assert((*reg(0x0a110800) & 4) != 0);
	/* Stock acquisition uses TX0 2234 us / internal MCS1. Preserve TX1
	 * duration, keep TX held until valid admission, and derive dwell from
	 * the corrected whole frame. This does not simulate over-air acceptance. */
	*reg(0x0a110a00) = 0xa5a5168f; inject_receive = 0;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 56, dev_plan, sizeof dev_plan) == 0);
	assert(*reg(0x0a110a00) == 0xa5a50da3);
	assert((*reg(0x0a110b00) & 0x3f700) == 0x13000);
	assert(ar_rf_receive_information()->search_frame_us == 5634);
	assert(ar_rf_receive_information()->search_dwell_ms == 7);
	assert((*reg(0x0a110800) & 4) && !ar_rf_receive_information()->tx_packets);
	inject_valid = inject_receive = 1; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->transition_status == 1);
	assert((*reg(0x0a110b00) & 0x3f700) == 0x13000);
	*reg(0xe080117c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->tx_packets == 1);
	*reg(0xe080115c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->tx_owned == 0);
	ticks += 100 * 40000; ar_rf_poll(); inject_valid = 0;
	assert((*reg(0x0a110800) & 4) != 0);
	/* First-login evidence survives later ordinary control messages. The
	 * observer must not change report state, assignment or connected MMIO. */
	assert(ar_rf_monitor_plan(5770000, 100, 4, 57, dev_plan, sizeof dev_plan) == 0);
	inject_valid = 1; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->lock_messages == 1);
	assert(ar_rf_receive_information()->lock_results[0] == 1);
	uint8_t login_control[43] = {43,1,0,0x40,1,0,0,0,5,6,7,8,5,9};
	login_control[28] = 1; login_control[32] = 1;
	login_control[33] = 9; login_control[34] = 7;
	memcpy(login_control + 35, dev_plan + 4, 4);
	memcpy(login_control + 39, "\1\2\3\4", 4);
	checksum = 0;
	for (unsigned i = 0; i < sizeof login_control; i++) { checksum = ar_mac_crc8_step(checksum ^ login_control[i]); }
	login_control[2] = checksum;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 1, 0, login_control, sizeof login_control, NULL, 0, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size;
	injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->login_extension_packets == 1);
	assert(ar_rf_receive_information()->login_matching_packets == 1);
	assert(ar_rf_receive_information()->lock_results[2] == 1);
	assert(ar_rf_receive_information()->last_login_slot == 0);
	assert(ar_rf_receive_information()->last_login_token == 9);
	assert(ar_rf_receive_information()->last_login_value == 7);
	assert(ar_rf_receive_information()->last_login_ap_state == 2);
	assert(ar_rf_receive_information()->last_login_sync == 0x04030201);
	assert(ar_rf_receive_information()->first_login_length == sizeof login_control);
	assert(ar_rf_receive_information()->first_matching_length == sizeof login_control);
	assert(!memcmp(ar_rf_receive_information()->first_matching_control, login_control, sizeof login_control));
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 2, 0, control, sizeof control, NULL, 0, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size;
	injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->lock_messages == 3);
	assert(ar_rf_receive_information()->lock_results[0] == 2);
	assert(ar_rf_receive_information()->login_matching_packets == 1);
	assert(!memcmp(ar_rf_receive_information()->first_login_control, login_control, sizeof login_control));
	assert(ar_rf_receive_information()->lock_observed_extensions == 5);
	assert(ar_rf_receive_information()->assigned_slot == 8 && !(*reg(0x0a110cf4) & (1u << 21)));
	*reg(0xe080117c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->last_tx_packet[12] == 0x21);
	ticks += 100 * 40000; ar_rf_poll(); inject_valid = 0;
	assert((*reg(0x0a110800) & 4) != 0);
	/* DEV1 supplies effective TX gains, independently of report status bits.
	 * Reject truncation/analog overflow before any peripheral write. */
	uint8_t gain_plan[32]; memcpy(gain_plan, dev_plan, 24); gain_plan[3] = '1';
	gain_plan[24] = 17; gain_plan[25] = 65; gain_plan[26] = 29; gain_plan[27] = 83;
	put32(gain_plan + 28, 5770000);
	before_writes = writes;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 58, dev_plan, sizeof dev_plan) == -22);
	assert(ar_rf_monitor_plan(5770000, 100, 4, 58, gain_plan, 28) == -22);
	gain_plan[24] = 64;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 58, gain_plan, sizeof gain_plan) == -22);
	gain_plan[24] = 17; gain_plan[26] = 64;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 58, gain_plan, sizeof gain_plan) == -22);
	gain_plan[26] = 29;
	assert(writes == before_writes);
	uint32_t gain_a_other = *reg(0x0a1109a4) & ~0x3fffu;
	uint32_t gain_b_other = *reg(0x0a110da4) & ~63u;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 58, gain_plan, sizeof gain_plan) == 0);
	assert(*reg(0x0a1109a4) == (gain_a_other | 17u << 8 | 65));
	assert(*reg(0x0a110da4) == (gain_b_other | 29));
	assert((*reg(0x0a110da0) >> 24) == 83);
	assert(ar_rf_receive_information()->search_frame_us == 5634);
	assert((*reg(0x0a110800) & 4) != 0);
	/* The caller's calibration buffer can be reused immediately. */
	memset(gain_plan + 24, 0, 4);
	inject_valid = 1; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->transition_status == 1);
	assert((*reg(0x0a1109a4) & 0x3fff) == (17u << 8 | 65));
	*reg(0xe080117c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->last_tx_packet[14] == 0); /* Existing 2/2 raw statuses. */
	ticks += 100 * 40000; ar_rf_poll(); inject_valid = 0;
	/* Apply only supported mode-0 remote sync, retain ACK on a small budget,
	 * emit it once, and stop on a stale assignment. */
	uint8_t connected_plan[36]; memcpy(connected_plan, gain_plan, 28);
	connected_plan[3] = '2'; connected_plan[28] = 25;
	memset(connected_plan + 29, 0, 3); put32(connected_plan + 32, 5770000);
	before_writes = writes; connected_plan[28] = 64;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 59, connected_plan, sizeof connected_plan) == -22);
	connected_plan[28] = 25; connected_plan[31] = 1;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 59, connected_plan, sizeof connected_plan) == -22);
	connected_plan[31] = 0; assert(writes == before_writes);
	connected_plan[3] = '3'; connected_plan[29] = 128;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 61, connected_plan, sizeof connected_plan) == -22);
	assert(writes == before_writes); connected_plan[29] = 6;
	uint8_t mcs_plan[AR_RF_DEV4_PREFIX + 4] = {0};
	memcpy(mcs_plan, connected_plan, 32); mcs_plan[3] = '4';
	mcs_plan[32] = 1; mcs_plan[33] = 5; mcs_plan[34] = 2;
	mcs_plan[40] = mcs_plan[41] = 255; mcs_plan[42] = 1; mcs_plan[43] = 2;
	mcs_plan[48] = 1; mcs_plan[60] = 5;
	/* Synthetic, unique FCH keys; production plans take the release-bound
	 * original table from private input. Key1 and key65536 exercise MCS1/5. */
	for (unsigned i = 0; i < 25; i++) { put32(mcs_plan + 168 + i * 4, (i / 9) << 16 | ((i / 3) % 3) << 8 | i % 3); }
	put32(mcs_plan + 168 + 5 * 4, 65536); put32(mcs_plan + 168 + 9 * 4, 258);
	/* MCS2's normal modulation/rate key for the TX command test. */
	put32(mcs_plan + 168, 2); put32(mcs_plan + 168 + 2 * 4, 0);
	put32(mcs_plan + AR_RF_DEV4_PREFIX, 5770000);
	assert(ar_rf_monitor_plan(5770000, 100, 4, 67, connected_plan, sizeof connected_plan) == -22);
	mcs_plan[43] = 4;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 67, mcs_plan, sizeof mcs_plan) == -22);
	mcs_plan[43] = 2; mcs_plan[44] = 1;
	assert(ar_rf_monitor_plan(5770000, 100, 4, 67, mcs_plan, sizeof mcs_plan) == -22);
	mcs_plan[44] = 0;
	assert(writes == before_writes);
	assert(sizeof(struct ar_rf_mcs_info) == 256);
	for (unsigned operation = 65; operation <= 77; operation++) {
	event_count = 0; memset(event_data, 0, sizeof event_data);
	ar_rf_set_event_callback(receive_event, event_data);
	/* Independent hardware stimulus; the preceding case can leave a valid
	 * command in its synthetic decoder registers after the monitor stops. */
	for (unsigned i = 0; i < 3; i++) { *reg(0x0a110f04 + i * 4) = 0; }
	forwarded_count = 0; transmit_calls = 0; transmit_commits = 0;
	transmit_remaining = sizeof transmit_source;
	for (unsigned i = 0; i < sizeof transmit_source; i++) { transmit_source[i] = i ^ 0x5a; }
	ar_rf_set_tx_callbacks(transmit_peek, transmit_commit, transmit_source);
	login_control[39] = 1; login_control[2] = 0; checksum = 0;
	for (unsigned i = 0; i < sizeof login_control; i++) { checksum = ar_mac_crc8_step(checksum ^ login_control[i]); }
	login_control[2] = checksum;
	ar_rf_set_payload_callback(receive_payload, forwarded);
	if (operation >= 69) { ticks = UINT32_MAX - 500000; }
	uint64_t session_started = 0;
	uint8_t candidate_request[402] = {0, 2};
	memcpy(candidate_request + 2, mcs_plan + 12, 8);
	if (operation >= 76) {
		if (operation == 77) {
			assert(ar_rf_prepare_host(5770000, 0, 4, mcs_plan, sizeof mcs_plan) == -22);
			assert(ar_rf_monitor_plan(5770000, 0, 4, 77, mcs_plan, sizeof mcs_plan) == -22);
			assert(ar_rf_prepare_host_continuous(5770000, 4, mcs_plan, sizeof mcs_plan) == 0);
		} else { assert(ar_rf_prepare_host(5770000, 100, 4, mcs_plan, sizeof mcs_plan) == 0); }
		assert(ar_rf_information()->phase == AR_RF_PSRAM && !event_count);
		assert((*reg(0x0a110800) & 5) == 5);
		unsigned prepare_writes = writes;
		for (unsigned i = 0; i < 50; i++) { ar_rf_poll(); }
		assert(writes == prepare_writes && !event_count);
		assert(ar_rf_host_status(0x3ff, host_status) == 0 && !host_status[252] && host_status[253] == 25);
		assert(ar_rf_host_candidates(NULL, 402) == -22);
		assert(ar_rf_host_candidates(candidate_request, 401) == -22);
		candidate_request[0] = 1; assert(ar_rf_host_candidates(candidate_request, 402) == -22);
		candidate_request[0] = 0; candidate_request[1] = 101;
		assert(ar_rf_host_candidates(candidate_request, 402) == -22 && writes == prepare_writes);
		candidate_request[1] = 2;
		assert(ar_rf_host_candidates(candidate_request, 402) == 0);
		assert(ar_rf_information()->phase == AR_RF_RECEIVING);
		session_started = ticks;
	} else {
		assert(ar_rf_monitor_plan(5770000, 100, 4, operation,
			operation >= 67 ? mcs_plan : connected_plan,
			operation >= 67 ? sizeof mcs_plan : sizeof connected_plan) == 0);
	}
	if (operation >= 67) {
		unsigned status_writes = writes;
		memset(host_status, 0xa5, sizeof host_status);
		assert(ar_rf_host_status(0x3ff, host_status) == 0 && writes == status_writes);
		assert(host_status[0] == 1 && !host_status[1] && !host_status[2] && host_status[3] == 1);
		assert(host_status[4] == 1 && host_status[5] == 1 && !memcmp(host_status + 6, mcs_plan + 4, 4));
		assert(!host_status[252] && host_status[253] == 25 && !memcmp(host_status + 254, mcs_plan + 8, 4));
		for (unsigned slot = 1; slot < 8; slot++) { assert(host_status[252 + slot * 6] == 3 && host_status[253 + slot * 6] == 25); }
		const uint8_t freq[4] = {0x10,0x0b,0x58,0};
		for (unsigned user = 0; user < 10; user++) {
			assert(!memcmp(host_status + 20 + 24*user, freq, 4) && !memcmp(host_status + 32 + 24*user, freq, 4));
			assert(host_status[24 + 24*user] == 25);
		}
		assert(ar_rf_host_status(0x100, host_status) == 0);
		for (unsigned i = 12; i < 252; i++) { if (i < 204 || i >= 228) { assert(host_status[i] == 0); } }
		uint32_t saved = *reg(0x0a1109e0); *reg(0x0a1109e0) = (saved & ~7u) | 7;
		assert(ar_rf_host_status(0x100, host_status) == -95); *reg(0x0a1109e0) = saved;
	}
	*reg(0x0a110f00) = 0x1096; *reg(0xe0801168) |= 1; ar_rf_poll();
	assert(!ar_rf_receive_information()->mcs_buffer_transitions);
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 0, 0, control, sizeof control, NULL, 0, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; inject_valid = 1; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->dev_state == 1);
	if (operation >= 75) {
		assert(event_count == 2 && !event_ids[0] && !event_ids[1]);
		assert(!memcmp(event_data[0], (uint8_t[]){0,0,3}, 3));
		assert(!memcmp(event_data[1], (uint8_t[]){0,1,0}, 3));
	}
	*reg(0x0a110f5c) = 160 << 16; *reg(0xe080117c) |= 1; ar_rf_poll();
	assert(!transmit_calls && !transmit_commits);

	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 1, 0, login_control, sizeof login_control, NULL, 0, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->sync_unsupported == 1);
	assert(ar_rf_receive_information()->dev_state == 1 && ar_rf_receive_information()->assigned_slot == 8);
	memset(login_control + 39, 0, 4); login_control[2] = 0; checksum = 0;
	for (unsigned i = 0; i < sizeof login_control; i++) { checksum = ar_mac_crc8_step(checksum ^ login_control[i]); }
	login_control[2] = checksum;
	uint8_t port0[20] = {0x4a,2,16,0x58};
	struct ar_mac_payload part = {.data = port0, .length = sizeof port0, .port = 0, .flags = 15};
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 2, 0, login_control, sizeof login_control, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->dev_state == 2 && ar_rf_receive_information()->assigned_slot == 0);
	if (operation >= 67) { assert(ar_rf_host_status(0, host_status) == 0 && host_status[252] == 2); }
	if (operation >= 75) {
		assert(event_count == 3 && !event_ids[2]);
		assert(!memcmp(event_data[2], (uint8_t[]){0,2,1}, 3));
	}
	if (operation == 76) {
		unsigned update_writes = writes;
		assert(ar_rf_host_candidates(candidate_request, 402) == 0 && writes == update_writes);
		candidate_request[1] = 0;
		assert(ar_rf_host_candidates(candidate_request, 402) == -95 && writes == update_writes);
		/* Rejected list replacement preserves admission and the live link. */
		assert(ar_rf_receive_information()->dev_state == 2 && event_count == 3);
	}
	assert(ar_rf_receive_information()->dev_token == 9 && ar_rf_receive_information()->sync_ack_pending == 1);
	uint32_t bf0_before = *reg(0x0a110bf0), d08_before = *reg(0x0a110d08);
	*reg(0x0a110f00) = 0x1094; *reg(0xe0801168) |= 1; ar_rf_poll();
	assert(!ar_rf_receive_information()->mcs_buffer_transitions);
	*reg(0x0a110f00) = 0x1096; *reg(0xe0801168) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->mcs_buffer_transitions == (operation == 65));
	assert(*reg(0x0a110bf0) == ((bf0_before & ~3u) | 1));
	assert(*reg(0x0a110d08) == (d08_before & ~(1u << 16)));
	*reg(0xe0801168) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->mcs_buffer_transitions == (operation == 65));
	assert((*reg(0x0a110cf4) & (1u << 21)) && (*reg(0x0a110b4c) & 0xf8) == 8);
	assert(ar_rf_receive_information()->port0_packets == 1 && ar_rf_receive_information()->last_port0_length == 20);
	assert(!memcmp(ar_rf_receive_information()->first_port0, port0, 20));
	*reg(0x0a110f5c) = 39 << 16; *reg(0xe080117c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->sync_ack_pending && !ar_rf_receive_information()->sync_ack_submitted);
	assert(ar_rf_receive_information()->last_tx_length == 24);
	assert(ar_rf_receive_information()->last_tx_packet[12] == 2 && ar_rf_receive_information()->last_tx_packet[14] == 25);
	*reg(0x0a110f5c) = 40 << 16; *reg(0xe080117c) |= 1; ar_rf_poll();
	assert(!ar_rf_receive_information()->sync_ack_pending && ar_rf_receive_information()->sync_ack_submitted == 1);
	assert(ar_rf_receive_information()->last_tx_length == 33);
	assert(ar_mac_decode(ar_rf_receive_information()->first_sync_ack_packet, 33, &encoded) == 0);
	assert(ar_mac_payload_at(&encoded, 0, &part) == 0 && part.length == 5 && part.port == 0);
	assert(!memcmp(part.data, "\x4a\x04\x01\x4f\x00", 5));
	assert((*reg(0x0a110d40) & 0xff00ffff) == 5);
	assert(!transmit_calls && !transmit_commits); /* ACK priority, including insufficient budget. */
	if (operation >= 66) {
		*reg(0x0a110f5c) = 31 << 16; *reg(0xe080117c) |= 1; ar_rf_poll();
		assert(!transmit_calls && transmit_remaining == 140); /* Rounded budget is24. */
		*reg(0x0a110f5c) = 39 << 16; *reg(0xe080117c) |= 1; ar_rf_poll();
		assert(transmit_calls == 1 && transmit_commits == 1 && transmit_remaining == 136);
		const struct ar_rf_tx_payload_info *tx = ar_rf_tx_payload_information();
		assert(tx->packets == 1 && tx->bytes == 4 && tx->first_length == 32);
		assert(ar_mac_decode(tx->first_packet, tx->first_length, &encoded) == 0);
		assert(ar_mac_payload_at(&encoded, 0, &part) == 0 && part.port == 3 && part.length == 4);
		assert(!memcmp(part.data, transmit_source, 4));
		*reg(0x0a110f5c) = 160 << 16; *reg(0xe080117c) |= 1; ar_rf_poll();
		assert(transmit_remaining == 4 && tx->last_length == 160 && tx->bytes == 136);
		assert(ar_mac_decode(tx->last_packet, tx->last_length, &encoded) == 0);
		assert(ar_mac_payload_at(&encoded, 0, &part) == 0 && part.port == 3 && part.length == 132);
		assert(!memcmp(part.data, transmit_source + 4, 132));
		*reg(0xe080117c) |= 1; ar_rf_poll();
		assert(!transmit_remaining && transmit_commits == 3 && tx->bytes == 140);
		memset(transmit_source, 0, sizeof transmit_source);
		assert(tx->first_packet[28] == 0x5a && tx->last_packet[28] == (136 ^ 0x5a));
	}

	/* The same full login is idempotent once connected. */
	injected = 0; ar_rf_poll(); ar_rf_poll();
	*reg(0xe080117c) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->sync_ack_submitted == 1 && ar_rf_receive_information()->last_tx_length == 24);
	assert(ar_rf_receive_information()->port_packets[0] == 2);
	assert(ar_rf_receive_information()->port0_commands[2] == 2);
	/* Seven identical one-ID requests initialize then trip the configured
	 * six-miss watchdog. Only B00 bit30 changes, and the next two events
	 * restore it without retiring any owned DMA packet. */
	uint32_t b00_before = *reg(0x0a110b00);
	*reg(0x0a110f68) = 0; *reg(0x0a110f64) = 5u << 20;
	for (unsigned i = 0; i < 7; i++) { *reg(0xe0801178) |= 1; ar_rf_poll(); }
	assert(ar_rf_receive_information()->retx_disable == 1 && ar_rf_receive_information()->retx_phase == 2);
	assert(*reg(0x0a110b00) == (b00_before | (1u << 30)));
	unsigned owned_before = ar_rf_receive_information()->tx_owned;
	*reg(0xe0801178) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->retx_phase == 1 && !ar_rf_receive_information()->retx_enable);
	*reg(0xe0801178) |= 1; ar_rf_poll();
	assert(ar_rf_receive_information()->retx_enable == 1 && !ar_rf_receive_information()->retx_phase);
	assert(*reg(0x0a110b00) == b00_before && ar_rf_receive_information()->tx_owned == owned_before);
	if (operation >= 67) {
		const struct ar_rf_mcs_info *mcs = ar_rf_mcs_information();
		assert(mcs->state & 1); assert(!mcs->requests);
		*reg(0x0a110fbc) = 8 << 20; *reg(0x0a110f28) = 0; *reg(0x0a110ee0) = 500;
		for (unsigned i = 0; i < 8; i++) {
			*reg(0x0a110f00) = 0x1096; *reg(0xe0801168) |= 1; ar_rf_poll();
			*reg(0xe0801160) |= 1; ar_rf_poll();
		}
		mcs = ar_rf_mcs_information();
		assert(mcs->samples == 8 && mcs->requests == 1 && mcs->current == 5 && (mcs->state & 8));
		assert(!mcs->acknowledgements && !mcs->fch_writes);
		uint32_t sync_before = *reg(0x0a110b8c), d30_before = *reg(0x0a110d30);
		*reg(0xe080117c) |= 1; ar_rf_poll();
		assert(*reg(0x0a110b8c) == sync_before); /* Enqueue precedes source18 write. */
		assert(*reg(0x0a110d30) == ((d30_before & ~0xff0000u) | 0xff0000u));
		*reg(0x0a110f68) = 0; *reg(0xe0801178) |= 1; ar_rf_poll();
		uint8_t command[8] = {0,0xf3,5,0,0xc5,0,0,0}; command[0] = ar_rf_control_checksum(command);
		uint32_t expected0, expected1; memcpy(&expected0, command, 4); memcpy(&expected1, command + 4, 4);
		assert(*reg(0x0a110b8c) == expected0 && *reg(0x0a110b90) == expected1);
		mcs = ar_rf_mcs_information(); assert(mcs->fch_writes == 1 && mcs->advertisements == 1);
		*reg(0x0a110f00) = 0x3080; *reg(0xe0801168) |= 1; ar_rf_poll();
		mcs = ar_rf_mcs_information();
		assert(mcs->acknowledgements == 1 && !(mcs->state & 8) && mcs->buffer_applies == 1);
		assert(mcs->observed_counts[5] == 1 && mcs->fch_writes == 2 && mcs->cancellations == 1);
		if (operation >= 75) {
			assert(event_count == 4 && event_ids[3] == 1);
			assert(!memcmp(event_data[3], (uint8_t[]){0,1,5,25}, 4));
		}
		command[1] = 0xf0; command[0] = ar_rf_control_checksum(command); memcpy(&expected0, command, 4);
		assert(*reg(0x0a110b8c) == expected0 && *reg(0x0a110b90) == expected1);
		assert(*reg(0x0a110d30) == (d30_before & ~0xff0000u));
		if (operation >= 68) {
			assert(sizeof(struct ar_rf_phy_trace) == 3328);
			const struct ar_rf_phy_trace *trace = ar_rf_phy_trace_information();
			assert(trace->events && trace->buckets[0].count == 8 && !trace->buckets[1].count);
			assert(trace->buckets[0].snr_min == 500 && trace->buckets[0].snr_max == 500);
			assert(trace->buckets[0].zero_errors == 8);
			struct ar_rf_phy_event first = trace->first[0];
			uint32_t bb0 = *reg(0x0a110bb0) | 0x80, c00 = *reg(0x0a110c00) | 0x80;
			*reg(0x0a110bb0) = bb0; *reg(0x0a110c00) = c00;
			/* The MCS ACK FCH reset the EOP guard. First EOP consumes a
			 * sample; the next100 are retained only by the diagnostic. */
			for (unsigned i = 0; i < 101; i++) { *reg(0xe0801160) |= 1; ar_rf_poll(); }
			assert(ar_rf_phy_information()->samples == 9);
			assert(trace->buckets[0].count == 9 && trace->buckets[1].count == 100);
			assert(*reg(0x0a110bb0) == bb0 && *reg(0x0a110c00) == c00);
			assert(trace->first_count == 32 && trace->last_count == 32 && trace->last_head == (trace->events & 31));
			assert(!memcmp(&first, &trace->first[0], sizeof first));
			for (unsigned i = 0; i < 32; i++) {
				const struct ar_rf_phy_event *event = &trace->last[(trace->last_head + i) & 31];
				assert((event->flags & 255) == 12 && (event->flags & 256) && !(event->flags & (1u << 13)));
				assert(event->samples == 9 && (uint16_t)event->snr == 500 && event->end_us >= event->begin_us);
			}
		}
	}

	if (operation >= 69) {
		assert(sizeof(struct ar_rf_sync_info) == 128);
		const struct ar_rf_sync_info *sync = ar_rf_sync_information();
		uint32_t de0 = *reg(0x0a110de0), df0 = *reg(0x0a110df0);
		uint8_t identity[7] = {0x25,0x16,4,0x48,0xcb,0x6a,6};
		assert(ar_rf_control_checksum(identity) == identity[0]); /* Trial114 full-tail vector. */
		identity[0] ^= 1; receive_sync(identity, 0);
		assert(!sync->rx_writes && *reg(0x0a110de0) == de0);
		identity[2] = 2; receive_sync(identity, 1);
		assert(sync->rejected && !sync->rx_writes); /* Correct check, wrong length. */
		identity[2] = 4; identity[1] = 0x56; receive_sync(identity, 1);
		assert(sync->pending && !sync->rx_writes); /* Do not apply at TTL5. */
		ticks += 10 * 40000; ar_rf_poll(); assert(!sync->rx_writes);
		identity[1] = 0x16; receive_sync(identity, 1);
		assert(sync->rx_writes == 1 && !sync->pending && sync->identity == 0xcb && sync->user == 8);
		assert(*reg(0x0a110de0) == ((de0 & ~255u) | 0xcb));
		assert(*reg(0x0a110df0) == ((df0 & ~255u) | 0xcb));
		ticks += 20 * 40000; ar_rf_poll(); assert(sync->rx_writes == 1); /* Canceled fallback. */
		identity[1] = 0x26; identity[4] = 0x55; receive_sync(identity, 1);
		ticks += 8 * 40000; ar_rf_poll(); assert(sync->rx_writes == 1);
		ticks += 2 * 40000; ar_rf_poll(); /* Missing TTL1: 5ms frame +4ms fallback. */
		assert(sync->rx_writes == 2 && !sync->pending && sync->timer_fired == 1);
		assert((*reg(0x0a110de0) & 255) == 0x55);
		identity[4] = 0x66; receive_sync(identity, 1);
		uint8_t idle_header[7] = {0}; receive_sync(idle_header, 1);
		ticks += 10 * 40000; ar_rf_poll(); assert(sync->rx_writes == 2); /* Latest cache is idle. */
		identity[1] = 0xf6; identity[2] = 5; identity[3] = 0x58;
		uint32_t tx8 = *reg(0x0a110b84), txshared = *reg(0x0a110b60);
		receive_sync(identity, 1); assert(sync->tx_writes == 1);
		assert(*reg(0x0a110b84) == ((tx8 & ~0xff0000u) | 0x660000));
		assert(*reg(0x0a110b60) == ((txshared & ~0xff0000u) | 0x660000));
		identity[3] = 0x49; uint32_t rx9 = *reg(0x0a110de8), shared = *reg(0x0a110df0);
		receive_sync(identity, 1); assert(sync->rx_writes == 3);
		assert(*reg(0x0a110de8) == ((rx9 & ~255u) | 0x66) && *reg(0x0a110df0) == shared);
		identity[3] = 0x4a; receive_sync(identity, 1); assert(sync->invalid_users == 1 && sync->rx_writes == 3);
		/* AP time passes through the real FCH checksum/length gates. A valid
		 * time header replaces a queued identity without applying its timer. */
		identity[1] = 0x26; identity[2] = 4; identity[3] = 8;
		receive_sync(identity, 1); assert(sync->pending);
		uint8_t clock[7] = {0,0xa7,9,0x67,0x45,0x23,0xf1};
		receive_sync(clock, 1);
		uint32_t time_before = ar_rf_host_ap_time();
		assert(time_before - UINT32_C(0xa1234567) <= 1);
		assert(sync->pending && sync->rx_writes == 3);
		unsigned unsupported_before = sync->unsupported;
		ticks += 20 * 40000; ar_rf_poll();
		assert(!sync->pending && sync->rx_writes == 3 && sync->unsupported == unsupported_before);
		assert(ar_rf_host_ap_time() - time_before >= 20 && ar_rf_host_ap_time() - time_before <= 21);
		clock[3] = 0; /* Corrupt the check without recomputing it. */
		receive_sync(clock, 0);
		assert(ar_rf_host_ap_time() - time_before <= 21);
		clock[2] = 6; receive_sync(clock, 1); /* Correct check, wrong length. */
		assert(ar_rf_host_ap_time() - time_before <= 21);
	}
	if (operation >= 72) {
		assert(sizeof(struct ar_rf_tx_mcs_info) == 64);
		const struct ar_rf_tx_mcs_info *txm = ar_rf_tx_mcs_information();
		uint8_t request[7] = {0,0xf3,5,0,0xc2,0x83,6};
		uint32_t initial = *reg(0x0a110b00);
		/* Idempotent repeated request; a mode2 TX field becomes mode1 for
		 * normal-table MCS2. All unrelated fields and duration are retained. */
		*reg(0x0a110b00) = (initial & ~0xc00000u) | 0x800000u;
		uint32_t duration = *reg(0x0a110a00);
		receive_sync(request, 1);
		assert(txm->requests == 1 && txm->applied == 1 && txm->current == 2 && txm->mode_changed == 1);
		assert(*reg(0x0a110b00) == ((initial & ~0xc3f700u) | 0x406000u));
		assert(*reg(0x0a110a00) == duration);
		uint32_t applied = *reg(0x0a110b00);
		receive_sync(request, 1); assert(txm->applied == 1 && txm->unchanged == 1);
		request[3] = 1; receive_sync(request, 1); assert(txm->rejected == 1);
		request[3] = 0; request[4] = 0x3f; receive_sync(request, 1); assert(txm->rejected == 2);
		request[4] = 0xc1; receive_sync(request, 1); assert(txm->downgrades == 1 && txm->current == 2);
		assert(*reg(0x0a110b00) == applied);
	}
	/* Compact CRC integrity and token continuity are separate gates. */
	uint8_t compact_control[6] = {0,23,9,2,0,0};
	for (unsigned i = 1; i < 6; i++) { compact_control[0] = ar_mac_crc8_step(compact_control[0] ^ compact_control[i]); }
	uint8_t data_payload[140]; memset(data_payload, 0xa6, sizeof data_payload);
	part.data = data_payload; part.length = sizeof data_payload; part.port = 1;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 3, 0, compact_control, 6, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->compact_valid == 1 && ar_rf_receive_information()->dev_state == 2);
	assert(ar_rf_receive_information()->first_data_port == 1 && ar_rf_receive_information()->first_data_length == 140);
	assert(ar_rf_receive_information()->port_bytes[1] == 140);
	assert(!memcmp(ar_rf_receive_information()->first_data, data_payload, 128));
	assert(forwarded_count == 1 && forwarded_slot == 0 && forwarded_port == 1 && forwarded_length == 140);
	assert(!memcmp(forwarded, data_payload, 140));
	uint8_t other_peer[sizeof login_control]; memcpy(other_peer, login_control, sizeof other_peer);
	other_peer[8] ^= 1; other_peer[2] = 0; checksum = 0;
	for (unsigned i = 0; i < sizeof other_peer; i++) { checksum = ar_mac_crc8_step(checksum ^ other_peer[i]); }
	other_peer[2] = checksum;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 4, 0, other_peer, sizeof other_peer, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(forwarded_count == 1 && ar_rf_receive_information()->dev_state == 2);
	/* One internal command split across two separate MAC descriptors/frames. */
	uint8_t split_command[8] = {0x4a,3,4,0x49,1,2,3,4};
	part.port = 0; part.data = split_command; part.length = 3;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 4, 0, compact_control, 6, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(!ar_rf_receive_information()->stream_commands[3] && ar_rf_receive_information()->stream_buffered == 3);
	part.data = split_command + 3; part.length = 5;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 5, 0, compact_control, 6, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->stream_commands[3] == 1 && !ar_rf_receive_information()->stream_buffered);
	assert(ar_rf_receive_information()->first_nonstats_length == 8);
	assert(!memcmp(ar_rf_receive_information()->first_nonstats, split_command, 8));
	compact_control[0] ^= 1;
	part.port = 4; part.data = data_payload; part.length = sizeof data_payload;
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 4, 0, compact_control, 6, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->compact_invalid == 1 && ar_rf_receive_information()->dev_state == 2);
	assert(forwarded_count == 1); /* Bad control must not forward payload. */
	/* A valid compact with a changed token prevents further transmission. */
	if (operation != 77) {
	compact_control[2]++; compact_control[0] = 0;
	for (unsigned i = 1; i < 6; i++) { compact_control[0] = ar_mac_crc8_step(compact_control[0] ^ compact_control[i]); }
	assert(ar_mac_encode(valid_packet, sizeof valid_packet, 5, 0, compact_control, 6, &part, 1, &packet_size) == 0);
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(ar_rf_receive_information()->link_invalidations == 1 && ar_rf_receive_information()->dev_state == 0);
	assert(forwarded_count == 1); /* Token invalidation precedes delivery. */
	if (operation >= 69) {
		uint8_t identity[7] = {0,0x16,4,8,0x77,0,0};
		unsigned count = ar_rf_sync_information()->rx_writes;
		receive_sync(identity, 1);
		assert(ar_rf_sync_information()->rx_writes == count && ar_rf_sync_information()->state_skips);
	}
	unsigned calls_before = transmit_calls;
	transmit_remaining = 1;
	*reg(0xe080117c) |= 1; ar_rf_poll();
	assert(transmit_calls == calls_before && transmit_remaining == 1);

	assert((*reg(0x0a110800) & 4) && !(*reg(0x0a110cf4) & (1u << 21)));
	}
	if (operation >= 71) {
		const struct ar_rf_traffic_info *traffic = ar_rf_traffic_information();
		assert(sizeof *traffic == 768 && traffic->version == 1);
		unsigned accepted = 0, dispatched = 0;
		for (unsigned i = 0; i < 10; i++) {
			const struct ar_rf_command_observation *command = &traffic->commands[i];
			accepted += command->accepted; dispatched += command->dispatched;
			if (!command->accepted) { continue; }
			assert((command->first[1] & 15) == i && (command->last[1] & 15) == i);
			assert(command->first_us <= command->last_us);
			assert(ar_rf_control_checksum(command->first) == command->first[0]);
			assert(ar_rf_control_checksum(command->last) == command->last[0]);
		}
		assert(accepted == ar_rf_sync_information()->accepted && dispatched >= 6);
		assert(traffic->commands[6].accepted >= (operation == 77 ? 7u : 8u));
		assert(traffic->commands[6].dispatched >= (operation == 77 ? 5u : 6u));
		for (unsigned i = 0; i < 5; i++) {
			unsigned total = 0;
			for (unsigned second = 0; second < 16; second++) { total += traffic->port_bytes_by_second[second][i]; }
			assert(total == traffic->ports[i].bytes && total == ar_rf_receive_information()->port_bytes[i]);
			assert(traffic->ports[i].packets == ar_rf_receive_information()->port_packets[i]);
			assert(traffic->ports[i].first_us <= traffic->ports[i].last_us);
		}
	}
	if (operation == 77) {
		/* Session survives more than one low-counter wrap. Control timers use
		 * elapsed milliseconds even after the diagnostic microseconds wrap. */
		const struct ar_rf_sync_info *sync = ar_rf_sync_information();
		uint8_t identity[7] = {0,0x26,4,0x48,0x55,0,0};
		for (unsigned epoch = 0; epoch < 3; epoch++) {
			uint64_t advance = epoch == 0 ? UINT64_C(240000) * 40000 :
				epoch == 1 ? (UINT64_C(1) << 32) * 40 : (UINT64_C(1) << 32) * 40000;
			if (epoch == 2) {
				/* Arm a 9-ms timer four milliseconds before 32-bit ms wrap. */
				ticks = session_started + ((UINT64_C(1) << 32) - 4) * 40000;
			} else { ticks += advance; }
			ar_rf_poll();
			assert(ar_rf_information()->phase == AR_RF_RECEIVING);
			/* Force a fresh EOP sample and check MCS uses the millisecond
			 * clock, not sample_elapsed_us / 1000 after that field wraps. */
			if (!epoch) { tear_timer_read = 1; } /* Low wraps inside high/low/high. */
			*reg(0x0a110f00) = 8 << 9; *reg(0xe0801168) |= 1; ar_rf_poll();
			*reg(0x0a110fbc) = 8 << 20; *reg(0xe0801160) |= 1; ar_rf_poll();
			uint32_t expected_ms = (uint32_t)((ticks - session_started) / 40000);
			uint32_t measured_ms = ar_rf_mcs_information()->sample_ms;
			assert((uint32_t)(measured_ms - expected_ms + 1) <= 2);
			unsigned fired = sync->timer_fired;
			receive_sync(identity, 1); assert(sync->pending);
			ticks += 8 * 40000; ar_rf_poll(); assert(sync->pending && sync->timer_fired == fired);
			ticks += 2 * 40000; ar_rf_poll(); assert(!sync->pending && sync->timer_fired == fired + 1);
		}
		/* One million polls is a diagnostic watchdog, never a normal-session
		 * lifetime. Keep the active radio alive beyond that count as well. */
		for (unsigned i = 0; i < 1000001; i++) {
			/* The peer remains present during this long-session test. */
			if (!(i % 100)) { *reg(0x0a110f00) = 0x1096; *reg(0xe0801168) |= 1; }
			ar_rf_poll();
		}
		assert(ar_rf_information()->phase == AR_RF_RECEIVING);
		assert(ar_rf_stop_host() == 0 && ar_rf_stop_host() == 0);
		assert(!sync->pending && (*reg(0x0a110800) & 5) == 5);
		unsigned stopped_writes = writes;
		ar_rf_poll(); assert(writes == stopped_writes);
	}
	ticks += 100 * 40000; ar_rf_poll(); inject_valid = 0;
	if (operation >= 67) { assert(ar_rf_host_status(0, host_status) == 0 && !host_status[252] && host_status[253] == 25); }
	if (operation >= 76) {
		assert(ar_rf_host_candidates(candidate_request, 402) == (operation == 77 ? 0 : -11));
		assert(ar_rf_information()->phase == AR_RF_PSRAM); /* Explicit stop does not auto-start. */
	}
	if (operation >= 75) {
		unsigned tx_events = 0, rx_events = 0;
		for (unsigned i = 3; i + 1 < event_count; i++) {
			assert(event_ids[i] == 1 && event_data[i][0] == 0);
			if (event_data[i][1]) { rx_events++; }
			else { tx_events++; assert(!memcmp(event_data[i], (uint8_t[]){0,0,2,1}, 4)); }
		}
		assert(tx_events == 1 && rx_events == ar_rf_mcs_information()->buffer_applies);
		assert(!event_ids[event_count - 1] && !memcmp(event_data[event_count - 1], (uint8_t[]){0,0,2}, 3));
	} else { assert(!event_count); }
	}
	ar_rf_set_event_callback(NULL, NULL);
	/* EOP measurement layout, selector restoration, user9 alias, and the
	 * duplicate guard reset by each FCH. These are synthetic register inputs. */
	inject_receive = 0; inject_header = 0;
	before = writes;
	assert(ar_rf_monitor_plan(5770000, 15001, 4, 70, mcs_plan, sizeof mcs_plan) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 10001, 4, 69, mcs_plan, sizeof mcs_plan) == -22 && writes == before);
	assert(ar_rf_monitor_plan(5770000, 15000, 4, 70, mcs_plan, sizeof mcs_plan) == 0);
	ticks += 10001 * 40000; ar_rf_poll(); assert(ar_rf_information()->phase == AR_RF_RECEIVING);
	ticks += 5000 * 40000; ar_rf_poll(); assert(ar_rf_information()->phase == AR_RF_PSRAM);
	assert(ar_rf_monitor_plan(5770000, 100, 4, 64, connected_plan, sizeof connected_plan) == 0);
	assert(sizeof(struct ar_rf_phy_info) == 88);
	assert(!ar_rf_phy_trace_information()->events);
	assert(!ar_rf_sync_information()->accepted && !ar_rf_sync_information()->pending);
	assert(!ar_rf_traffic_information()->commands[6].accepted && !ar_rf_traffic_information()->ports[0].bytes);
	assert(!ar_rf_tx_mcs_information()->requests && ar_rf_tx_mcs_information()->current == 1);
	measurement_banks = 1;
	*reg(0x0a110f00) = (9 << 9) | 0x80;
	*reg(0xe0801168) |= 1; ar_rf_poll();
	*reg(0x0a110bb0) = 0x12348080; *reg(0x0a110c00) = 0x98765480;
	*reg(0x0a110808) = 0xdeadbeef;
	*reg(0x0a110f28) = 0x01230000; *reg(0x0a110ee0) = 0x98764567;
	*reg(0x0a110ee4) = 0x7654329a; *reg(0x0a110fbc) = 9 << 20;
	*reg(0xe0801160) |= 1; ar_rf_poll();
	const struct ar_rf_phy_info *m = ar_rf_phy_information();
	const uint8_t expected_sample[16] = {0x67,0x45,0,0,0x23,1,12,12,0x9a,0,0,0,0x56,0x78,0x12,0x34};
	assert(m->samples == 1 && !m->forced_errors);
	assert(!memcmp(m->sample, expected_sample, 16) && !memcmp(m->first_sample, expected_sample, 16));
	assert(*reg(0x0a110bb0) == 0x12348080 && *reg(0x0a110c00) == 0x98765480 && *reg(0x0a110808) == 0xdeadbeef);
	*reg(0xe0801160) |= 1; ar_rf_poll();
	assert(m->samples == 1 && m->duplicates == 1);
	*reg(0x0a110f00) = 8 << 9; *reg(0xe0801168) |= 1; ar_rf_poll();
	*reg(0xe0801160) |= 1; ar_rf_poll();
	assert(m->samples == 2 && m->forced_errors == 1 && m->sample[2] == 1);
	assert(!memcmp(m->first_sample, expected_sample, 16));
	*reg(0x0a110fbc) = 15 << 20; *reg(0xe0801160) |= 1; ar_rf_poll();
	assert(m->samples == 2 && m->other_users == 1);
	measurement_banks = 0; ticks += 100 * 40000; ar_rf_poll();
	assert(ar_rf_monitor_plan(5770000, 100, 4, 64, connected_plan, sizeof connected_plan) == 0);
	assert(!ar_rf_phy_information()->samples && !ar_rf_phy_information()->duplicates);
	ticks += 100 * 40000; ar_rf_poll();
	/* Normal config search uses the supplied list and keeps the DEV channel
	 * index synchronized with hardware retuning before admission. */
	uint8_t search_plan[sizeof mcs_plan + 4]; memcpy(search_plan,mcs_plan,sizeof mcs_plan);
	put32(search_plan + sizeof mcs_plan,5805000);
	assert(ar_rf_prepare_config(5770000,4,search_plan,sizeof search_plan,1)==0);
	assert(*reg(0x0a110810) == 0x183a113a && *reg(0x0a110814) == 0x00802309);
	assert(*reg(0x0a110870) == 0x0f00dd88 && *reg(0x0a1108a4) == 0xe000);
	assert_channel_plls(5770000);
	uint8_t candidates[402]={0,0};
	assert(ar_rf_host_candidates(candidates,sizeof candidates)==0);
	assert(ar_rf_receive_information()->channel_count==2 && ar_rf_receive_information()->frequency_khz==5770000);
	ticks += 11*40000; ar_rf_poll();
	assert(ar_rf_receive_information()->channel_index==1 && ar_rf_receive_information()->frequency_khz==5805000);
	assert_channel_plls(5805000);
	assert(*reg(0x0a110800) & 4); /* TX stays held until admission. */
	const struct ar_rf_rx_info *search = ar_rf_receive_information();
	unsigned restarts = search->search_restarts;
	uint8_t candidate_header[7] = {0};
	receive_sync(candidate_header,1);
	ticks += search->search_frame_us * 40;
	receive_sync(candidate_header,1);
	assert(search->search_suspended && search->search_restarts == restarts);
	ticks += (search->search_frame_us + 1000) * 40;
	receive_sync(candidate_header,1);
	assert(search->search_restarts == restarts + 1 && !search->search_suspended);
	assert_channel_plls(5770000); /* Repeated good headers did not extend grace. */
	/* A completed but unadmitted packet cannot latch search forever. Pending
	 * DMA does prevent retuning even well beyond the ordinary dwell. */
	assert(ar_mac_encode(valid_packet,sizeof valid_packet,0,0,control,sizeof control,NULL,0,&packet_size)==0);
	*reg(0x0a110f88) = 1; *reg(0x0a110f90) = packet_size;
	inject_valid = inject_receive = delay_dma = 1; injected = 0;
	ar_rf_poll(); restarts = search->search_restarts;
	ticks += 100 * 40000; ar_rf_poll();
	assert(search->search_suspended && search->search_restarts == restarts);
	delay_dma = 0; *reg(0xe0801164) |= 1; ar_rf_poll();
	assert(search->admission_rejects == 1 && !search->acquisition_ready);
	assert(search->search_restarts == restarts + 1 && search->channel_index == 1);
	assert_channel_plls(5805000);
	/* Admission on the second channel sees the updated DEV channel index,
	 * releases TX and prevents further scan retuning. */
	candidates[1] = 1; memcpy(candidates+2,control+8,4);
	assert(!ar_rf_host_candidates(candidates,sizeof candidates));
	control[6] = control[7] = 1; control[2] = 0; checksum = 0;
	for (unsigned i = 0; i < sizeof control; i++) { checksum = ar_mac_crc8_step(checksum ^ control[i]); }
	control[2] = checksum;
	assert(!ar_mac_encode(valid_packet,sizeof valid_packet,1,0,control,sizeof control,NULL,0,&packet_size));
	*reg(0x0a110f90) = packet_size; injected = 0; ar_rf_poll(); ar_rf_poll();
	assert(search->acquisition_ready && search->transition_status == 1 && !(*reg(0x0a110800) & 4));
	restarts = search->search_restarts; ticks += 100 * 40000; ar_rf_poll();
	assert(search->search_suspended && search->search_restarts == restarts);
	/* Load the calibrated context through a real preparation notification.
	 * Restart and recovery must retain it even if pre-encode is polled before
	 * the next preparation IRQ. A register equality check after reloading
	 * would miss the unsafe interval, so also count compensation writes. */
	*reg(0x0a110e6c) = 0; inject_command = 1; ar_rf_poll();
	assert(*reg(0x0a110480) & 2);
	const uint32_t compensation_addresses[] = {0x0a110480,0x0a11048c,0x0a1104a0,0x0a1104a4,0x0a1104a8};
	uint32_t compensation_saved[5];
	for (unsigned i=0;i<5;i++) { compensation_saved[i]=*reg(compensation_addresses[i]); }
	unsigned loaded_writes = compensation_writes;
	inject_command = 1; ar_rf_poll(); assert(compensation_writes == loaded_writes);
	const struct ar_rf_lifecycle_info *life = ar_rf_lifecycle_information();
	assert(life->version == 1 && life->state == 2);
	unsigned starts = life->starts, stops = life->stops, cancelled = life->cancelled_rx;
	before = writes;
	assert(ar_rf_host_control(0,0)==-7 && ar_rf_host_control(2,0)==-7 && ar_rf_host_control(3,0)==-7);
	assert(ar_rf_host_control(1,1)==-22 && ar_rf_host_control(4,0)==-95 && writes==before);
	for (unsigned cycle=0; cycle<3; cycle++) {
		uint8_t clock[7] = {0,7,8,0x40,0x42,0x0f,0}; /* 1,000,000 ms. */
		receive_sync(clock,1);
		uint32_t old_time = ar_rf_host_ap_time(); uint64_t old_ticks = ticks;
		assert(old_time - 1000000 <= 1);
		/* A DMA-complete indication is consumed without forwarding its packet
		 * during stop; no ring slot can be reused while DMA remains owned. */
		if (cycle == 1) { injected = 0; ar_rf_poll(); }
		assert(ar_rf_host_control(1,0)==0 && life->stops==stops+cycle+1);
		assert(ar_rf_lifecycle_information()->state==1 && (*reg(0x0a110800)&13)==13);
		unsigned stopped_writes=writes;
		ar_rf_poll(); assert(writes==stopped_writes);
		assert(ar_rf_host_control(1,0)==-7);
		assert(!ar_rf_host_status(0x3ff,host_status) && !host_status[252]);
		assert(!ar_rf_host_candidates(candidates,sizeof candidates));
		assert(ar_rf_information()->phase==AR_RF_PSRAM);
		if (cycle == 1) {
			assert(!ar_rf_host_control(3,0) && ar_rf_lifecycle_information()->state==0);
			assert(ar_rf_host_control(3,0)==-7 && ar_rf_host_control(0,0)==-7);
			assert(ar_rf_host_status(0x3ff,host_status)==-11);
			assert(ar_rf_host_candidates(candidates,sizeof candidates)==-11);
			assert(!ar_rf_host_control(2,0) && ar_rf_lifecycle_information()->state==1);
			assert(ar_rf_host_ap_time()==UINT32_MAX);
			assert(!ar_rf_host_candidates(candidates,sizeof candidates));
			assert(ar_rf_information()->phase==AR_RF_PSRAM); /* Init is not start. */
		}
		assert(!ar_rf_host_control(0,0) && life->starts==starts+cycle+1);
		assert(ar_rf_lifecycle_information()->state==2 && !search->acquisition_ready);
		assert(compensation_writes == loaded_writes);
		for (unsigned i=0;i<5;i++) { assert(*reg(compensation_addresses[i])==compensation_saved[i]); }
		if (cycle == 1) { assert(ar_rf_host_ap_time()==UINT32_MAX); }
		else {
			uint32_t elapsed = (uint32_t)((ticks-old_ticks)/40000);
			assert(ar_rf_host_ap_time()-old_time-elapsed <= 1);
		}
		assert_channel_plls(5770000);
		ticks += 11*40000; ar_rf_poll(); assert_channel_plls(5805000);
		/* The latest host candidates survived every native state rebuild. */
		injected = 0; ar_rf_poll(); ar_rf_poll();
		assert(search->acquisition_ready && search->transition_status==1);
		*reg(0xe080117c) |= 1; ar_rf_poll(); /* Pre-encode precedes prepare. */
		assert(compensation_writes == loaded_writes && (*reg(0x0a110480)&2));
		*reg(0x0a110e6c) = 0; inject_command = 1; ar_rf_poll();
		assert(compensation_writes == loaded_writes);
	}
	assert(life->cancelled_rx==cancelled+1);
	/* Normal sessions reacquire autonomously. Unrelated PHY users cannot
	 * refresh logical user8, and loss does not require any MAC/video packet. */
	const struct ar_rf_recovery_info *recovery = ar_rf_recovery_information();
	unsigned recovered = recovery->completed, lost = recovery->fch_losses;
	inject_receive = 0;
	*reg(0x0a110f00) = 0x1096; *reg(0xe0801168) |= 1; ar_rf_poll();
	unsigned threshold = recovery->monitor_threshold, interval = recovery->monitor_interval_ms;
	assert(threshold && interval);
	for (unsigned i=0;i<threshold;i++) {
		ticks += interval * 40000;
		*reg(0x0a110f00) = 0x80; *reg(0xe0801168) |= 1; ar_rf_poll();
	}
	assert(recovery->completed==recovered+1 && recovery->fch_losses==lost+1 && !recovery->failures);
	assert(compensation_writes == loaded_writes);
	for (unsigned i=0;i<5;i++) { assert(*reg(compensation_addresses[i])==compensation_saved[i]); }
	assert(ar_rf_information()->phase==AR_RF_RECEIVING && !search->acquisition_ready && !search->dev_state);
	assert_channel_plls(5770000);
	/* Candidate removal drains a pending DMA without delivering its old
	 * packet. Latest configuration remains available for later re-admission. */
	for (unsigned cycle=0;cycle<2;cycle++) {
		ticks += 11*40000; ar_rf_poll(); assert_channel_plls(5805000);
		assert(!ar_mac_encode(valid_packet,sizeof valid_packet,0,0,control,sizeof control,NULL,0,&packet_size));
		*reg(0x0a110f90)=packet_size; inject_receive=1; injected=0; ar_rf_poll(); ar_rf_poll();
		assert(search->acquisition_ready && search->dev_state==1);
		/* Complete login, then invalidate with a new peer token on cycle 1. */
		if (cycle==1) {
			login_control[6]=login_control[7]=1; memset(login_control+39,0,4);
			login_control[2]=0; checksum=0;
			for(unsigned i=0;i<sizeof login_control;i++) { checksum=ar_mac_crc8_step(checksum^login_control[i]); }
			login_control[2]=checksum;
			assert(!ar_mac_encode(valid_packet,sizeof valid_packet,1,0,login_control,sizeof login_control,NULL,0,&packet_size));
			*reg(0x0a110f90)=packet_size; injected=0; ar_rf_poll(); ar_rf_poll();
			assert(search->dev_state==2);
			uint8_t changed[6]={0,23,10,2,0,0};
			for(unsigned i=1;i<6;i++) { changed[0]=ar_mac_crc8_step(changed[0]^changed[i]); }
			assert(!ar_mac_encode(valid_packet,sizeof valid_packet,2,0,changed,sizeof changed,NULL,0,&packet_size));
			*reg(0x0a110f90)=packet_size; injected=0; ar_rf_poll(); ar_rf_poll();
			assert(recovery->completed==recovered+3 && recovery->last_reason==2 && recovery->invalidations);
		} else {
			delay_dma=1; injected=0; ar_rf_poll(); /* DMA still in flight. */
			complete_dma_reads=3;
			unsigned cancelled_before=life->cancelled_rx;
			candidates[1]=0; assert(!ar_rf_host_candidates(candidates,sizeof candidates));
			assert(recovery->pending==3 && (*reg(0x0a110800)&4));
			ar_rf_poll();
			assert(recovery->completed==recovered+2 && life->cancelled_rx==cancelled_before+1);
			assert(!complete_dma_reads && recovery->stop_stage==3 && recovery->stop_pending_rx==1);
			delay_dma=0;
			assert(recovery->last_reason==3 && recovery->candidate_removals);
			inject_receive=0;
			/* A removed peer cannot regain admission until explicitly restored. */
			ticks += 11*40000; ar_rf_poll();
			inject_receive=1; injected=0; ar_rf_poll(); ar_rf_poll();
			assert(!search->acquisition_ready && search->admission_rejects);
			assert(!ar_rf_stop_host());
			candidates[1]=1; assert(!ar_rf_host_candidates(candidates,sizeof candidates));
			assert(!ar_rf_host_control(0,0));
		}
	}
	assert(compensation_writes == loaded_writes);
	/* Bounded diagnostics own their field setup independently. Returning to
	 * normal operation must await a fresh preparation, not reuse stale cache. */
	assert(!ar_rf_stop_host());
	assert(!ar_rf_monitor_plan(5770000,100,4,64,connected_plan,sizeof connected_plan));
	assert(!(*reg(0x0a110480)&2) && compensation_writes > loaded_writes);
	ticks += 100*40000; ar_rf_poll();
	assert(!ar_rf_prepare_config(5770000,4,search_plan,sizeof search_plan,1));
	assert(!ar_rf_host_candidates(candidates,sizeof candidates));
	assert(!(*reg(0x0a110480)&2));
	/* Recovery failure cannot reuse an outstanding DMA slot or resume TX. */
	ticks+=11*40000; ar_rf_poll();
	assert(!ar_mac_encode(valid_packet,sizeof valid_packet,0,0,control,sizeof control,NULL,0,&packet_size));
	*reg(0x0a110f90)=packet_size; injected=0; ar_rf_poll(); ar_rf_poll();
	assert(search->acquisition_ready);
	loaded_writes = compensation_writes;
	*reg(0x0a110e6c) = 0; inject_command = 1; ar_rf_poll();
	assert((*reg(0x0a110480)&2) && compensation_writes > loaded_writes);
	/* A stuck DMA must fail stop within its bound, keep its storage owned and
	 * refuse start/init. A late completion cannot revive the stopped session. */
	delay_dma=1; injected=0; ar_rf_poll();
	candidates[1]=0; assert(!ar_rf_host_candidates(candidates,sizeof candidates));
	ar_rf_poll();
	assert(recovery->failures==1 && (int32_t)recovery->last_error==-110 && ar_rf_lifecycle_information()->state==3);
	assert(recovery->stop_stage==1 && recovery->stop_pending_rx==1);
	before=writes;
	assert(ar_rf_host_control(0,0)==-110 && ar_rf_host_control(2,0)==-110 && writes==before);
	*reg(0xe0801164)|=1; ar_rf_poll(); assert(writes==before);
	assert(ar_rf_probe() == -110); /* Dead OTP does not hang or claim completion. */
	assert(ar_rf_information()->phase == AR_RF_OFF);
	puts("synthetic RF peripheral contracts passed");
	return 0;
}
