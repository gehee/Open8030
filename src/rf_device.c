/* SPDX-License-Identifier: MIT */
/* Independently implemented Ascent VRX RF peripheral and DEV receiver. Board
 * profiles are data, not executable vendor instructions. Native video and
 * explicit host restarts are verified; full firmware parity remains open. */
#include "ar_rf_device.h"
#include "ar_mac.h"
#include "ar_dev.h"
#include "ar_dev_tx.h"
#include "ar_dev_mcs.h"
#include "ar_fch_monitor.h"
#include "ar_ap_time.h"
#include "ar_command.h"
#include <string.h>
#ifndef AR_RF_SIMULATED
#include "ar_sdio_device.h"
#define progress ar_sdio_record_progress
#else
static void progress(uint32_t stage, uint32_t a, uint32_t b, uint32_t c) { (void)stage; (void)a; (void)b; (void)c; }
#endif

#define BB UINT32_C(0x0a110000)
#define CLOCK UINT32_C(0x0a104000)
#define OTP UINT32_C(0x00140000)
#define TIMER UINT32_C(0xe000bff8)
static struct ar_rf_info information;
static uint8_t calibration[5];
static unsigned pins_configured;
static struct ar_rf_rx_info receiver;
static uint8_t receive_buffers[4][65536] __attribute__((aligned(512)));
static unsigned receive_head, receive_pending, receive_length, receive_user;
static uint32_t receive_metadata;
static uint64_t receive_started;
static struct ar_ap_time ap_time;
static uint32_t receive_milliseconds, receive_iterations;
static unsigned receive_operation;
static ar_rf_event_callback event_callback;
static void *event_context;
void ar_rf_set_event_callback(ar_rf_event_callback callback, void *context)
{
	event_callback = callback; event_context = context;
}
static void publish_event(unsigned id, const uint8_t *data, size_t length)
{
	if (receive_operation >= 75 && event_callback) { event_callback(id, data, length, event_context); }
}
static void link_event(unsigned current, unsigned previous)
{
	/* DEV 0x22870c publishes host slot0,current,previous. */
	if (current == previous) { return; }
	const uint8_t data[] = {0, current, previous};
	publish_event(0, data, sizeof data);
}
static void mcs_event(unsigned direction, unsigned current, unsigned previous)
{
	/* 0x21f8be: DEV host slot0, direction0=TX/1=RX. */
	const uint8_t data[] = {0, direction, current, previous};
	publish_event(1, data, sizeof data);
}
static uint32_t search_started;
static unsigned normal_search, search_candidate_seen;
static uint32_t search_candidate_started;
static uint64_t search_pll;
static uint64_t channel_plls[AR_RF_MAX_CHANNELS];
static struct ar_command_program command_program;
static struct ar_command_state command_state = {.cached_bank = 3, .board_band = -1};
static unsigned commands_ready;
#ifdef AR_RF_SIMULATED
const struct ar_command_program *ar_rf_command_information(void) { return &command_program; }
#endif
static struct ar_dev_idle_config dev_config;
static uint8_t dev_candidates[AR_DEV_MAX_CANDIDATES][4], dev_status_a, dev_status_b;
static unsigned host_prepared;
static unsigned host_initialized, host_start_required;
static struct {
	uint8_t channels[AR_RF_DEV4_PREFIX + AR_RF_MAX_CHANNELS * 4];
	size_t length;
	uint32_t frequency, milliseconds;
	uint8_t identity;
	unsigned deferred, valid;
} host_config;
static struct ar_rf_lifecycle_info lifecycle = {.version = 1};
static struct ar_fch_monitor fch_monitor;
static struct ar_rf_recovery_info recovery = {.version = 2};
static unsigned recovery_reset_pending;
static void recover_host(void);
static int automatic_recovery(void) { return receive_operation == 76 && !receive_milliseconds; }
static int start_prepared_receive(void);
static void stop_receive(void);
static int configure_monitor(uint32_t frequency, uint32_t milliseconds, uint8_t identity_hash,
	unsigned operation, const uint8_t *channels, size_t length, unsigned deferred);
static uint8_t dev_power_index, dev_sync_ack[5];
static struct ar_dev_tx_context dev_tx;
static struct ar_dev_retx dev_retx;
static struct ar_dev_stream dev_stream;
static struct ar_rf_phy_info phy;
static struct ar_rf_phy_trace phy_trace;
static uint32_t phy_poll_tick;
const struct ar_rf_phy_trace *ar_rf_phy_trace_information(void) { return &phy_trace; }
static struct ar_dev_mcs dev_mcs;
static struct ar_rf_mcs_info mcs_info;
static uint32_t mcs_fch_keys[25];
static uint8_t mcs_sync_mmio;
static struct ar_rf_sync_info sync_info;
const struct ar_rf_sync_info *ar_rf_sync_information(void) { return &sync_info; }
static struct ar_rf_traffic_info traffic_info;
const struct ar_rf_traffic_info *ar_rf_traffic_information(void) { return &traffic_info; }
static struct ar_rf_tx_mcs_info tx_mcs_info;
const struct ar_rf_tx_mcs_info *ar_rf_tx_mcs_information(void) { return &tx_mcs_info; }
const struct ar_rf_mcs_info *ar_rf_mcs_information(void)
{
	mcs_info.samples = dev_mcs.samples; mcs_info.requests = dev_mcs.requests;
	mcs_info.acknowledgements = dev_mcs.acknowledgements; mcs_info.unsolicited = dev_mcs.unsolicited;
	mcs_info.timeouts = dev_mcs.timeouts; mcs_info.advertisements = dev_mcs.advertisements;
	mcs_info.cancellations = dev_mcs.cancellations;
	mcs_info.state = dev_mcs.connected | dev_mcs.ready << 1 | dev_mcs.started << 2 |
		dev_mcs.pending << 3 | dev_mcs.phase << 4 | dev_mcs.advertised << 5 | dev_mcs.direction << 6;
	mcs_info.current = dev_mcs.current < dev_mcs.config.count ? dev_mcs.config.rows[dev_mcs.current].mcs : 25;
	mcs_info.previous = dev_mcs.previous < dev_mcs.config.count ? dev_mcs.config.rows[dev_mcs.previous].mcs : 25;
	mcs_info.observed = dev_mcs.observed; mcs_info.average_snr = dev_mcs.average_snr;
	mcs_info.bad_samples = dev_mcs.bad_samples; mcs_info.recent_bad = dev_mcs.recent_bad;
	mcs_info.sample_ms = dev_mcs.sample_ms; mcs_info.request_ms = dev_mcs.request_ms;
	mcs_info.direction_ms = dev_mcs.direction_ms; mcs_info.sync_mmio = mcs_sync_mmio;
	return &mcs_info;
}
static uint8_t phy_header_valid[10], phy_eop_seen[10];
const struct ar_rf_phy_info *ar_rf_phy_information(void) { return &phy; }
static ar_rf_payload_callback payload_callback;
static ar_rf_tx_peek_callback tx_peek_callback;
static ar_rf_tx_commit_callback tx_commit_callback;
static void *tx_context;
static struct ar_rf_tx_payload_info tx_payload_info;

void ar_rf_set_tx_callbacks(ar_rf_tx_peek_callback peek, ar_rf_tx_commit_callback commit, void *context)
{
	tx_peek_callback = peek; tx_commit_callback = commit; tx_context = context;
}

const struct ar_rf_tx_payload_info *ar_rf_tx_payload_information(void)
{
	return &tx_payload_info;
}
static void *payload_context;

void ar_rf_set_payload_callback(ar_rf_payload_callback callback, void *context)
{
	payload_callback = callback; payload_context = context;
}
static unsigned dev_active;

#ifdef AR_RF_SIMULATED
extern uint32_t ar_rf_test_read(uint32_t address);
extern void ar_rf_test_write(uint32_t address, uint32_t value);
extern void ar_rf_test_start_dma(uint8_t *buffer, unsigned length);
#define read32 ar_rf_test_read
#define write32 ar_rf_test_write
static uint8_t read8(uint32_t address) { return read32(address & ~3u) >> ((address & 3) * 8); }
static void write8(uint32_t address, uint8_t value)
{
	unsigned shift = (address & 3) * 8;
	write32(address & ~3u, (read32(address & ~3u) & ~(0xffu << shift)) | (uint32_t)value << shift);
}
static void barrier(void) { }
#else
static uint32_t read32(uint32_t address) { return *(volatile uint32_t *)(uintptr_t)address; }
static void write32(uint32_t address, uint32_t value) { *(volatile uint32_t *)(uintptr_t)address = value; }
static uint8_t read8(uint32_t address) { return *(volatile uint8_t *)(uintptr_t)address; }
static void write8(uint32_t address, uint8_t value) { *(volatile uint8_t *)(uintptr_t)address = value; }
static void barrier(void) { __asm__ volatile("fence rw,rw" ::: "memory"); }
#endif

/* The hardware counter is 64 bits at 40 MHz. Read a coherent high/low pair
 * rather than dividing a truncated 32-bit difference (wraps every 107 s).
 * Diagnostic microsecond fields retain their ABI and wrap at 2^32 us; control
 * timers use milliseconds directly, never a truncated microsecond timestamp. */
static uint64_t timer_ticks(void)
{
	uint32_t high, low, again;
	do {
		high = read32(TIMER + 4); low = read32(TIMER); again = read32(TIMER + 4);
	} while (high != again);
	return (uint64_t)high << 32 | low;
}
static uint64_t elapsed_ticks(void) { return timer_ticks() - receive_started; }
static uint32_t elapsed_us(void) { return (uint32_t)(elapsed_ticks() / 40); }
static uint32_t elapsed_ms(void) { return (uint32_t)(elapsed_ticks() / 40000); }

static uint32_t le32(const uint8_t *data)
{
	return data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}
static void update(uint32_t address, uint32_t clear, uint32_t set)
{
	uint32_t value = (read32(address) & ~clear) | set;
	write32(address, value);
	barrier();
	if (!commands_ready) { return; }
	/* Mirror the profile fields consumed by the original metadata getters.
	 * Updates preserve first-match behavior and unrelated entry fields. */
	unsigned offset = address - BB;
	if (offset >= 0x9a0 && offset <= 0x9f8 && !(offset & 7)) {
		unsigned user = (offset - 0x9a0) / 8;
		if (clear & 0x700) { (void)ar_command_update(&command_program,1,user,1,0,value >> 8,0); }
		if (clear & 0xc7) {
			unsigned type = value & 7, mode = value >> 6 & 3;
			unsigned flag = (type == 1 && mode < 2) || (type == 2 && mode >= 2);
			(void)ar_command_update(&command_program,1,user,1,4,0,flag);
		}
	} else if (offset >= 0xb00 && offset <= 0xb2c && !(offset & 3) && offset != 0xb28 && (clear & 7)) {
		(void)ar_command_update(&command_program,1,(offset - 0xb00) / 4,0,0,value,0);
	} else if (offset == 0xb60 && (clear & 0x07000000)) {
		(void)ar_command_update(&command_program,1,10,0,0,value >> 24,0);
	}
}
static void field(uint32_t encoded, uint32_t value)
{
	unsigned width = encoded & 31, shift = (encoded >> 8) & 31;
	uint32_t mask = encoded & 0xe0 ? UINT32_MAX : (UINT32_C(1) << width) - 1;
	update(BB + (encoded >> 16), mask << shift, (value & mask) << shift);
}
static void sync_tx_mcs(const uint8_t control[7])
{
	/* 0x2300a2 -> 0x229bd4 -> PHY107: mode0 DEV accepts slot0;
	 * low five payload bits select TX MCS, irrespective of bandwidth bits
	 * when an MCS is present. 0x24d2a4 applies increases immediately. */
	unsigned mcs = control[4] & 31;
	tx_mcs_info.requests++; tx_mcs_info.requested = mcs;
	if (control[3] || mcs >= 16) { tx_mcs_info.rejected++; return; }
	if (mcs == tx_mcs_info.current) { tx_mcs_info.unchanged++; return; }
	/* The stock decrease path has a six-interleaver drain and ownership
	 * changes. Do not replace that contract with an immediate field write. */
	if (mcs < tx_mcs_info.current) { tx_mcs_info.downgrades++; return; }
	uint32_t key = mcs_fch_keys[mcs], word = read32(BB + 0xb00);
	unsigned modulation = (key >> 16) & 255, code = (key >> 8) & 255, rate = key & 255;
	if ((key >> 24) || modulation > 4 || code > 2 || rate > 2) { tx_mcs_info.rejected++; return; }
	/* 0x221610: code length follows bandwidth/interleave and modulation.
	 * Normal keys leave a TX mode other than2 unchanged; mode2 becomes1. */
	static const uint8_t bits[] = {1,2,4,6,8};
	unsigned exponent = (word & 7) + 2 - rate + ((word >> 18) & 7);
	unsigned value = ((unsigned)bits[modulation] << exponent) >> 2;
	unsigned code_length;
	if (value == 2) { code_length = 0; }
	else if (value == 4) { code_length = 1; }
	else if (value && !(value & 7)) { code_length = 2; }
	else { tx_mcs_info.rejected++; return; }
	tx_mcs_info.before = word; tx_mcs_info.key = key;
	if (((word >> 22) & 3) == 2) { field(0xb001602, 1); tx_mcs_info.mode_changed++; }
	field(0xb000803, modulation); field(0xb000c04, code + code_length * 3); field(0xb001002, rate);
	tx_mcs_info.after = read32(BB + 0xb00);
	mcs_event(0, mcs, tx_mcs_info.current); tx_mcs_info.current = mcs;
	tx_mcs_info.last_apply_us = elapsed_us();
	if (!tx_mcs_info.applied++) { tx_mcs_info.first_apply_us = tx_mcs_info.last_apply_us; }
}
static void sync_apply(void)
{
	const uint8_t *control = sync_info.last_control;
	if (receive_operation >= 71) { traffic_info.commands[control[1] & 15].dispatched++; }
	if (receive_operation >= 72 && (control[1] & 15) == 3) { sync_tx_mcs(control); return; }
	if ((control[1] & 15) != 6) { sync_info.unsupported++; return; }
	unsigned user = control[3] & 15, transmit = !!(control[3] & 16);
	if (user > 9) { sync_info.invalid_users++; return; }
	/* Original 0x236d9a: incoming bit4=0 sets RX identity, but not in
	 * DEV idle or flag0x40 state. Native admits only the supported normal
	 * mode0 link; unsupported remote-sync flags never enter state2. */
	if (!transmit && !receiver.dev_state) { sync_info.state_skips++; return; }
	uint32_t encoded = transmit ? (((user + 2) & ~3u) + 0xb7c) << 16 |
		((user + 2) & 3) << 11 | 8 : (user + 0x1b4) * 0x80000 | 8;
	uint32_t shared = transmit ? 0xb601008 : 0xdf00008;
	sync_info.user = user; sync_info.identity = control[4]; sync_info.transmit = transmit;
	sync_info.field_before = read32(BB + (encoded >> 16));
	sync_info.shared_before = read32(BB + (shared >> 16));
	field(encoded, control[4]);
	if (user == 0 || user == 8) { field(shared, control[4]); }
	sync_info.field_after = read32(BB + (encoded >> 16));
	sync_info.shared_after = read32(BB + (shared >> 16));
	sync_info.last_apply_us = elapsed_us();
	if (!sync_info.rx_writes && !sync_info.tx_writes) { sync_info.first_apply_us = sync_info.last_apply_us; }
	if (transmit) { sync_info.tx_writes++; } else { sync_info.rx_writes++; }
}
static void sync_receive(const uint8_t control[7])
{
	/* 0x248b40 validates lengths before replacing the shared cache. The
	 * checksum covers all seven bytes, including unused payload tails. */
	static const uint8_t lengths[10] = {255,1,4,2,3,3,2,4,4,0};
	unsigned command = control[1] & 15, length = (control[2] >> 1) & 7;
	unsigned ttl = (control[1] >> 4) | (control[2] & 1) << 4;
	if (command > 9 || (lengths[command] != 255 && length != lengths[command]) ||
	    (command == 8 && (control[6] & 12))) { sync_info.rejected++; return; }
	sync_info.accepted++; memcpy(sync_info.last_control, control, 7);
	if (receive_operation >= 71) {
		struct ar_rf_command_observation *item = &traffic_info.commands[command];
		uint32_t elapsed = elapsed_us();
		if (!item->accepted) { item->first_us = elapsed; memcpy(item->first, control, 7); }
		item->accepted++; item->last_us = elapsed; memcpy(item->last, control, 7);
	}
	if (command == 6 && !sync_info.identity_commands++) { memcpy(sync_info.first_identity, control, 7); }
	/* Command7 consumes header bits as time, not a TTL countdown. It does
	 * not cancel an existing shared timer; its cache replacement prevents
	 * the previous command from being dispatched when that timer expires. */
	if (command == 7) {
		ar_ap_time_receive(&ap_time, control, (uint32_t)(timer_ticks() / 40000));
		return;
	}
	if (!command) { return; }
	if (ttl == 1 || ttl == 31) {
		if (ttl == 1) { sync_info.pending = 0; }
		sync_info.immediate++; sync_apply();
	} else if (ttl > 1) {
		sync_info.deadline_ms = elapsed_ms() +
			((ttl - 1) * receiver.search_frame_us) / 1000 + 4;
		sync_info.pending = 1; sync_info.armed++;
	}
}
static void sync_poll(uint32_t ms)
{
	if (!sync_info.pending || (int32_t)(ms - sync_info.deadline_ms) < 0) { return; }
	sync_info.pending = 0; sync_info.timer_fired++;
	unsigned ttl = (sync_info.last_control[1] >> 4) | (sync_info.last_control[2] & 1) << 4;
	unsigned command = sync_info.last_control[1] & 15;
	/* Command7 has no timer callback in the original descriptor table. */
	if (ttl && command && command != 7) { sync_apply(); }
}
static void read_control_words(uint32_t *words)
{
	static const uint16_t offsets[] = {0xf04, 0xf08, 0xf0c, 0xf10, 0xf34, 0xf38, 0xf3c};
	for (unsigned word = 0; word < 7; word++) { words[word] = read32(BB + offsets[word]); }
}
static void read_control_header(uint32_t *words)
{
	/* Stock 0x22d30a reads this bank, restoring the caller's selector. */
	uint32_t old = read32(BB + 0xbb0);
	field(0xbb00701, 1);
	read_control_words(words);
	field(0xbb00701, (old >> 7) & 1);
}
static uint32_t read_header_status(void)
{
	uint32_t old = read32(BB + 0xbb0);
	field(0xbb00701, 0);
	uint32_t value = read32(BB + 0xf00);
	field(0xbb00701, (old >> 7) & 1);
	return value;
}
static int failure(int code)
{
	information.error = (uint32_t)code;
	return code;
}
static int wait_bits(uint32_t address, uint32_t mask, uint32_t expected, uint32_t us)
{
	uint32_t started = read32(TIMER);
	/* Both the timer and an iteration cap bound failed-clock/RTL cases. */
	for (unsigned iteration = 0; iteration < 1000000; iteration++) {
		if ((read32(address) & mask) == expected) { return 0; }
		if ((uint32_t)(read32(TIMER) - started) >= us * 40) { return -110; }
	}
	return -110;
}
static int delay_us(uint32_t us)
{
	uint32_t started = read32(TIMER);
	for (unsigned iteration = 0; iteration < 1000000; iteration++) {
		if ((uint32_t)(read32(TIMER) - started) >= us * 40) { return 0; }
	}
	return -110;
}

static int otp_word(unsigned offset, uint32_t *word)
{
	write32(OTP + 0x98, offset / 4);
	barrier();
	uint32_t started = read32(TIMER);
	for (unsigned iteration = 0; iteration < 100000; iteration++) {
		uint32_t status = read32(OTP + 0xa4);
		information.otp_status = status;
		if (status & 4) {
			*word = read32(OTP + 0x9c);
			write32(OTP + 0xa4, 0);
			return 0;
		}
		if (status & 3) { write32(OTP + 0xa4, 0); return failure(-5); }
		if ((uint32_t)(read32(TIMER) - started) >= 80000) { break; }
	}
	return failure(-110);
}

int ar_rf_probe(void)
{
	memset(&information, 0, sizeof information);
	commands_ready = 0;
	receive_operation = 0; host_prepared = 0;
	command_state.cached_bank = 3; command_state.board_band = -1;
	pins_configured = 0;
	/* Stock Ascent VRX application entry sets these two system-control bits
	 * before baseband initialization (0x20abba); preserve all other bits. */
	update(0x0a1060c8, 0, 3);
	/* Measured clock table: ID 12 is CLOCK+0x80 bit 1. */
	update(CLOCK + 0x80, 0, 2);
	for (uint32_t offset = 0x100; offset < 0x300; offset += 4) { write32(OTP + offset, 0); }
	barrier();
	uint32_t word;
	int result = otp_word(8, &word);
	if (!result) { information.chip_profile = word >> 16 & 255; information.phase = AR_RF_PROBED; }
	return result;
}

int ar_rf_board_identity(uint8_t mac[4])
{
	if (!mac || information.phase != AR_RF_PROBED) { return -22; }
	uint32_t first, low, high;
	int result = otp_word(12, &first);
	if (!result) { result = otp_word(24, &low); }
	if (!result) { result = otp_word(28, &high); }
	if (result) { return result; }
	/* Original 204208 consumes bytes 4 and 16..19 of the OTP block at 10.
	 * Assemble unaligned bytes explicitly; no OTP programming operation. */
	uint32_t wafer = first >> 16 & 255, serial = low >> 16 | high << 16;
	if (!wafer && !serial) { return -2; }
	uint32_t packed = (serial << 6) | (wafer & 63);
	for (unsigned i = 0; i < 4; i++) { mac[i] = packed >> (8 * i); }
	return 0;
}

int ar_rf_setup_pins(const uint8_t *records, size_t length)
{
	if (information.phase != AR_RF_PROBED || !records || length != 40) { return failure(-22); }
	unsigned seen = 0;
	for (size_t i = 0; i < length; i += 4) {
		unsigned bank = records[i], pin = records[i + 1];
		if (bank > 1 || pin > 4 || records[i + 2] > 15 || records[i + 3]) { return failure(-22); }
		unsigned bit = 1u << (bank * 5 + pin);
		if (seen & bit) { return failure(-22); }
		seen |= bit;
	}
	update(CLOCK + 0x200, 0, 1u << 25); /* Measured GPIO clock ID 21. */
	for (size_t i = 0; i < length; i += 4) {
		unsigned bank = records[i], pin = records[i + 1], shift = pin * 4;
		update(0x0a10a02c + bank * 4, 15u << shift, (uint32_t)records[i + 2] << shift);
		if (!bank) { update(0x0a090140, 3u << (pin * 2), 2u << (pin * 2)); }
	}
	pins_configured = 1;
	return 0;
}

static void configure_clocks(void)
{
	/* Board startup's inactive-bank clock configuration, preserving other bits. */
	uint32_t value = read32(CLOCK + 4);
	write32(CLOCK + 4, value & 0x80000000 ? (value & 0x7ffff880) | 0x1101 : (value & 0xf880ffff) | 0x91010000);
	value = read32(CLOCK + 0x14);
	write32(CLOCK + 0x14, value & 0x80000000 ? (value & 0x7ffffc80) | 0x1100 : (value & 0xfc80ffff) | 0x91000000);
	update(CLOCK, 0, 1u << 12); /* Clock ID 0: BB root. */
	update(CLOCK + 8, 0, 1u << 12); /* Clock ID 1: PSRAM domain. */
}

int ar_rf_load_defaults(const uint8_t *records, size_t length)
{
	if ((information.phase != AR_RF_PROBED && information.phase != AR_RF_DEFAULTS) || information.chip_profile != 1) {
		return failure(-95);
	}
	if (!records || !length || length % 8 || information.default_writes + length / 8 > 704) { return failure(-22); }
	/* Validate the entire batch before issuing any peripheral write. */
	for (size_t i = 0; i < length; i += 8) {
		unsigned offset = records[i] | (unsigned)records[i + 1] << 8;
		if (offset % 4 || offset > 0xdfc || records[i + 2] || records[i + 3]) { return failure(-22); }
	}
	if (!information.default_writes) { configure_clocks(); }
	command_state.cached_bank = 3; command_state.board_band = -1;
	for (size_t i = 0; i < length; i += 8) {
		unsigned offset = records[i] | (unsigned)records[i + 1] << 8;
		write32(BB + offset, le32(records + i + 4));
		information.default_writes++;
	}
	barrier();
	information.phase = AR_RF_DEFAULTS;
	information.error = 0;
	return 0;
}

static int hold_engines(void)
{
	field(0x8000001, 1); field(0x8000301, 1);
	field(0x80c0201, 1); field(0x80c0301, 1);
	return wait_bits(BB + 0xfa8, 0x300, 0x300, 2000);
}
static void release_engines(void)
{
	field(0x80c0201, 0); field(0x80c0301, 0);
	field(0x8000301, 0); field(0x8000001, 0);
}
static int idle(void)
{
	int result = hold_engines();
	release_engines();
	return result;
}

int ar_rf_calibrate(void)
{
	if (information.phase != AR_RF_DEFAULTS || information.default_writes != 704) { return failure(-22); }
	command_state.cached_bank = 3; command_state.board_band = -1;
	for (unsigned user = 0; user < 11; user++) {
		uint32_t offset = ((user >> 1) + 0x2ca) * 0x40000;
		field(user == 10 ? 0xb600d01 : offset | ((user & 1) * 0x1000 + 0x700) | 1, 1);
		field(user == 10 ? 0xb701c04 : offset | ((user & 1) * 0x10 + 8) * 0x100 | 4, 12);
		field(user == 10 ? 0xb701804 : offset | (user & 1) << 12 | 4, 12);
		if (user != 10) { field((user * 0x40000 + 0xb000000) | 0x1808, 20); }
	}
	field(0x81701, 1); field(0x81701, 0);
	if (wait_bits(BB + 0xe0, 0xff, 0x6f, 100000)) { return failure(-110); }
	field(0x1c1901, 1); field(0x1c1901, 0);
	if (wait_bits(BB + 0xe0, 0xff000000, 0xff000000, 100000)) { return failure(-110); }
	/* Band 2 (5 GHz) selector and five-byte calibration control value. */
	field(0x4840802, 2); field(0x4900808, 10);
	static const uint8_t control[5] = {1, 0x13, 0, 0, 0};
	for (unsigned i = 0; i < sizeof control; i++) {
		unsigned offset = 0x4d0 + i, shift = (offset & 3) * 8;
		update(BB + (offset & ~3u), 0xffu << shift, (uint32_t)control[i] << shift);
	}
	if (idle()) { return failure(-110); }
	uint8_t history[3][4] = {{0}};
	for (unsigned round = 0; round < 100; round++) {
		field(0x3281008, 0xff); field(0x3001008, 0xa0); field(0x3281008, 0);
		uint32_t started = read32(TIMER), state = 0;
		for (unsigned iteration = 0; iteration < 1000000; iteration++) {
			state = read32(BB + 0x360) & 0xff;
			if (state == 6 || state == 7) { break; }
			if ((uint32_t)(read32(TIMER) - started) >= 4000000) { break; }
		}
		information.calibration_status = state;
		information.calibration_rounds = round + 1;
		if (state != 6 && state != 7) { field(0x8000001, 1); return failure(-110); }
		uint32_t left = read32(BB + 0x4f0), right = read32(BB + 0x4f4);
		uint8_t *current = history[round % 3];
		current[0] = left; current[1] = left >> 8; current[2] = right >> 8; current[3] = right >> 16;
		if (round >= 2) {
			unsigned maximum = 0;
			for (unsigned sample = 0; sample < 2; sample++) {
				for (unsigned component = 0; component < 4; component++) {
					int a = (int8_t)history[sample][component], b = (int8_t)history[sample + 1][component];
					unsigned difference = (unsigned)(a > b ? a - b : b - a);
					if (difference > maximum) { maximum = difference; }
				}
			}
			if (maximum < 3) {
				memcpy(calibration, current, 4);
				calibration[4] = (current[0] & 0x80) | ((current[1] >> 1) & 0x40) |
					((current[2] >> 4) & 8) | ((current[3] >> 5) & 4);
				field(0x8000001, 1);
				information.phase = AR_RF_CALIBRATED;
				information.error = 0;
				return 0;
			}
		}
	}
	field(0x8000001, 1);
	return failure(-5);
}

int ar_rf_setup_psram(void)
{
	if (information.phase != AR_RF_CALIBRATED) { return failure(-22); }
	update(CLOCK + 8, 0, 1u << 28);
	update(CLOCK + 8, ~UINT32_C(0xfc80ffff), 0x1010000);
	field(0x8081901, 0); field(0xcd00001, 0); field(0xcdc0801, 0);
	field(0x8000301, 1); field(0x8000001, 1); field(0x8000201, 1);
	field(0x8000301, 0); field(0x8000001, 0);
	write32(BB + 0xcd0, 0x80250700); write32(BB + 0xcd4, 0x2800200);
	write32(BB + 0xcd8, 0x0c030000); write32(BB + 0xcdc, 0xee999000);
	write32(BB + 0xce0, 0xaaaa); write32(BB + 0xce4, UINT32_MAX);
	write32(BB + 0xce8, 0x120); write32(BB + 0xcec, 0x40000000);
	barrier();
	if (delay_us(1000)) { return failure(-110); }
	field(0xcd00001, 1);
	if (delay_us(1000)) { return failure(-110); }
	field(0xcd00501, 1);
	if (delay_us(10000)) { return failure(-110); }
	uint32_t taps = read32(BB + 0xfa0);
	unsigned low = (taps >> 8) & 15, high = (taps >> 12) & 15;
	field(0xcd41f01, 1);
	field(0xcd41008, low <= high ? ((low + high) >> 1) | 0x80 : 0x89);
	for (unsigned attempt = 0; attempt < 4; attempt++) {
		if (delay_us(1000)) { return failure(-110); }
		field(0xcdc1010, 0xaa99);
		if (delay_us(1000)) { return failure(-110); }
		field(0xcdc0801, 1);
		if (delay_us(5000)) { return failure(-110); }
		information.psram_status = read32(BB + 0xfa4);
		if (information.psram_status == 0xaa991007) {
			field(0xcd00501, 0); field(0xcdc0801, 0);
			field(0x8000001, 1); field(0x8000201, 1); field(0x80c0301, 1);
			int idle_result = wait_bits(BB + 0xfa8, 0x200, 0x200, 2000);
			field(0x80c0301, 0); field(0x8000201, 0);
			if (idle_result) { return failure(idle_result); }
			information.phase = AR_RF_PSRAM;
			information.error = 0;
			return 0;
		}
		field(0xcd41008, 0x89);
	}
	return failure(-5);
}

const struct ar_rf_info *ar_rf_information(void) { return &information; }

int ar_rf_pll_word(uint32_t frequency, uint64_t *word)
{
	if (!word || frequency > 7500000) { return -22; }
	for (unsigned bucket = 0; bucket < 2; bucket++) {
		uint32_t low = bucket ? 4860000 : 3500000, high = bucket ? 7500000 : 4860000;
		for (unsigned divider = 0; divider < 8; divider++, low >>= 1, high >>= 1) {
			uint32_t vco = frequency << (divider + 1);
			if (frequency > low && frequency <= high && vco >= 7000000 && vco <= 15000000) {
				uint32_t integer = (uint32_t)(vco / 40000), remainder = (uint32_t)(vco % 40000);
				uint32_t control = ((bucket ? 0 : 1) << 7) | (divider << 4) | (divider << 1) | (integer >> 8);
				uint32_t fraction = 0;
				for (unsigned bit = 0; bit < 24; bit++) {
					remainder <<= 1; fraction <<= 1;
					if (remainder >= 40000) { remainder -= 40000; fraction |= 1; }
				}
				*word = (uint64_t)(control & 255) << 32 | (uint64_t)(integer & 255) << 24 |
					fraction;
				return 0;
			}
		}
	}
	return -22;
}

static void serial_word(unsigned offset, uint64_t word)
{
	uint32_t high = word >> 32, low = (uint32_t)word;
	uint8_t bytes[5] = {high, low >> 24, low >> 16, low >> 8, low};
	for (unsigned byte = 0; byte < 5; byte++) {
		unsigned address = offset + byte, shift = (address & 3) * 8;
		update(BB + (address & ~3u), 0xffu << shift, (uint32_t)bytes[byte] << shift);
	}
}
static void tune_context_plls(uint64_t pll)
{
	/* The 40-bit values use nonuniform subword placement, not a five-byte
	 * stride. Decode the offset AND bit shift of the corroborated field map. */
	static const uint16_t shadows[12] = {0x93a,0x940,0x945,0x94a,0x950,0x955,
		0x95a,0x960,0x905,0x96a,0x90a,0x90a};
	static const uint16_t other_shadows[12] = {0x910,0x915,0x91a,0x920,0x925,0x92a,
		0x930,0x935,0x900,0x965,0x90a,0x90a};
	for (unsigned user = 0; user < 12; user++) {
		serial_word(shadows[user], pll);
		serial_word(other_shadows[user], pll);
	}
}
static void configure_receive_context(uint64_t pll)
{
	/* Both direction banks must follow the selected channel, including the
	 * slot banks selected after admission. Keep tuning separate from profile
	 * setup so search cannot overwrite PHY state or DMA ownership. */
	tune_context_plls(pll);
	for (unsigned user = 0; user < 12; user++) {
		uint32_t phy = (0x9a0 + user * 8) << 16;
		unsigned broadcast = user == 8, search = user >= 10;
		field(phy | 3, search ? 0 : broadcast ? 5 : 3);
		field(phy | 0x803, broadcast || user == 11 ? 4 : user == 10 ? 3 : 1);
		field(phy | 0xd03, search ? 0 : broadcast ? 6 : 5);
		field(phy | 0x301, 1);
		field(phy | 0x401, 0);
		field(phy | 0x501, !search);
		field(phy | 0x602, 2);
		field(phy | 0x1808, broadcast ? 0x20 : 0x3c);
		field(phy | 0x1401, 1); field(phy | 0x1701, 1);
		uint32_t band = (0x9a4 + user * 8) << 16;
		field(band | 0x1903, 4); field(band | 0x1201, 1); field(band | 0x1601, 1);
		field(band | 0x1d03, 4); field(band | 0x1301, 1); field(band | 0x1701, 1);
		field(band | 8, broadcast ? 0 : user == 0 ? 0x27 : 0x4e);
		field(band | 0x806, broadcast ? 15 : user == 0 ? 34 : 23);
		uint32_t power = (0xda0 + user * 8) << 16;
		field(power | 0x1808, broadcast ? 0 : user == 0 ? 0x27 : 0x4e);
		field((power + 0x40000) | 6, broadcast ? 14 : user == 0 ? 34 : 22);
		if (user < 10) {
			/* Corroborated static fields in the other per-user PHY bank, not
			 * packet contents, live gain status or DMA descriptors. */
			uint32_t other_phy = (0xb00 + user * 4) << 16;
			field(other_phy | 3, broadcast ? 2 : 1);
			field(other_phy | 0x301, 0); field(other_phy | 0x401, broadcast);
			field(other_phy | 0x501, 1); field(other_phy | 0x1501, 0);
			field(other_phy | 0x1203, 2); field(other_phy | 0x1602, user != 0);
			field(other_phy | 0x602, 0); field(other_phy | 0x1e01, broadcast);
			field(other_phy | 0x803, 0); field(other_phy | 0xc04, user == 0 || broadcast ? 6 : 3);
			field(other_phy | 0x1002, user != 0);
			field(((user >> 1) + 0x2ca) * 0x40000 | (user & 1) << 12 | 0x601, 1);
		}
	}
	field(0xa681e02, 0); field(0xc040e02, 0);
}
static unsigned irq_vector(unsigned index) { return index + (index < 4 ? 72 : 76); }
static void irq_ack(unsigned index)
{
	field(0xaf40001 | index << 8, 1); field(0xaf40001 | index << 8, 0);
	uint32_t address = 0xe0801000u + irq_vector(index) * 4;
	write8(address, read8(address) & ~1u);
}

static void phy_trace_begin(struct ar_rf_phy_event *event, unsigned index)
{
	event->begin_us = elapsed_us();
	event->flags = index | (phy_eop_seen[8] ? 1u << 8 : 0) |
		(phy_header_valid[8] ? 1u << 9 : 0) | 1u << 10;
	event->fbc = read32(BB + 0xfbc);
	uint32_t old = read32(BB + 0xbb0);
	field(0xbb00701, 0);
	event->fch_before = read32(BB + 0xf00); event->errors = read32(BB + 0xf28);
	field(0xbb00701, (old >> 7) & 1);
	old = read32(BB + 0xc00); field(0xc000701, 0);
	event->snr = read32(BB + 0xee0); field(0xc000701, (old >> 7) & 1);
	event->command = read32(BB + 0xe6c);
	event->pending = (read8(0xe0801000u + irq_vector(12) * 4) & 1) << 12 |
		(read8(0xe0801000u + irq_vector(14) * 4) & 1) << 14;
	event->samples = phy.samples; event->mac_valid = receiver.mac_valid_packets;
}

static void phy_trace_bucket(unsigned bucket, const struct ar_rf_phy_event *event)
{
	struct ar_rf_phy_bucket *b = &phy_trace.buckets[bucket];
	uint16_t snr = event->snr, errors = event->errors;
	if (!b->count || snr < b->snr_min) { b->snr_min = snr; }
	if (!b->count || snr > b->snr_max) { b->snr_max = snr; }
	if (!b->count) { b->first_us = event->begin_us; }
	b->count++; b->snr_sum += snr; b->error_sum += errors;
	b->zero_errors += !errors; b->last_us = event->begin_us;
}

static void phy_trace_finish(struct ar_rf_phy_event *event, unsigned index)
{
	uint32_t old = read32(BB + 0xbb0); field(0xbb00701, 0);
	event->fch_after = read32(BB + 0xf00); field(0xbb00701, (old >> 7) & 1);
	if (event->fch_before & 0x80) { event->flags |= 1u << 11; }
	if (receiver.mac_valid_packets != event->mac_valid) { event->flags |= 1u << 12; }
	if (phy.samples != event->samples) { event->flags |= 1u << 13; }
	event->samples = phy.samples; event->mac_valid = receiver.mac_valid_packets;
	event->end_us = elapsed_us();
	uint32_t elapsed = event->end_us - event->begin_us;
	if (elapsed > phy_trace.max_handler_us) { phy_trace.max_handler_us = elapsed; }
	phy_trace.header_changed += event->fch_before != event->fch_after;
	phy_trace.simultaneous_pending += event->pending == ((1u << 12) | (1u << 14));
	if (index == 12) {
		phy_trace_bucket(event->flags & (1u << 8) ? 1 : 0, event);
		if (event->flags & (1u << 9)) { phy_trace_bucket(2, event); }
	} else { phy_trace_bucket(index == 13 ? 5 : event->fch_before & 0x80 ? 3 : 4, event); }
	if (phy_trace.first_count < 32) { phy_trace.first[phy_trace.first_count++] = *event; }
	phy_trace.last[phy_trace.last_head] = *event;
	phy_trace.last_head = (phy_trace.last_head + 1) & 31;
	if (phy_trace.last_count < 32) { phy_trace.last_count++; }
	phy_trace.events++;
}

static void receive_event(void)
{
	receiver.receive_events++;
	uint32_t bitmap = read32(BB + 0xf88) & 0x3ff;
	unsigned user = 0;
	while (user < 10 && !(bitmap & (1u << user))) { user++; }
	if (user == 10) { receiver.invalid_packets++; return; }
	field(0xcec0004, user);
	if (user < 8) {
		uint32_t bits = read32(BB + 0xcf0) >> 24, bit = 1u << user;
		field(0xcf01808, bits | bit); field(0xcf01808, bits & ~bit);
	} else {
		uint32_t bits = read32(BB + 0xcf4) & 3, bit = 1u << (user - 8);
		field(0xcf40002, bits | bit); field(0xcf40002, bits & ~bit);
	}
	uint32_t metadata = read32(BB + 0xf90), length = metadata & 0xffff;
	if ((metadata & (1u << 29)) || !length || receive_pending) {
		receiver.invalid_packets++;
		field(0xd540001 | (user + 8) << 8, 1); field(0xd540001 | (user + 8) << 8, 0);
		return;
	}
	receive_length = length; receive_user = user; receive_metadata = metadata; receive_pending = 1;
	barrier();
	field(0xbec0020, (uint32_t)(uintptr_t)receive_buffers[receive_head]);
	field(0xbe81801, 1); field(0xbe81801, 0);
#ifdef AR_RF_SIMULATED
	ar_rf_test_start_dma(receive_buffers[receive_head], receive_length);
#endif
}

static void dev_admit(const struct ar_dev_control *control)
{
	if (receive_operation < 54 || dev_active || receiver.acquisition_ready) { return; }
	struct ar_dev_acquisition plan;
	receiver.admission_attempts++;
	int result = ar_dev_idle_evaluate(&dev_config, control, &plan);
	/* Full retuning/profile negotiation is not yet implemented. The tested
	 * acquisition baseline uses the same frequency in both directions. */
	if (!result && plan.transmit_channel != plan.receive_channel) { result = AR_MAC_UNSUPPORTED; }
	receiver.last_admission_result = (uint32_t)result;
	if (result) { receiver.admission_rejects++; return; }
	receiver.acquisition_ready = 1;
	memcpy(receiver.accepted_peer, plan.peer, 4);
	receiver.assigned_slot = 8; /* AP login has not assigned a slot. */
	link_event(1, receiver.dev_state); receiver.dev_state = 1;
	ar_dev_tx_init(&dev_tx);
	/* Mode-0 idle -> lock prerequisites (0x22d996/0x22d71c). This is a
	 * bounded first transition, not the subsequent login/MCS state machine. */
	field(0xb001602, plan.control_field_b00);
	field(0xbf00002, 1); /* 0x222bde(8,2) maps to physical user 0. */
	receiver.search_suspended = 1;
	/* Both configured TX profiles are below bandwidth code 4. This is the
	 * corroborated A48 value before those TX commands, not the RX8 profile. */
	field(0xa480020, 0);
	field(0xb7c1008, plan.peer_hash); field(0xb601008, plan.peer_hash);
	field(0x8000201, 1); field(0x80c0301, 1);
	result = wait_bits(BB + 0xfa8, 0x200, 0x200, 2000);
	field(0x80c0301, 0);
	receiver.transition_elapsed_us = elapsed_us();
	if (result) {
		receiver.transition_status = (uint32_t)result;
		return; /* TX remains held; no retry can falsely claim activation. */
	}
	dev_active = 1;
	if (automatic_recovery()) { ar_fch_monitor_enable(&fch_monitor, 1); }
	field(0x8000201, 0);
	receiver.transition_status = 1;
}

static void dev_tx_finish_segments(void)
{
	field(0xd401808, 255); field(0xd401808, 0);
}

static void dev_mcs_apply(unsigned action)
{
	if (action & AR_DEV_MCS_RESET_RX) {
		/* 23015e -> 22ab4c -> 22a9dc(2). Native waits are bounded and
		 * record failure rather than entering the vendor fatal-reset path. */
		field(0x8000101, 1); field(0x80c0201, 1);
		if (wait_bits(BB + 0xfa8, 0x100, 0x100, 2000)) { mcs_info.reset_errors++; }
		field(0x80c0201, 0); field(0x8000101, 0); mcs_info.rx_resets++;
	}
	if (mcs_sync_mmio && (action & AR_DEV_MCS_SYNC_ON)) { field(0xd301008, 255); }
	if (mcs_sync_mmio && (action & AR_DEV_MCS_SYNC_OFF)) { field(0xd301008, 0); }
	if (action & AR_DEV_MCS_WRITE_FCH) {
		uint8_t sync[8] = {0};
		memcpy(sync, dev_mcs.fch, 7); sync[0] = ar_rf_control_checksum(sync);
		dev_mcs.fch[0] = sync[0];
		mcs_info.fch_words[0] = le32(sync); mcs_info.fch_words[1] = le32(sync + 4);
		write32(BB + 0xb8c, mcs_info.fch_words[0]); write32(BB + 0xb90, mcs_info.fch_words[1]);
		barrier(); mcs_info.fch_writes++;
	}
	if (action & AR_DEV_MCS_APPLY_BUFFER) {
		/* PHY106 -> 222c0a; mode0 logical RX8 maps to physical buffer0. */
		field(0xbf00002, 1); field(0xd081001, 0); mcs_info.buffer_applies++;
		mcs_event(1,
			dev_mcs.current < dev_mcs.config.count ? dev_mcs.config.rows[dev_mcs.current].mcs : 25,
			dev_mcs.previous < dev_mcs.config.count ? dev_mcs.config.rows[dev_mcs.previous].mcs : 25);
		if (!mcs_info.first_ack_ms && dev_mcs.acknowledgements) {
			mcs_info.first_ack_ms = elapsed_ms();
		}
	}
}

static void dev_measure_eop(uint32_t status)
{
	/* 0x2210cc maps raw user9 to logical8; each FCH clears the EOP guard.
	 * This mode-0 path reports only the configured broadcast receiver8. */
	unsigned user = (status >> 20) & 15;
	if (user == 9) { user = 8; }
	if (user != 8) { phy.other_users++; return; }
	if (phy_eop_seen[user]) { phy.duplicates++; return; }
	phy_eop_seen[user] = 1;
	phy.last_fbc = status;
	uint32_t old = read32(BB + 0xbb0);
	field(0xbb00701, 0);
	phy.raw_errors = read32(BB + 0xf08 + user * 4);
	field(0xbb00701, (old >> 7) & 1);
	old = read32(BB + 0xc00);
	field(0xc000701, 0);
	phy.raw_snr = read32(BB + 0xea0 + user * 8);
	phy.raw_aux = read32(BB + 0xea4 + user * 8);
	field(0xc000701, (old >> 7) & 1);
	/* 0x22d158 and 0x22d22a select the gain/offset bank independently. */
	old = read32(BB + 0x808);
	field(0x8081b01, 0);
	phy.raw_gain[0] = read32(BB + 0xe30); phy.raw_gain[1] = read32(BB + 0xe34);
	field(0x8081b01, 1);
	phy.raw_offset[0] = read32(BB + 0xe30); phy.raw_offset[1] = read32(BB + 0xe34);
	field(0x8081b01, (old >> 27) & 1);
	uint16_t errors = phy.raw_errors;
	if (!errors && !phy_header_valid[user]) { errors = 1; phy.forced_errors++; }
	/* Stock leaves sample bytes9..11 as stack residue. Native initializes
	 * those padding bytes to zero, without inventing measurement values. */
	memset(phy.sample, 0, sizeof phy.sample);
	phy.sample[0] = phy.raw_snr; phy.sample[1] = phy.raw_snr >> 8;
	phy.sample[2] = errors; phy.sample[3] = errors >> 8;
	phy.sample[4] = phy.raw_errors >> 16; phy.sample[5] = phy.raw_errors >> 24;
	phy.sample[6] = (phy.raw_gain[0] >> 16) + (phy.raw_gain[0] >> 24);
	phy.sample[7] = phy.raw_gain[1] + (phy.raw_gain[1] >> 8);
	phy.sample[8] = phy.raw_aux;
	/* Exact 0x8204140b at 0x22d28a matched this byte reversal for 1024
	 * random hardware inputs in trial109. No other encoding is inferred. */
	uint32_t offset = __builtin_bswap32((phy.raw_offset[0] >> 16) | (phy.raw_offset[1] << 16));
	for (unsigned i = 0; i < 4; i++) { phy.sample[12 + i] = offset >> (i * 8); }
	uint64_t sample_ticks = elapsed_ticks();
	phy.sample_elapsed_us = (uint32_t)(sample_ticks / 40);
	if (!phy.samples) { memcpy(phy.first_sample, phy.sample, sizeof phy.sample); }
	phy.samples++;
	if (receive_operation >= 67) {
		dev_mcs_apply(ar_dev_mcs_sample(&dev_mcs, (uint32_t)(sample_ticks / 40000), phy.raw_snr, errors));
		if (!mcs_info.first_request_ms && dev_mcs.requests) { mcs_info.first_request_ms = dev_mcs.request_ms; }
	}
}

static void request_recovery(unsigned reason)
{
	if (recovery.pending) { return; }
	recovery.pending = reason;
	/* Stop new submissions immediately. Rebuild only outside packet parsing,
	 * after the current owned RX view has been released. */
	field(0x8000201, 1);
}

static void dev_invalidate(void)
{
	if (automatic_recovery()) {
		recovery.invalidations++; request_recovery(2); return;
	}
	if (receive_operation >= 67) { dev_mcs_apply(ar_dev_mcs_connect(&dev_mcs, 0)); }
	field(0x8000201, 1); field(0xcf41501, 0);
	link_event(0, receiver.dev_state);
	dev_active = 0; receiver.dev_state = 0; receiver.assigned_slot = 8;
	receiver.sync_ack_pending = 0; receiver.link_invalidations++;
}

static void dev_command_observed(const uint8_t *data, size_t length, void *context)
{
	(void)context;
	if (data[1] != 2 && !receiver.first_nonstats_length) {
		receiver.first_nonstats_length = length;
		memcpy(receiver.first_nonstats, data, length);
	}
}

static void dev_observe_compact(const struct ar_mac_frame *frame)
{
	struct ar_dev_compact compact;
	int result = ar_dev_compact_decode(frame->outband, frame->outband_length, &compact);
	receiver.last_dev_result = result;
	if (result) { receiver.compact_invalid++; receiver.dev_control_invalid++; return; }
	receiver.compact_valid++; receiver.dev_control_valid++;
	memcpy(receiver.last_compact, frame->outband, 6);
	/* No address/channel fields exist in this form. Never use it for idle
	 * admission or fabricate a full broadcast with an invented peer address. */
	if (!dev_active || receiver.dev_state != 2 || receiver.assigned_slot != 0) { return; }
	if (compact.token != receiver.dev_token || compact.state != 2) { dev_invalidate(); }
}

static void dev_observe_payloads(const struct ar_mac_frame *frame)
{
	if (receive_operation < 59 || !frame->descriptor_count) { return; }
	receiver.payload_packets++;
	for (unsigned i = 0; i < frame->descriptor_count; i++) {
		struct ar_mac_payload payload;
		if (ar_mac_payload_at(frame, i, &payload)) { continue; }
		if (receive_operation >= 71 && payload.port < 5) {
			struct ar_rf_port_observation *item = &traffic_info.ports[payload.port];
			uint64_t packet_ticks = elapsed_ticks();
			uint32_t elapsed = (uint32_t)(packet_ticks / 40);
			if (!item->packets) { item->first_us = elapsed; item->first_sequence = frame->sequence; }
			item->packets++; item->bytes += payload.length;
			item->last_us = elapsed; item->last_sequence = frame->sequence;
			uint64_t second = packet_ticks / 40000000;
			if (second < 16) { traffic_info.port_bytes_by_second[second][payload.port] += payload.length; }
		}
		if (receive_operation >= 60) {
			if (payload.port < 5) {
				receiver.port_packets[payload.port]++; receiver.port_bytes[payload.port] += payload.length;
			} else { receiver.other_port_packets++; receiver.other_port_bytes += payload.length; }
			if (payload.port && !receiver.first_data_length && payload.length) {
				receiver.first_data_port = payload.port; receiver.first_data_length = payload.length;
				memcpy(receiver.first_data, payload.data, payload.length < 128 ? payload.length : 128);
			}
		}
		if (payload.port) { continue; }
		if (receive_operation >= 62 && dev_active) {
			(void)ar_dev_stream_feed(&dev_stream, payload.data, payload.length, dev_command_observed, NULL);
			memcpy(receiver.stream_commands, dev_stream.commands, sizeof receiver.stream_commands);
			receiver.stream_checksum_errors = dev_stream.checksum_errors;
			receiver.stream_length_errors = dev_stream.length_errors;
			receiver.stream_discarded = dev_stream.discarded; receiver.stream_buffered = dev_stream.used;
		}
		size_t copied = payload.length < 132 ? payload.length : 132;
		if (!receiver.port0_packets) {
			receiver.first_port0_length = payload.length;
			memcpy(receiver.first_port0, payload.data, copied);
		}
		receiver.port0_packets++; receiver.port0_bytes += payload.length;
		receiver.last_port0_length = payload.length;
		memset(receiver.last_port0, 0, sizeof receiver.last_port0);
		memcpy(receiver.last_port0, payload.data, copied);
		if (receive_operation < 60) { continue; }
		/* Diagnostic envelope recognition only. A partial command may span
		 * MAC frames; count it as unframed here, not as corrupt stream data. */
		size_t offset = 0;
		while (offset < payload.length) {
			const uint8_t *data = payload.data + offset;
			size_t left = payload.length - offset;
			if (left < 4 || data[0] != 0x4a || data[2] > 128 || (size_t)data[2] + 4 > left) {
				receiver.port0_unframed++; break;
			}
			uint8_t check = 0; size_t length = data[2] + 4;
			for (size_t j = 0; j < length; j++) { check ^= data[j]; }
			if (check) { receiver.port0_unframed++; break; }
			if (data[1] < 8) { receiver.port0_commands[data[1]]++; }
			if (data[1] != 2 && !receiver.first_nonstats_length) {
				receiver.first_nonstats_length = length;
				memcpy(receiver.first_nonstats, data, length);
			}
			offset += length;
		}
	}
}

static void dev_observe_lock(const struct ar_dev_control *control)
{
	if (receive_operation < 57 || !dev_active) { return; }
	uint32_t elapsed = elapsed_us();
	receiver.lock_messages++;
	receiver.lock_slot_states[(control->slots[0] >> 1) & 3]++;
	receiver.lock_observed_extensions |= control->extensions;
	if (control->extensions & 1) {
		if (!receiver.login_extension_packets) {
			receiver.first_login_elapsed_us = elapsed;
			receiver.first_login_length = control->length;
			memcpy(receiver.first_login_control, control->data, control->length);
		}
		receiver.login_extension_packets++;
		receiver.last_login_elapsed_us = elapsed;
	}
	struct ar_dev_lock_context lock = {.selected_slot = 0,
		.assigned_slot = receiver.assigned_slot, .token = receiver.dev_token};
	memcpy(lock.peer, receiver.accepted_peer, 4);
	/* A normal state-2 broadcast need not carry another login extension.
	 * A changed token/profile stops TX before delivery. Normal continuous
	 * sessions rebuild after releasing this packet's owned view. */
	if (receive_operation >= 59 && receiver.dev_state == 2) {
		if (memcmp(control->peer, lock.peer, 4)) { return; }
		if (control->mode || control->slot_bitmap != dev_config.slot_bitmap ||
		    control->receive_channel != dev_config.receive_channel ||
		    control->transmit_channel != dev_config.receive_channel ||
		    (control->slots[receiver.assigned_slot] >> 8) != receiver.dev_token ||
		    ((control->slots[receiver.assigned_slot] >> 1) & 3) != 2) {
			dev_invalidate();
		}
		return;
	}
	struct ar_dev_login login;
	enum ar_dev_lock_result result = ar_dev_lock_evaluate(&dev_config, &lock, control, &login);
	receiver.last_lock_result = result;
	receiver.lock_results[result]++;
	if (result != AR_DEV_LOCK_ASSIGNMENT) { return; }
	if (!receiver.login_matching_packets) {
		receiver.first_matching_elapsed_us = elapsed;
		receiver.first_matching_length = control->length;
		memcpy(receiver.first_matching_control, control->data, control->length);
	}
	receiver.login_matching_packets++;
	receiver.last_matching_elapsed_us = elapsed;
	receiver.last_login_slot = login.slot; receiver.last_login_token = login.token;
	receiver.last_login_value = login.value; receiver.last_login_ap_state = login.ap_state;
	memcpy(&receiver.last_login_sync, login.sync, 4);
	if (receive_operation < 59) { return; }
	/* This first connected path supports physical user0 / assigned slot0.
	 * 0x232928 requires every remote-sync flag to have been applied before
	 * setting completion. A zero flags byte needs only the four-byte CRC ACK;
	 * unknown/nonzero requests must not be acknowledged as executed. */
	if (login.slot || login.sync[0]) { receiver.sync_unsupported++; return; }
	if (login.ap_state != 2) { return; }
	uint8_t check = 0;
	for (unsigned i = 0; i < 4; i++) { check = ar_mac_crc8_step(check ^ login.sync[i]); }
	(void)ar_dev_command_encode(dev_sync_ack, sizeof dev_sync_ack, 4, &check, 1);
	receiver.assigned_slot = login.slot; receiver.dev_token = login.token;
	receiver.sync_ack_pending = 1;
	/* 0x22ef52: local state2, reset MCS observation, DEV B4C request field
	 * via 0x2236e0 and CF4 bit21 via 0x22b98e(1). Operation75 publishes the
	 * corresponding link transition to registered host observers. */
	field(0xb4c0305, 1); field(0xcf41501, 1);
	link_event(2, receiver.dev_state);
	receiver.dev_state = 2; receiver.connected_elapsed_us = elapsed;
	if (receive_operation >= 67) { dev_mcs_apply(ar_dev_mcs_connect(&dev_mcs, 1)); }
}

static void dev_preencode(void)
{
	if (!dev_active || recovery.pending) { return; }
	receiver.tx_preencode++;
	receiver.last_tx_user = (read32(BB + 0xf70) >> 12) & 15;
	receiver.last_tx_budget = read32(BB + 0xf5c) >> 16;
	receiver.last_tx_request = (read32(BB + 0xf58) >> 4) & 15;
	if (receiver.last_tx_user != 0) {
		receiver.tx_errors++; dev_tx_finish_segments(); return;
	}
	(void)ar_dev_tx_retire(&dev_tx, 1);
	if (receive_operation >= 67) {
		dev_mcs_apply(ar_dev_mcs_preencode(&dev_mcs,
			elapsed_ms(), receiver.assigned_slot));
	}
	struct ar_dev_report report = {.state = receiver.dev_state, .assigned_slot = receiver.assigned_slot,
		.receive_channel = dev_config.receive_channel,
		.slot_value = dev_power_index,
		.status_a = dev_status_a, .status_b = dev_status_b};
	memcpy(report.local, dev_config.local, 4);
	const struct ar_dev_tx_packet *packet;
	unsigned ack = receiver.sync_ack_pending && (receiver.last_tx_budget & ~7u) >= 33;
	uint8_t stream[132]; unsigned port = 0;
	size_t length = ack ? sizeof dev_sync_ack : 0;
	size_t budget = receiver.last_tx_budget & ~7u;
	/* Keep remote-sync ACK ahead of host streams even when its budget is
	 * temporarily too small. The endpoint retains bytes on every skipped TX. */
	if (!receiver.sync_ack_pending && receive_operation >= 66 && receiver.dev_state == 2 &&
	    receiver.assigned_slot == 0 && budget > 28 && tx_peek_callback && tx_commit_callback) {
		size_t capacity = budget - 28;
		if (capacity > sizeof stream) { capacity = sizeof stream; }
		length = tx_peek_callback(stream, capacity, &port, tx_context);
		if (!length) { port = 0; }
		if (length > capacity || (length && (!port || port > 4))) {
			receiver.tx_errors++; dev_tx_finish_segments(); return;
		}
	}
	int result = ar_dev_tx_prepare_payload(&dev_tx, &report, port, ack ? dev_sync_ack : stream,
		length, receiver.last_tx_budget, &packet);
	if (result) {
		receiver.tx_errors++; receiver.tx_owned = dev_tx.owned;
		dev_tx_finish_segments(); return;
	}
	/* Original 0x237ca6 submits header then outband as distinct segments,
	 * followed by its terminal mask pulse. Caches remain disabled. */
	barrier();
	field(0xd440020, (uint32_t)(uintptr_t)packet->bytes);
	field(0xd400010, packet->header_length); field(0xd401808, 1);
	field(0xd440020, (uint32_t)(uintptr_t)(packet->bytes + packet->header_length));
	field(0xd400010, 16); field(0xd401808, 2);
	if (ack) {
		if (!receiver.sync_ack_submitted) {
			memcpy(receiver.first_sync_ack_packet, packet->bytes, packet->length);
		}
		receiver.sync_ack_pending = 0; receiver.sync_ack_submitted++;
	}
	if (length) {
		field(0xd440020, (uint32_t)(uintptr_t)(packet->bytes + packet->header_length + 16));
		field(0xd400010, length); field(0xd401808, 4);
	}
	dev_tx_finish_segments();
	if (length && !ack) {
		if (!tx_payload_info.packets) {
			tx_payload_info.first_length = packet->length;
			memcpy(tx_payload_info.first_packet, packet->bytes, packet->length);
		}
		tx_payload_info.packets++; tx_payload_info.bytes += length;
		tx_payload_info.last_length = packet->length; tx_payload_info.last_port = port;
		memset(tx_payload_info.last_packet, 0, sizeof tx_payload_info.last_packet);
		memcpy(tx_payload_info.last_packet, packet->bytes, packet->length);
		tx_commit_callback(port, length, tx_context);
	}
	memcpy(receiver.last_tx_packet, packet->bytes, sizeof receiver.last_tx_packet);
	receiver.last_tx_length = packet->length;
	memset(receiver.last_tx_extended, 0, sizeof receiver.last_tx_extended);
	memcpy(receiver.last_tx_extended, packet->bytes, packet->length < sizeof receiver.last_tx_extended ?
		packet->length : sizeof receiver.last_tx_extended);
	receiver.last_tx_elapsed_us = elapsed_us();
	if (!receiver.tx_packets) { receiver.first_tx_elapsed_us = receiver.last_tx_elapsed_us; }
	receiver.tx_packets++; receiver.tx_owned = dev_tx.owned;
}

static void receive_complete(void)
{
	receiver.dma_completions++;
	if (!receive_pending) { receiver.invalid_packets++; return; }
	barrier();
	receiver.last_packet_elapsed_us = elapsed_us();
	if (!receiver.packets) { receiver.first_packet_elapsed_us = receiver.last_packet_elapsed_us; }
	receiver.packets++;
	receiver.bytes += receive_length;
	receiver.last_length = receive_length; receiver.last_user = receive_user;
	receiver.last_rx_metadata = receive_metadata;
	memset(receiver.first_bytes, 0, sizeof receiver.first_bytes);
	memcpy(receiver.first_bytes, receive_buffers[receive_head], receive_length < 64 ? receive_length : 64);
	/* Parse synchronously while this slot is owned by the receive consumer.
	 * Views must not survive advancing the ring. */
	struct ar_mac_frame frame;
	int result = ar_mac_decode(receive_buffers[receive_head], receive_length, &frame);
	receiver.last_mac_result = (uint32_t)result;
	if (result) { receiver.mac_invalid_packets++; }
	else {
		int forwarding_context_valid = 1;
		receiver.mac_valid_packets++;
		receiver.last_mac_sequence = frame.sequence;
		receiver.last_mac_descriptors = frame.descriptor_count;
		receiver.last_outband_length = frame.outband_length;
		dev_observe_payloads(&frame);
		if (receive_operation >= 60 && frame.outband_length == 6) {
			dev_observe_compact(&frame);
			result = (int)receiver.last_dev_result;
		} else if (frame.outband_length) {
			struct ar_dev_control control;
			result = ar_dev_control_decode(frame.outband, frame.outband_length, &control);
			receiver.last_dev_result = (uint32_t)result;
			if (result == AR_MAC_UNSUPPORTED) { receiver.dev_control_unsupported++; }
			else if (result) { receiver.dev_control_invalid++; }
			else {
				receiver.dev_control_valid++;
				receiver.last_dev_length = control.length;
				receiver.last_dev_sequence = control.sequence;
				receiver.last_dev_channels = control.receive_channel | (uint32_t)control.transmit_channel << 8;
				receiver.last_dev_extensions = control.extensions;
				memcpy(receiver.last_dev_peer, control.peer, 4);
				memset(receiver.last_dev_control, 0, sizeof receiver.last_dev_control);
				memcpy(receiver.last_dev_control, control.data, control.length);
				dev_admit(&control);
				dev_observe_lock(&control);
				forwarding_context_valid = !memcmp(control.peer, receiver.accepted_peer, 4);
			}
		}
		/* Copy external payloads only after this frame's control can invalidate
		 * the admitted context. Internal port 0 has its own command stream. */
		if (receive_operation >= 63 && !recovery.pending && !result && forwarding_context_valid && dev_active && receiver.dev_state == 2 &&
		    receiver.assigned_slot == 0 && payload_callback) {
			for (unsigned i = 0; i < frame.descriptor_count; i++) {
				struct ar_mac_payload payload;
				if (!ar_mac_payload_at(&frame, i, &payload) && payload.port && payload.port < 5) {
					payload_callback(0, payload.port, payload.data, payload.length, payload_context);
				}
			}
		}
	}
	receive_head = (receive_head + 1) & 3;
	receive_pending = 0;
}

/* Duration of this monitor's six-step DEV frame only. The stock frame reader
 * (0x21acda) rounds each 640 ns duration separately before summing (0x21b008).
 * Prepare commands use 0x984; RX8, special user13 and TX0 use the other fields.
 * This does not implement the general stock frame interpreter. */
static uint32_t dev_frame_microseconds(void)
{
	uint32_t prepare = ((read32(BB + 0x984) & 0xffff) * 640 + 500) / 1000;
	uint32_t broadcast = ((read32(BB + 0x970) >> 16) * 640 + 500) / 1000;
	uint32_t special = ((read32(BB + 0x980) & 0xffff) * 640 + 500) / 1000;
	uint32_t transmit = ((read32(BB + 0xa00) & 0xffff) * 640 + 500) / 1000;
	return prepare * 3 + broadcast + special + transmit;
}

uint8_t ar_rf_control_checksum(const uint8_t control[7])
{
	/* Independently derived bit basis of the original 0x253668 lookup.
	 * The last eight entries are zero in this exact release. Preserve this
	 * exception rather than assuming a standard CRC polynomial. Exhaustive
	 * table and message comparisons are recorded in the private evidence. */
	uint8_t check = 0;
	for (unsigned byte = 1; byte < 7; byte++) {
		uint8_t value = control[byte];
		if (byte == 6) { value |= 0xf0; }
		check = ar_mac_crc8_step(check ^ value);
	}
	return check;
}

uint32_t ar_rf_host_ap_time(void)
{
	return ar_ap_time_get(&ap_time, (uint32_t)(timer_ticks() / 40000));
}

int ar_rf_host_status(uint16_t users, uint8_t output[300])
{
	if (!output) { return -22; }
	if (receive_operation == 76 && !host_initialized) { return -11; }
	if (receive_operation < 67 || information.phase < AR_RF_PSRAM || information.error ||
	    !receiver.channel_count || dev_config.mode || dev_config.slot_bitmap != 1) { return -11; }
	/* GET_STATUS 0x245de8: DEV role, mode0, no external sync, slot0.
	 * DEV4 supports a single fixed frequency, programmed into both PLL banks
	 * for all users; it does not implement incoming frequency reassignment. */
	memset(output, 0, 300);
	output[0] = 1; output[3] = 1; output[4] = output[5] = 1;
	memcpy(output + 6, dev_config.local, 4);
	for (unsigned user = 0; user < 10; user++) {
		if (!(users & (1u << user))) { continue; }
		uint8_t *tx = output + 12 + user * 24, *rx = tx + 12;
		uint32_t tw = read32(BB + 0xb00 + user * 4);
		uint32_t rw = read32(BB + 0x9a0 + user * 8);
		uint32_t key = ((tw >> 8) & 7) << 16 | (((tw >> 12) & 15) % 3) << 8 |
			((tw >> 16) & 3) | (uint32_t)(((tw >> 22) & 3) == 2) << 24;
		tx[0] = 25;
		for (unsigned mcs = 0; mcs < 25; mcs++) {
			if (mcs_fch_keys[mcs] == key) { tx[0] = mcs; break; }
		}
		tx[1] = (tw >> 22) & 3; tx[2] = (tw >> 5) & 1;
		tx[3] = (tw >> 4) & 1; tx[4] = (tw >> 18) & 7; tx[5] = tw & 7;
		/* The original per-user RX getter deliberately returns MCS25;
		 * negotiated RX MCS is in the separate link entry below. */
		rx[0] = 25; rx[1] = (rw >> 6) & 3; rx[5] = (rw >> 8) & 7;
		unsigned selector = rw & 7, interleave = (rw >> 13) & 7;
		unsigned base = interleave & ~4u;
		if (selector == 1 || selector == 2) {
			rx[4] = selector - 1; rx[2] = interleave != 0;
		} else if (selector >= 3 && selector <= 6) {
			/* Inverse selector/interleaver mapping, 0x24d058. */
			if (base < 1 || base > 3 || selector < base + 2 || selector > base + 3 ||
			    (selector == 3 && base != 1) || (selector == 6 && base != 3)) { return -95; }
			rx[4] = base + 1; rx[3] = selector == base + 3; rx[2] = interleave != base;
		} else if (selector != 0) { return -95; }
		for (unsigned b = 0; b < 4; b++) {
			tx[8+b] = rx[8+b] = receiver.frequency_khz >> (8*b);
		}
	}
	/* Deterministic unavailable entries replace original unused stack bytes.
	 * 0x2327f4 supplies DEV's configured peer, not the observed AP address. */
	for (unsigned slot = 0; slot < 8; slot++) {
		output[252 + slot * 6] = 3; output[253 + slot * 6] = 25;
	}
	output[252] = information.phase == AR_RF_RECEIVING ? receiver.dev_state : 0;
	if (information.phase == AR_RF_RECEIVING && dev_active && receiver.dev_state == 2 &&
	    dev_mcs.current < dev_mcs.config.count) {
		output[253] = dev_mcs.config.rows[dev_mcs.current].mcs;
	}
	memcpy(output + 254, dev_config.configured_peer, 4);
	return 0;
}

int ar_rf_monitor(uint32_t frequency, uint32_t milliseconds, uint8_t identity_hash, unsigned operation)
{
	return ar_rf_monitor_plan(frequency, milliseconds, identity_hash, operation, NULL, 0);
}

int ar_rf_monitor_plan(uint32_t frequency, uint32_t milliseconds, uint8_t identity_hash,
	unsigned operation, const uint8_t *channels, size_t length)
{
	return configure_monitor(frequency, milliseconds, identity_hash, operation, channels, length, 0);
}

static int prepare_host(uint32_t frequency, uint32_t milliseconds, uint8_t identity_hash,
	const uint8_t *channels, size_t length, unsigned deferred)
{
	int result = configure_monitor(frequency, milliseconds, identity_hash, 76, channels, length, deferred);
	if (result) { return result; }
	memmove(host_config.channels, channels, length);
	host_config.length = length; host_config.frequency = frequency;
	host_config.milliseconds = milliseconds; host_config.identity = identity_hash;
	host_config.deferred = deferred; host_config.valid = host_initialized = 1;
	host_start_required = 0;
	return 0;
}
int ar_rf_prepare_host(uint32_t frequency, uint32_t milliseconds, uint8_t identity_hash,
	const uint8_t *channels, size_t length)
{
	return prepare_host(frequency, milliseconds, identity_hash, channels, length, 1);
}

/* Firmware-owned continuous mode is not exposed by the bounded diagnostic
 * monitor command. The backend still supplies the actual start candidates. */
int ar_rf_prepare_host_continuous(uint32_t frequency, uint8_t identity_hash,
	const uint8_t *channels, size_t length)
{
	return prepare_host(frequency, 0, identity_hash, channels, length, 2);
}

int ar_rf_prepare_config(uint32_t frequency, uint8_t identity_hash,
	const uint8_t *channels, size_t length, unsigned search)
{
	if (search > 1) { return -22; }
	return prepare_host(frequency, 0, identity_hash, channels, length, search ? 3 : 2);
}

int ar_rf_host_candidates(const uint8_t *data, size_t length)
{
	/* Original 0x221dca copies count four-byte addresses; 0x21b3e2
	 * additionally checks the active peer against the configured/list peers.
	 * Continuous sessions rebuild after accepting active-peer removal. */
	if (!data || length != 402 || data[0] || data[1] > AR_DEV_MAX_CANDIDATES) { return -22; }
	if (receive_operation != 76 || !host_initialized || information.error ||
	    (!host_prepared && information.phase != AR_RF_RECEIVING && !host_start_required)) { return -11; }
	unsigned remove_peer = 0;
	if (dev_active && memcmp(receiver.accepted_peer, dev_config.configured_peer, 4)) {
		unsigned match = 0;
		for (unsigned i = 0; i < data[1]; i++) {
			if (!memcmp(receiver.accepted_peer, data + 2 + i * 4, 4)) { match = 1; break; }
		}
		if (!match && !automatic_recovery()) { return -95; }
		remove_peer = !match;
	}
	memcpy(dev_candidates, data + 2, data[1] * 4);
	dev_config.candidate_count = data[1];
	if (remove_peer) {
		recovery.candidate_removals++; ar_fch_monitor_force(&fch_monitor);
		/* No old-peer TX while waiting for the next monitor interval. */
		request_recovery(3);
	}
	if (!host_prepared || host_start_required) { return 0; }
	host_prepared = 0;
	return start_prepared_receive();
}

static void command_write(uint32_t encoded, uint32_t value, void *context)
{
	(void)context;
	field(encoded, value);
}
static void command_byte(uint32_t offset, uint32_t value, void *context)
{
	(void)context;
	unsigned shift = (offset & 3) * 8;
	update(BB + (offset & ~3u),255u << shift,value << shift);
}
static int build_commands(void)
{
	/* Mode-0 DEV frame from 0x22c774: RX8, independent measurement RX11,
	 * TX0 with pre-command mode6. All three currently share one frequency.
	 * Scan actions and retained TX-buffer ownership remain separate gaps. */
	static const struct ar_command_descriptor frame[] = {{0x3621,0},{0x358,0},{0x3413,0}};
	struct ar_command_profile profiles[AR_COMMAND_USERS];
	for (unsigned user = 0; user < AR_COMMAND_USERS; user++) {
		uint32_t rx = read32(BB + 0x9a0 + user * 8);
		uint32_t tx = read32(BB + (user == 10 ? 0xb60 : 0xb00 + user * 4));
		unsigned type = rx & 7, mode = rx >> 6 & 3;
		profiles[user] = (struct ar_command_profile){
			.frequency = {receiver.frequency_khz,receiver.frequency_khz},
			.bandwidth = {tx >> (user == 10 ? 24 : 0) & 7,rx >> 8 & 7},
			.rx_flag = (type == 1 && mode < 2) || (type == 2 && mode >= 2)};
	}
	memcpy(command_state.calibration[2],calibration,sizeof calibration);
	command_state.valid_banks = 4; /* Only bank 2 is calibrated by this runtime. */
	int result = ar_command_build(&command_program,frame,3,1,profiles,command_write,NULL);
	commands_ready = !result;
	return result;
}

static int configure_monitor(uint32_t frequency, uint32_t milliseconds, uint8_t identity_hash,
	unsigned operation, const uint8_t *channels, size_t length, unsigned deferred)
{
	progress(0xa000, frequency, milliseconds, operation);
	uint64_t pll;
	unsigned maximum_ms = deferred ? 60000 : operation >= 70 ? 15000 : operation >= 53 ? 10000 : 1500;
	if (information.phase != AR_RF_PSRAM || !pins_configured || operation > (deferred ? 76u : 75u) || milliseconds > maximum_ms || (!milliseconds && deferred < 2) || ar_rf_pll_word(frequency, &pll)) {
		return failure(-22);
	}
	uint8_t fixed_channel[4], tx_gain[4] = {0};
	struct ar_dev_mcs_config policy = {0};
	if (operation >= 54) {
		unsigned prefix = operation >= 67 ? AR_RF_DEV4_PREFIX : operation >= 59 ? 32 : operation >= 58 ? 28 : 24;
		if (!channels || length < prefix + 4 || (length - prefix) % 4 ||
		    (length - prefix) / 4 > AR_RF_MAX_CHANNELS ||
		    memcmp(channels, operation >= 67 ? "DEV4" : operation >= 61 ? "DEV3" : operation >= 59 ? "DEV2" : operation >= 58 ? "DEV1" : "DEV0", 4) || channels[20] > 2 ||
		    channels[21] >= (length - prefix) / 4) { return failure(-22); }
		if (operation >= 58 && (channels[24] > 63 || channels[26] > 63)) { return failure(-22); }
		if (operation >= 59 && (channels[28] > 63 || channels[29] > (operation >= 61 ? 127 : 0) || channels[30] || channels[31])) { return failure(-22); }
		if (operation >= 67) {
			if (ar_dev_mcs_decode_config(&policy, channels + 32, AR_DEV_MCS_CONFIG_BYTES)) { return failure(-22); }
			for (unsigned i = 0; i < 25; i++) {
				uint32_t key = le32(channels + 168 + i * 4);
				if (key & ~UINT32_C(0x01070303)) { return failure(-22); }
				for (unsigned j = 0; j < i; j++) {
					if (key == le32(channels + 168 + j * 4)) { return failure(-22); }
				}
			}
		}
		unsigned count = (length - prefix) / 4, index = channels[21];
		uint8_t hash = 0;
		for (unsigned i = 0; i < 4; i++) { hash ^= channels[4 + i]; }
		if (hash != identity_hash || le32(channels + prefix + index * 4) != frequency) { return failure(-22); }
		for (unsigned i = 0; i < count; i++) {
			uint64_t checked;
			uint32_t value = le32(channels + prefix + i * 4);
			if (value <= 4300000 || ar_rf_pll_word(value, &checked)) { return failure(-22); }
			for (unsigned j = 0; j < i; j++) {
				if (value == le32(channels + prefix + j * 4)) { return failure(-22); }
			}
		}
		memset(&dev_config, 0, sizeof dev_config);
		memcpy(dev_config.local, channels + 4, 4);
		memcpy(dev_config.configured_peer, channels + 8, 4);
		memcpy(dev_candidates, channels + 12, 8);
		dev_config.candidates = dev_candidates; dev_config.candidate_count = channels[20];
		dev_config.receive_channel = index; dev_config.channel_count = count;
		dev_config.slot_bitmap = 1;
		dev_status_a = channels[22]; dev_status_b = channels[23];
		if (operation >= 58) { memcpy(tx_gain, channels + 24, 4); }
		dev_power_index = operation >= 59 ? channels[28] : 0;
		ar_dev_retx_init(&dev_retx, operation >= 61 ? channels[29] : 0);
		if (operation >= 67) {
			(void)ar_dev_mcs_init(&dev_mcs, &policy);
			mcs_sync_mmio = channels[42];
			for (unsigned i = 0; i < 25; i++) { mcs_fch_keys[i] = le32(channels + 168 + i * 4); }
		}
		if (deferred == 3) {
			/* Normal loader init_chan=-1 searches its configured list. */
			if (index) { return failure(-22); }
			channels += prefix; length -= prefix;
		} else {
			memcpy(fixed_channel, channels + prefix + index * 4, 4);
			channels = fixed_channel; length = 4;
		}
	}
	if (operation >= 39) {
		if (!channels || !length || length % 4 || length > AR_RF_MAX_CHANNELS * 4 || le32(channels) != frequency) { return failure(-22); }
		for (size_t i = 0; i < length / 4; i++) {
			uint32_t next = le32(channels + i * 4);
			/* This bring-up has calibration and pin setup only for band 2. */
			if (next <= 4300000 || ar_rf_pll_word(next, &channel_plls[i])) { return failure(-22); }
			for (size_t j = 0; j < i; j++) {
				if (next == le32(channels + j * 4)) { return failure(-22); }
			}
		}
	} else if (length) { return failure(-22); }
	commands_ready = 0;
	memset(&receiver, 0, sizeof receiver);
	memset(&tx_payload_info, 0, sizeof tx_payload_info);
	ar_dev_stream_init(&dev_stream);
	memset(&phy, 0, sizeof phy);
	memset(&phy_trace, 0, sizeof phy_trace); phy_poll_tick = 0;
	memset(&sync_info, 0, sizeof sync_info);
	memset(&traffic_info, 0, sizeof traffic_info); traffic_info.version = 1;
	memset(&tx_mcs_info, 0, sizeof tx_mcs_info); tx_mcs_info.current = 1;
	memset(&mcs_info, 0, sizeof mcs_info); mcs_info.first_observed_mcs = 25;
	if (operation < 67) { memset(&dev_mcs, 0, sizeof dev_mcs); mcs_sync_mmio = 0; }
	memset(phy_header_valid, 0, sizeof phy_header_valid); memset(phy_eop_seen, 0, sizeof phy_eop_seen);
	dev_active = 0;
	/* Stock's preparation cache belongs to the calibrated radio, not a link
	 * session. Mode-0 engine resets preserve it. Only normal host contexts
	 * may reuse this single supported band; diagnostics can replace fields
	 * independently and must not share their cached state with normal use. */
	if (operation != 76 || receive_operation != 76) { command_state.cached_bank = 3; command_state.board_band = -1; }
	memset(&fch_monitor, 0, sizeof fch_monitor); recovery_reset_pending = 0;
	receiver.before_monitor_header = read_header_status();
	read_control_header(receiver.before_monitor_control);
	if (operation >= 39) {
		receiver.channel_count = length / 4;
		for (unsigned i = 0; i < receiver.channel_count; i++) {
			receiver.channels[i].frequency_khz = le32(channels + i * 4);
		}
	}
	receiver.frequency_khz = frequency;
	receive_head = receive_pending = 0;
	receive_operation = operation;
	normal_search = deferred == 3; search_candidate_seen = 0;
	receive_milliseconds = milliseconds; host_prepared = 0;
	if (deferred) { field(0x8000201, 1); field(0x8000001, 1); }
	/* Fixed-channel receive diagnostic, not a complete link/session scheduler. */
	serial_word(0x905, pll); /* DEV broadcast RX user 8. */
	if (operation) { serial_word(0x900, pll); }
	serial_word(0x90a, pll); /* Search RX user 11. */
	/* The dedicated calibration PLL bank remains at its calibration word in
	 * the working stock receiver; normal commands select per-user shadows. */
	serial_word(0x4d0, UINT64_C(0x0113000000));
	configure_receive_context(pll);
	if (operation >= 58) {
		/* 0x24528a..0x2452de, user0 with both per-path controls enabled.
		 * The plan supplies effective calibration fields after any factory
		 * offset/clock scaling. No proprietary gain table is embedded and
		 * these fields do not constitute dynamic power control. */
		field(0x9a40806, tx_gain[0]); field(0x9a40008, tx_gain[1]);
		field(0xda40006, tx_gain[2]); field(0xda01808, tx_gain[3]);
	}
	/* Post-calibration PHY initialization is distinct from reset defaults.
	 * Ascent VRX uses the measured high-rate clock flag and DEV role, mode 0. */
	field(0x8001301, 1); field(0xa600401, 0);
	field(0x8000701, 0); field(0x8001002, 0); field(0x80c0002, 0);
	field(0xb201e01, 1);
	/* Per-user receive buffer controls are initialized after PSRAM in stock.
	 * Clear only the ten documented bit-5 fields, preserving adjacent users. */
	for (unsigned user = 0; user < 10; user++) {
		field(((user >> 2) + 0x2fc) * 0x40000 | ((user & 3) * 0x800 + 0x500) | 1, 0);
	}
	/* Closed-loop power setup follows user profiles in the stock initializer. */
	field(0xa281901, 1); field(0xa580006, 0); field(0xa580806, 0);
	/* Corroborated post-user-configuration helper for this profile. Reset
	 * defaults leave these controls masked; the working stock readback is 0. */
	update(BB + 0xbac, 0xffffc0c0, 0);
	update(BB + 0xbd4, 0x0003ff00, 0);
	field(0xa400020, 0x0d0d0d0d); field(0xa480020, 0x80808080); field(0xa200301, 1);
	field(0x9f81601, 1); field(0x9f81808, 0x3c);
	field(0x9e41903, 4); field(0x9e41601, 1); field(0x9e41201, 1);
	if (operation) { field(0x9e41d03, 4); field(0x9e41701, 1); field(0x9e41301, 1); }
	field(0xa900010, frequency / 2000);
	field(0xb400817, 0); field(0xb440013, 0x36000);
	field(0xba01010, 0x4000); field(0xba40007, 0x1d); field(0xba41010, 0); field(0xba80004, 9);
	field(0xcec0817, 0x654000); field(0xcf00014, 0x28800); field(0xcf00002, 2);
	field(0x8081801, 0); field(0x8081901, 0); field(0xcd00001, 1);
	/* PHY correlation/scrambler identity: the stock initializer programs it
	 * for all RX users from the XOR of the four-byte local baseband MAC. */
	for (unsigned user = 0; user < 10; user++) { field((user + 0x1b4) * 0x80000 | 8, identity_hash); }
	field(0xdf00008, identity_hash);
	for (unsigned user = 0; user < 10; user++) {
		field((((user + 2) & ~3u) + 0xb7c) * 0x10000 | ((user + 2) & 3) << 11 | 8,
			user == 0 ? identity_hash : 0);
	}
	field(0xb601008, identity_hash); field(0xb601803, 1); field(0xb601b02, 1);
	if (operation >= 44) {
		/* Before the first TX preparation, retain initializer defaults. Once
		 * loaded, preserve compensation through normal restart/recovery, as
		 * 0x223128's cached-bank check and mode-0 reset helpers do. Clearing it
		 * here could expose a pre-encode before the next prepare notification.
		 * Other calibration banks and retained TX-buffer ownership remain unported. */
		if (command_state.cached_bank == 3) {
			field(0x4800101, 0);
			field(0x4a00010, 0); field(0x4a41010, 0);
			field(0x48c0008, read32(BB + 0x48c) & 0x33);
		}
	} else {
		/* Historical globally-applied compensation diagnostic. */
		field(0x4800101, 1);
		field(0x4a00008, calibration[0]); field(0x4a00808, calibration[1]);
		field(0x4a41008, calibration[2]); field(0x4a41808, calibration[3]);
		field(0x48c0008, calibration[4] | (read32(BB + 0x48c) & 0x33));
	}
	if (operation >= 45) {
		/* DEV initializer 0x22373a -> 0x2236e0 writes 1 to B4C[7:3].
		 * Trial-67 prestart readback corroborates this role-dependent field;
		 * its later per-user updates and software state remain unported. */
		field(0xb4c0305, 1);
	}
	progress(0xa001, read32(BB + 0xfa8), 0, 0);
	/* Expanded prepare/RX/wait sequence and user metadata, from the command contract. */
	static const uint8_t operations[] = {0x11, 0x22, 0x13, 0x23, 0x24, 0x15, 0x58,
		0x11, 0x11, 0x11, 0x11, 0x13, 0x13, 0x11, 0x13, 0x13, 0x11, 0x13};
	uint8_t command = operation >= 18 ? 0x13 : operations[operation];
	uint8_t prefix = (command & 0x30) == 0x10 ? 0x3a : 9;
	write32(BB + 0x810, (uint32_t)0x5c << 24 | (uint32_t)0x3a << 16 | (uint32_t)command << 8 | prefix);
	write32(BB + 0x814, 0x80);
	write32(BB + 0x870, operation == 6 ? 0x000feedd : 0x000fee88);
	for (unsigned offset = 0x8a0; offset < 0x900; offset += 4) { write32(BB + offset, 0); }
	field(0x9701010, 0xffff); /* Bounded monitor handles completion; no short RX cutoff. */
	if (operation) { field(0x9700010, 0xffff); }
	field(0x8081d01, 1); field(0x8081e01, 0);
	if (operation == 7) {
		/* Observed stock DEV receive/search loop: six command bytes, separate
		 * per-step user nibbles, and a search pre-command power context. */
		write32(BB + 0x810, 0x183a113a); write32(BB + 0x814, 0x00802309);
		write32(BB + 0x870, 0x0f00dd88); write32(BB + 0x8a4, 0x0000e000);
		field(0x9800010, 0xffff);
	}
	if (operation >= 8) {
		unsigned user = operation == 9 ? 13 : operation >= 13 ? 8 : 0;
		write32(BB + 0x870, 0x000fee00 | user << 4 | user);
		if (!user) { field(0xa100010, 0xffff); }
		else if (user == 13) { field(0x9800010, 0xffff); }
	}
	if (operation == 15 || operation == 16) {
		/* One logical receive command expands to prepare/body, followed by
		 * the end flag. No additional wait command or invalid-user context. */
		write32(BB + 0x810, 0x0080003a | (uint32_t)command << 8);
		write32(BB + 0x814, 0);
		write32(BB + 0x870, operation == 16 ? 0x00000f88 : 0x00000f00);
		if (operation == 15) { field(0xa100010, 0xffff); }
	}
	if (operation == 17) {
		write32(BB + 0x810, 0x133a113a); write32(BB + 0x814, 0x80);
		write32(BB + 0x870, 0x000f0088);
		field(0xa100010, 0xffff);
	}
	if (operation >= 18 && operation <= 24) {
		static const uint8_t raw_users[] = {0, 8, 9, 10, 11, 12, 13};
		unsigned user = raw_users[operation - 18];
		write32(BB + 0x810, 0x0080133a); write32(BB + 0x814, 0);
		write32(BB + 0x870, 0xf00 | user << 4 | user);
		field(0xa100010, 0x0da3); field(0x9701010, 0x208a);
		field(0x99c1010, 0x0b32); field(0x9800010, 0x009c); field(0x9801010, 0x041d);
		field(0x8080e01, 0);
	}
	if (operation >= 25) {
		unsigned user = operation == 25 ? 0 : operation == 26 ? 8 : 13;
		unsigned body = operation == 27 ? 0x58 : 0x11;
		write32(BB + 0x810, 0x0080003a | body << 8); write32(BB + 0x814, 0);
		write32(BB + 0x870, 0xf00 | user << 4 | user);
		field(0x9f80003, 0); field(0x9f80301, 0); field(0x9f80602, 0);
		field(0x9800010, 0x009c); field(0x9801010, 0x041d);
		field(0xa100010, 0x0da3); field(0x9701010, 0x208a);
	}
	if (operation >= 29 && operation <= 34) {
		/* Continuous slot-0 reception with the measured broadcast modulation
		 * profile. SDK bandwidth codes are 0=1.25,1=2.5,2=5,3=10,4=20,5=40 MHz. */
		unsigned bandwidth = operation - 29;
		field(0x9a00003, 5); field(0x9a00d03, 6); field(0x9a00803, bandwidth);
		field(0x9a01808, 0x20);
		field(0xb000003, bandwidth); field(0xb001203, 3); field(0xb000401, 1);
		write32(BB + 0x810, 0x0080133a); write32(BB + 0x814, 0);
		write32(BB + 0x870, 0xf00); field(0xa100010, 0x208a);
	}
	if (operation >= 35) {
		/* Corroborated DEV/mode-0 link-start prerequisites, not the already
		 * linked register snapshot: link task message 0x200 -> 0x22b98e(0),
		 * 0x22d71c(...,1), then 0x22aaea -> 0x222bde(8,1). Physical meaning
		 * beyond these field contracts remains under investigation. */
		field(0xcf41501, 0);
		field(0xde00008, 0);
		field(0xbf00002, 0); /* Mode 0 maps logical broadcast user 8 to buffer user 0. */
		write32(BB + 0x810, 0x183a113a); write32(BB + 0x814, 0x00802309);
		write32(BB + 0x870, 0x0f00dd88); write32(BB + 0x8a4, 0x0000e000);
		field(0x9701010, 0x208a); field(0x9800010, 0x009c);
	}
	if (operation >= 41) {
		/* Ascent VRX 18.21.10 external DEV br.rx.bandwidth is 5 MHz (code 2).
		 * Startup 0x2251ec -> 0x22d3a6 -> 0x22ad12 applies that configuration;
		 * the previous 20 MHz value came from an already-linked snapshot.
		 * Keep operation 39 as the control. Frame duration uses the same
		 * non-40-MHz branch in 0x22cea8. Software pre-command metadata and
		 * later negotiated bandwidth changes are not implemented here. */
		field(0x9e00803, 2);
	}
	if (operation >= 42) {
		/* External br.rx: payload on, tintlv enabled, length 12 (code 2),
		 * number 2 (code 1). 0x22d3a6 maps these to selector 4 / interleave 5.
		 * RX8 duration 0x22cea8: floor(((17/16*26)+3)*2048/22.4)+200
		 * = 3000 us; 0x22b40c rounds this to count 4688 (0x1250).
		 * Preserve operation 41's bandwidth-only comparison. */
		field(0x9e00003, 4); field(0x9e00d03, 5);
		field(0x9701010, 0x1250);
	}
	if (operation >= 43) {
		/* Stock prestart observer trial 67 corroborates RX8's default 0x3c.
		 * 0x24c93c writes the tuning-table/default value; 0x2371e4 writes
		 * 0x20 only for link state 1. Its physical interpretation remains
		 * unverified. Operation 42 retains the linked-snapshot comparison. */
		field(0x9e01808, 0x3c);
	}
	if (operation >= 46 && operation <= 52) {
		/* Compare the previously observed linked RX8 bandwidth/interleaver
		 * against configured startup, retaining startup upper byte and all
		 * operation-45 prerequisites. This does not assert the peer's current
		 * profile: a native load interrupts an already-established link. */
		field(0x9e00803, 4); field(0x9e00003, 5); field(0x9e00d03, 6);
		field(0x9701010, 0x208a);
	}
	if (operation >= 56) {
		/* Fixed DEV startup TX0 profile, corroborated by stock trial 94 and
		 * 0x24d13a/0x221610: internal MCS index 1, bandwidth code 1,
		 * interleave length code 2 -> modulation 0, code length/rate 3,
		 * rate field 1. This is not the later adaptive MCS policy.
		 * 0x22cea8 with this configured context gives
		 * floor(((17/16 * (12+2)) + 3) * 2048/22.4) + 600 = 2234 us.
		 * 0x22b40c rounds microseconds to 640 ns hardware counts.
		 * Program before deriving search dwell; preserve the other slot. */
		field(0xb000803, 0); field(0xb000c04, 3); field(0xb001002, 1);
		field(0xa000010, (2234u * 10000 / 640 + 5) / 10);
	}
	if (operation == 36 || operation == 37 || operation == 39 || operation >= 41) {
		/* 0x222cac(8,0) also clears the shared identity field. Trial 59
		 * cleared only DE0. Keep operation 35 available as the old control. */
		field(0xdf00008, 0);
		receiver.search_frame_us = dev_frame_microseconds();
		receiver.search_dwell_ms = receiver.search_frame_us / 1000 + 2;
		search_pll = pll;
	}
	if (operation == 47) {
		/* Working-stock readback retains the selected local identity in RX8
		 * and its shared field. Compare this with the acquisition clear;
		 * keep it explicit rather than assuming the peer's current state. */
		field(0xde00008, identity_hash); field(0xdf00008, identity_hash);
	}
	if (operation == 48) {
		/* Isolate the RX8 upper byte written by stock 0x2371e4 in link
		 * state 1. Retain operation 46's zero acquisition identity; this
		 * profile comparison does not perform or claim link acquisition. */
		field(0x9e01808, 0x20);
	}
	if (operation >= 49 && operation <= 52) {
		/* Stock 0x223128 selects A48=0 for a following transmit command
		 * with bandwidth code below 4. Both TX profiles in this diagnostic
		 * satisfy that case; working-stock readback also has zero. Retain
		 * operation 46's acquisition RX8 profile and TX hold. */
		field(0xa480020, 0);
	}
	/* Operation 53 retains operation 45's configured 5 MHz/length-12
	 * acquisition profile and pre-TX A48 value. Stock trial 94 links with
	 * those fields and takes several seconds; this longer diagnostic uses
	 * checksum-aware retries without claiming a stock MAC transition. */
	if (operation >= 37) {
		/* Mode-0 startup (0x22511c..0x22512a -> 0x21db2e) holds TX
		 * before DEV search. RX preparation and both-engine restart preserve
		 * this bit. PSRAM setup previously cleared it and the monitor never
		 * reinstated it. A future linked-state transition must release it;
		 * this bounded search diagnostic does not claim that transition. */
		field(0x8000201, 1);
	}
	if (operation >= 54) {
		/* Original mode-0 link initialization seals seven zero bytes through
		 * 0x241544 -> 0x2299d2, then writes two words through 0x24b514.
		 * The 18-byte 0x229ee6 path is gated on mode 3, not used here. */
		uint8_t sync[8] = {0}; sync[0] = ar_rf_control_checksum(sync);
		receiver.sync_control_words[0] = le32(sync);
		receiver.sync_control_words[1] = le32(sync + 4);
		write32(BB + 0xb8c, receiver.sync_control_words[0]);
		write32(BB + 0xb90, receiver.sync_control_words[1]);
		barrier();
	}
	if (operation == 38 || operation == 40) {
		/* Initial frequency-search descriptors 0x358/0x39c expand to
		 * prepare/power/prepare/wait for raw users 13/14 (0x21dfea).
		 * Stock startup at 0x225996..0x225a16 sets these wait/power fields.
		 * Observe one fixed channel; power samples are not received packets. */
		write32(BB + 0x810, 0x1c3a183a); write32(BB + 0x814, 0x80);
		write32(BB + 0x870, 0x000feedd); write32(BB + 0x8a4, 0);
		field(0x9800010, 0x009c);
		field(0x9880010, 2500); field(0x98c1c04, 0);
		/* Diagnostic survey dwell, not a claim to implement stock averaging. */
		if (operation == 40) { receiver.search_dwell_ms = 10; }
	}
	field(0x8080f01, 1); /* Unused special-user TX disabled, as in stock DEV. */
	if (operation == 76) {
		int result = build_commands();
		if (result) { return failure(result); }
	}
	if (deferred) {
		host_prepared = 1; information.error = 0;
		return 0;
	}
	return start_prepared_receive();
}

static int start_prepared_receive(void)
{
	if (automatic_recovery() && ar_fch_monitor_init(&fch_monitor, receiver.search_frame_us, timer_ticks() / 40000)) {
		return failure(-95);
	}
	/* Stock interrupt-context critical sections use the high byte of CLIC's
	 * threshold register. Mask delivery while preserving pending-bit polling. */
	write32(0xe0800008, 0xff000000);
	for (unsigned index = 0; index < 20; index++) {
		uint32_t address = 0xe0801000u + irq_vector(index) * 4;
		write8(address + 2, 1); write8(address + 1, read8(address + 1) | 1);
		irq_ack(index);
	}
	information.phase = AR_RF_RECEIVING;
	progress(0xa002, read32(BB + 0xfa8), 0, 0);
	/* Match the stock receive-start reset/idle ordering, rather than merely
	 * unfreezing a controller left in the calibration operation. */
	field(0x8000101, 1); field(0x80c0201, 1);
	int reset_result = wait_bits(BB + 0xfa8, 0x100, 0x100, 2000);
	receiver.before_release_header = read_header_status();
	field(0x80c0201, 0); field(0x8000101, 0);
	progress(0xa003, (uint32_t)reset_result, read32(BB + 0xfa8), 0);
	if (reset_result || idle()) { information.phase = AR_RF_PSRAM; return failure(-110); }
	link_event(0, 3);
	if (receiver.channel_count) { receiver.channels[0].visits = 1; }
	progress(0xa004, read32(BB + 0xfa8), 0, 0);
	receive_started = timer_ticks();
	search_started = (uint32_t)receive_started;
	receive_iterations = 0;
	information.error = 0;
	if (receive_operation == 76) { lifecycle.starts++; lifecycle.last_change_ms = 0; }
	return 0;
}

void ar_rf_poll(void)
{
	if (information.phase != AR_RF_RECEIVING) { return; }
	if (recovery.pending && automatic_recovery()) { recover_host(); return; }
	if (receive_operation >= 68) {
		uint32_t tick = read32(TIMER);
		if (receiver.dev_state == 2) {
			uint32_t gap = (uint32_t)(tick - phy_poll_tick) / 40;
			phy_trace.polls++;
			if (phy_poll_tick && gap > phy_trace.max_poll_gap_us) { phy_trace.max_poll_gap_us = gap; }
		}
		phy_poll_tick = tick;
	}
	if (!(receive_iterations & 1023)) { progress(0xa005, receive_iterations, (uint32_t)elapsed_ticks(), receiver.irq_total); }
		for (unsigned index = 0; index < 20; index++) {
			uint32_t address = 0xe0801000u + irq_vector(index) * 4;
			if (!(read8(address) & 1)) { continue; }
			struct ar_rf_phy_event trace;
			unsigned tracing = receive_operation >= 68 && receiver.dev_state == 2 &&
				(index == 12 || index == 13 || index == 14);
			if (tracing) { phy_trace_begin(&trace, index); }
			receiver.irq_counts[index]++; receiver.irq_total++;
			if (receive_operation >= 54) {
				receiver.irq_last_elapsed_us[index] = elapsed_us();
			}
			irq_ack(index);
			if (index == 16) { receive_event(); }
			else if (index == 13) { receive_complete(); }
			else if (index == 19) { dev_preencode(); }
			else if (index == 18 && receive_operation >= 54) {
				uint32_t context = read32(BB + 0xf68), request = read32(BB + 0xf64);
				unsigned user = (context >> 4) & 15;
				if (receiver.irq_counts[index] == 1) {
					receiver.interleaver_first_f68 = context;
					receiver.interleaver_first_f64 = request;
				}
				receiver.interleaver_last_f68 = context;
				receiver.interleaver_last_f64 = request;
				receiver.interleaver_users[user]++;
				receiver.interleaver_b4c_before = read32(BB + 0xb4c);
				if (receive_operation >= 55) {
					/* Original 0x227e78..0x227e88: executed independently of
					 * conditional clock adjustment and MCS retry bookkeeping.
					 * Preserve the four-bit raw user; this is not MAC admission. */
					field(0xb4c0305, (user << 1) | 1);
					receiver.interleaver_updates++;
				}
				receiver.interleaver_b4c_after = read32(BB + 0xb4c);
				if (receive_operation >= 67 && dev_active && user != 8) {
					dev_mcs_apply(ar_dev_mcs_interleaver(&dev_mcs));
				}
				if (receive_operation >= 61 && dev_active && user == 0) {
					uint8_t first = (request >> 20) & 15;
					uint8_t second = (read32(BB + 0xb00) & 0x10) ? (request >> 16) & 15 : 255;
					receiver.retx_events++; receiver.retx_request_ids[first]++;
					receiver.retx_last_request = first | (uint32_t)second << 8;
					enum ar_dev_retx_action action = ar_dev_retx_feed(&dev_retx, first, second);
					if (action == AR_DEV_RETX_INVALID) { receiver.retx_errors++; }
					if (action == AR_DEV_RETX_DISABLE || action == AR_DEV_RETX_ENABLE) {
						receiver.retx_b00_before = read32(BB + 0xb00);
						/* 0x22c6ec writes the inverse enable value to B00 bit30.
						 * Preserve MCS/profile fields and ownership of all TX data. */
						field(0xb001e01, action == AR_DEV_RETX_DISABLE);
						receiver.retx_b00_after = read32(BB + 0xb00);
						if (action == AR_DEV_RETX_DISABLE) {
							uint32_t elapsed = elapsed_us();
							if (!receiver.retx_disable) { receiver.retx_first_disable_us = elapsed; }
							receiver.retx_disable++; receiver.retx_last_disable_us = elapsed;
						} else { receiver.retx_enable++; }
					}
					receiver.retx_last_id = dev_retx.last_id; receiver.retx_repeated = dev_retx.repeated;
					receiver.retx_phase = dev_retx.phase;
				}
			}
			else if (index == 11 && dev_active) {
				receiver.tx_end++;
				(void)ar_dev_tx_retire(&dev_tx, 0); receiver.tx_owned = dev_tx.owned;
			}
			else if (index == 12) {
				/* Stock EOP handler 0x2210cc obtains raw user from FBC. */
				receiver.eop_last = read32(BB + 0xfbc);
				receiver.eop_users[(receiver.eop_last >> 20) & 15]++;
				if (receive_operation >= 64) { dev_measure_eop(receiver.eop_last); }
				if (receiver.irq_counts[12] <= 4) {
					struct ar_rf_eop_snapshot *snapshot = &receiver.eop_snapshots[receiver.irq_counts[12] - 1];
					snapshot->elapsed_us = elapsed_us();
					snapshot->status = receiver.eop_last;
					snapshot->valid_indications = receiver.fch_locked;
					uint32_t old = read32(BB + 0xbb0);
					field(0xbb00701, 0); snapshot->header = read32(BB + 0xf00);
					field(0xbb00701, (old >> 7) & 1);
					read_control_header(snapshot->control);
				}
			}
			else if (index == 15) { receiver.error_last = read32(BB + 0xf48); }
			else if (index == 5) {
				/* Stock IRQ5 (0x21f804) byte-swaps E70 before PHY message
				 * 0x100. Preserve the raw numeric contract; units unknown. */
				uint32_t power = __builtin_bswap32(read32(BB + 0xe70));
				receiver.power_last = power;
				if (!receiver.power_samples || power < receiver.power_min) { receiver.power_min = power; }
				if (!receiver.power_samples || power > receiver.power_max) { receiver.power_max = power; }
				receiver.power_samples++;
				if (receiver.channel_count) {
					struct ar_rf_channel_info *channel = &receiver.channels[receiver.channel_index];
					channel->power_last = power;
					if (!channel->power_samples || power < channel->power_min) { channel->power_min = power; }
					if (!channel->power_samples || power > channel->power_max) { channel->power_max = power; }
					channel->power_samples++;
				}
			}
			else if (index == 2) {
				/* Stock 0x228066 selects behavior from E6C's raw user.
				 * This is sampled context: polling can lag the IRQ edge. */
				uint32_t command = read32(BB + 0xe6c);
				if (receiver.irq_counts[2] == 1) { receiver.source2_command_first = command; }
				receiver.source2_command_last = command;
				receiver.source2_users[(command >> 16) & 15]++;
				receiver.source2_modes[(command >> 20) & 3]++;
				uint32_t old = read32(BB + 0xc00);
				field(0xc000701, 1);
				receiver.snr_words[0] = read32(BB + 0xef0);
				receiver.snr_words[1] = read32(BB + 0xef4);
				field(0xc000701, (old >> 7) & 1);
			}
			else if (index == 14) {
				uint32_t old = read32(BB + 0xbb0);
				field(0xbb00701, 0); receiver.fch_last = read32(BB + 0xf00);
				field(0xbb00701, (old >> 7) & 1);
				unsigned user = (receiver.fch_last >> 9) & 15;
				if (automatic_recovery() && (user == 8 || user == 9)) {
					recovery_reset_pending |= ar_fch_monitor_observe(&fch_monitor, !!(receiver.fch_last & 0x80));
				}
				if (receive_operation >= 64 && (user == 8 || user == 9)) {
					/* 0x227b96 -> 0x21acb2 aliases both raw users to logical8. */
					phy_header_valid[8] = !!(receiver.fch_last & 0x80); phy_eop_seen[8] = 0;
					phy.last_fch = receiver.fch_last;
				}
				receiver.header_users[user]++;
				if (receiver.fch_last & 0x80) {
					if (!receiver.fch_locked) {
						receiver.first_valid_header = receiver.fch_last;
						receiver.first_valid_command = read32(BB + 0xe6c);
						receiver.first_valid_elapsed_us = elapsed_us();
						/* Stock control-header reader 0x22d30a uses this bank.
						 * Retain raw words for independent content/checksum validation. */
						read_control_header(receiver.first_valid_control);
						/* Compare bank selection and a delayed read before assuming
						 * zero control content is a decoder result. No acceptance
						 * decision is made from these diagnostic samples. */
						receiver.first_selector_old = read32(BB + 0xbb0);
						field(0xbb00701, 0);
						read_control_words(receiver.first_control_bank0);
						field(0xbb00701, 1);
						receiver.first_selector_set = read32(BB + 0xbb0);
						receiver.first_bank_delay_status = (uint32_t)delay_us(1);
						receiver.first_selector_delayed = read32(BB + 0xbb0);
						read_control_words(receiver.first_control_delayed);
						field(0xbb00701, (old >> 7) & 1);
					}
					if (receive_operation >= 51) {
						receiver.last_control_header = receiver.fch_last;
						read_control_header(receiver.last_control_words);
						/* 0x22d30a extracts seven bytes starting at raw bit 28.
						 * All-zero bytes fail 0x2299d2 (computed 0x1e).
						 * This rejects only that measured pattern; nonzero
						 * content is not thereby accepted as a MAC header. */
						if (!(receiver.last_control_words[0] >> 28) &&
						    !receiver.last_control_words[1] &&
						    !(receiver.last_control_words[2] & 0xfffff)) {
							receiver.empty_control_headers++;
						}
						if (receive_operation >= 52) {
							uint8_t control[7];
							for (unsigned i = 0; i < 7; i++) {
								unsigned bit = 28 + i * 8, word = bit / 32, shift = bit % 32;
								uint32_t value = receiver.last_control_words[word] >> shift;
								if (shift > 24) { value |= receiver.last_control_words[word + 1] << (32 - shift); }
								control[i] = (uint8_t)value;
							}
							receiver.last_control_check = ar_rf_control_checksum(control);
							if (receiver.last_control_check == control[0]) {
								receiver.matching_control_headers++;
								if (normal_search && !search_candidate_seen && (user == 8 || user == 9)) {
									search_candidate_seen = 1; search_candidate_started = read32(TIMER);
								}
								if (receive_operation >= 69 && (user == 8 || user == 9)) { sync_receive(control); }
							}
							else { receiver.bad_control_headers++; }
						}
					}
					receiver.fch_locked++; receiver.header_valid_users[user]++;
					/* Stock 0x228172's key 1 maps to internal MCS index 1.
					 * Diagnostic 50 forces only 0x222c0a's two MMIO effects
					 * once for that observed user/MCS. Stock's pending-MCS
					 * software conditions and host notification are not ported.
					 * No packet or MAC-acceptance count is synthesized. */
					uint32_t mode = (receiver.fch_last >> 4) & 3;
					uint32_t key = ((receiver.fch_last >> 13) & 7) << 16 |
						((receiver.fch_last & 15) % 3) << 8 |
						(mode == 3 ? UINT32_C(1) << 24 : mode);
					if (receive_operation >= 67 && (user == 8 || user == 9)) {
						unsigned mcs = 0;
						while (mcs < 25 && mcs_fch_keys[mcs] != key) { mcs++; }
						mcs_info.last_header = receiver.fch_last;
						if (mcs < 25) {
							mcs_info.observed_counts[mcs]++;
							if (mcs_info.first_observed_mcs == 25) { mcs_info.first_observed_mcs = mcs; }
							dev_mcs_apply(ar_dev_mcs_observe(&dev_mcs, mcs));
						} else { mcs_info.unknown_headers++; }
					}
					/* Operation65 tests the same effects after native login,
					 * at the stock-linked user8/key1 observation. It does not
					 * implement the pending-MCS policy that selects this call. */
					unsigned linked_buffer_probe = receive_operation == 65 && dev_active &&
						receiver.dev_state == 2 && receiver.assigned_slot == 0;
					if ((receive_operation == 50 || linked_buffer_probe) && user == 8 && key == 1 &&
					    !receiver.mcs_buffer_transitions) {
						receiver.mcs_buffer_header = receiver.fch_last;
						receiver.mcs_buffer_bf0_before = read32(BB + 0xbf0);
						receiver.mcs_buffer_d08_before = read32(BB + 0xd08);
						/* DEV mode 0 maps logical user 8 to buffer user 0. */
						field(0xbf00002, 1);
						field(0xd081001, 0);
						receiver.mcs_buffer_bf0_after = read32(BB + 0xbf0);
						receiver.mcs_buffer_d08_after = read32(BB + 0xd08);
						receiver.mcs_buffer_transitions = 1;
					}
				}
			}
			else if (index == 7) {
				receiver.command_last = read32(BB + 0xe6c);
				receiver.command_modes[(receiver.command_last >> 20) & 3]++;
				if (receive_operation == 76 && commands_ready) {
					/* Fixed mode-0 DEV frame. The copy-mode TX path does not
					 * yet use the original retained-buffer ownership flags.
					 * Non-preparation scan callbacks remain unimplemented. */
					(void)ar_command_step(&command_program,receiver.command_last,
						&command_state,1,0,0,read32(BB + 0x48c),command_write,command_byte,NULL);
				} else if (((receiver.command_last >> 20) & 3) == 0) {
					/* Historical diagnostic modes keep their bounded setup. */
					static const uint16_t offsets[] = {9, 0x5b, 0x83, 0x85, 0x58, 0x5e};
					static const uint8_t values[] = {0x0e, 3, 8, 0x11, 0, 0};
					for (unsigned byte = 0; byte < 6; byte++) {
						unsigned offset = offsets[byte], shift = (offset & 3) * 8;
						update(BB + (offset & ~3u), 255u << shift, (uint32_t)values[byte] << shift);
					}
					/* Compensation is loaded for transmit-direction commands,
					 * not globally while setting up receive-only operation. */
					if (receive_operation == 1 || receive_operation == 3 || receive_operation == 4 ||
					    (dev_active && command_state.cached_bank == 3 && ((receiver.command_last >> 16) & 15) == 0)) {
						if (dev_active) {
							field(0x4800101, 1);
							field(0x4a01008, 0xff); field(0x4a80008, 0xff);
						}
						field(0x4a00008, calibration[0]); field(0x4a00808, calibration[1]);
						field(0x4a41008, calibration[2]); field(0x4a41808, calibration[3]);
						field(0x48c0008, calibration[4] | (read32(BB + 0x48c) & 0x33));
						if (dev_active) { command_state.cached_bank = 2; }
					}
				}
			}
			if (tracing) { phy_trace_finish(&trace, index); }
		}
	receive_iterations++;
	uint32_t now = read32(TIMER);
	uint64_t elapsed = elapsed_ticks();
	unsigned expired = receive_milliseconds && (elapsed >= (uint64_t)receive_milliseconds * 40000 || receive_iterations >= 1000000);
	if (receive_operation >= 69 && !expired) { sync_poll((uint32_t)(elapsed / 40000)); }
	if ((receive_operation == 36 || receive_operation == 37 || receive_operation >= 39) && !expired) {
		/* Bounded DEV message 0x205, with a one-entry or supplied channel
		 * list; stock alternates floor(frame_us/1000)+2 and
		 * 2*(floor(frame_us/1000)+1) at list wrap (0x22f2e2..0x22f300).
		 * Header/DMA evidence suspends retries so this diagnostic cannot reset
		 * a candidate packet. Operation 51 excludes only known-invalid empty
		 * control headers; operations 52/53 exclude checksum failures. Neither
		 * checksum equality nor this latch implements stock linked state. */
		if (normal_search) {
			/* A valid header may precede its payload/DMA interrupt. Give the
			 * first candidate on each visit two frames to reach admission.
			 * Repeated unrelated headers cannot extend this native grace window.
			 * DMA ownership and an admitted transition always prevent retuning. */
			unsigned grace = search_candidate_seen &&
				(uint32_t)(now - search_candidate_started) < 2 * receiver.search_frame_us * 40;
			receiver.search_suspended = dev_active || receiver.acquisition_ready || receive_pending || grace;
		} else {
			unsigned candidate_headers = receiver.fch_locked;
			if (receive_operation == 51) { candidate_headers -= receiver.empty_control_headers; }
			if (receive_operation >= 52) { candidate_headers -= receiver.bad_control_headers; }
			if (candidate_headers || receiver.receive_events || receive_pending) {
				receiver.search_suspended = 1;
			}
		}
		if (!receiver.search_suspended &&
		    (uint32_t)(now - search_started) >= receiver.search_dwell_ms * 40000) {
			unsigned next = 0, wrap = 1;
			if (receiver.channel_count) {
				next = receiver.channel_index + 1;
				wrap = next == receiver.channel_count;
				if (wrap) { next = 0; }
				search_pll = channel_plls[next];
			}
			if (wrap && receive_operation != 40) {
				uint32_t short_dwell = receiver.search_frame_us / 1000 + 2;
				receiver.search_dwell_ms = receiver.search_dwell_ms == short_dwell ?
					2 * (receiver.search_frame_us / 1000 + 1) : short_dwell;
			}
			int result;
			if (receiver.channel_count) {
				/* Keep both engines reset during retuning and pending power
				 * acknowledgment. Do not retune if the hold times out. This
				 * diagnostic uses a stronger boundary than stock's live tune. */
				result = hold_engines();
				if (!result) {
					irq_ack(5); irq_ack(7);
					if (normal_search) { tune_context_plls(search_pll); }
					else { serial_word(receive_operation == 40 ? 0x90a : 0x905, search_pll); }
					if (receive_operation != 40) {
						field(0xa900010, receiver.channels[next].frequency_khz / 2000);
					}
					field(0xbf00002, 0);
					release_engines();
				}
			} else {
				serial_word(0x905, search_pll);
				field(0xbf00002, 0);
				result = idle();
			}
			if (result) {
				receiver.search_error = (uint32_t)result;
				failure(result);
				expired = 1;
			} else {
				receiver.search_restarts++;
				if (receiver.channel_count) {
					receiver.channel_index = next;
					if (receive_operation >= 54) { dev_config.receive_channel = next; }
					receiver.channel_wraps += wrap;
					receiver.frequency_khz = receiver.channels[next].frequency_khz;
					if (commands_ready) {
						const unsigned users[] = {8,11,0};
						for (unsigned i = 0; i < 3; i++) {
							unsigned direction = i != 2;
							(void)ar_command_update(&command_program,1,users[i],direction,3,receiver.frequency_khz,0);
							/* Normal search validates every channel as band 2. */
							(void)ar_command_update(&command_program,1,users[i],direction,1,2,0);
						}
					}
					receiver.channels[next].visits++;
				}
				search_candidate_seen = 0;
				memset(phy_header_valid, 0, sizeof phy_header_valid);
				memset(phy_eop_seen, 0, sizeof phy_eop_seen);
			}
			/* No catch-up reset bursts after a delayed cooperative poll. */
			search_started = read32(TIMER);
		}
	}
	if (expired) { stop_receive(); }
	else if (automatic_recovery()) {
		unsigned action = ar_fch_monitor_poll(&fch_monitor, timer_ticks() / 40000);
		if (action & AR_FCH_LOST) {
			if (!recovery.pending) { recovery.fch_losses++; request_recovery(1); }
		}
		recovery_reset_pending |= action & AR_FCH_RESET_RX;
		if (recovery.pending) { recover_host(); return; }
		if (recovery_reset_pending) {
			if (receive_pending) { recovery.reset_deferred++; }
			else {
				field(0x8000101, 1); field(0x80c0201, 1);
				int result = wait_bits(BB + 0xfa8, 0x100, 0x100, 2000);
				field(0x80c0201, 0);
				if (result) {
					recovery.failures++; recovery.last_error = (uint32_t)result;
					stop_receive(); (void)failure(result); return;
				}
				field(0x8000101, 0); recovery.rx_resets++; recovery_reset_pending = 0;
			}
		}
	}
}

static void stop_receive(void)
{
	if (receive_operation >= 54) { field(0x8000201, 1); dev_active = 0; }
	field(0x8000001, 1);
	link_event(0, receiver.dev_state);
	receiver.monitor_elapsed_us = elapsed_us();
	information.phase = AR_RF_PSRAM;
	progress(0xa006, receiver.irq_total, receiver.packets, receiver.bytes);
	static const uint16_t offsets[16] = {0x800,0x808,0x810,0x814,0x870,0x8a4,
		0x9e0,0x9e4,0xa60,0xb20,0xbb0,0xc00,0xcd0,0xde0,0xde4,0xfa8};
	for (unsigned i = 0; i < 16; i++) { receiver.context_words[i] = read32(BB + offsets[i]); }
	receiver.decoder_controls[0] = read32(BB + 0xbac);
	receiver.decoder_controls[1] = read32(BB + 0xbd4);
	static const uint16_t clock_offsets[] = {0,4,8,12,16,20,0x80,0x200};
	for (unsigned i = 0; i < 8; i++) { receiver.clock_controls[i] = read32(CLOCK + clock_offsets[i]); }
	static const uint16_t gpio_offsets[] = {0,4,8,12,16,0x2c,0x30,0x44};
	for (unsigned i = 0; i < 8; i++) { receiver.gpio_controls[i] = read32(0x0a10a000 + gpio_offsets[i]); }
	sync_info.pending = 0; host_prepared = 0;
}

int ar_rf_stop_host(void)
{
	if (receive_operation != 76 ||
	    (information.phase != AR_RF_RECEIVING && information.phase != AR_RF_PSRAM)) { return -11; }
	if (information.phase == AR_RF_RECEIVING) {
		/* Stop submissions, then drain an outstanding DMA before resetting
		 * its producer engine. Resetting RX first can remove the completion
		 * we need to establish ownership. No new DMA is submitted while this
		 * cooperative call runs. On timeout retain storage and refuse reuse. */
		field(0x8000201, 1);
		recovery.stop_stage = 1; recovery.stop_pending_rx = receive_pending;
		int result = 0;
		if (receive_pending) {
			result = wait_bits(0xe0801000u + irq_vector(13) * 4, 1, 1, 2000);
			if (!result) { irq_ack(13); receive_pending = 0; lifecycle.cancelled_rx++; }
		}
		if (!result) { recovery.stop_stage = 2; result = hold_engines(); }
		recovery.stop_fa8 = read32(BB + 0xfa8);
		recovery.stop_irq13 = read8(0xe0801000u + irq_vector(13) * 4);
		if (!result) { recovery.stop_stage = 3; }
		lifecycle.last_change_ms = elapsed_ms();
		stop_receive();
		if (result) { lifecycle.last_error = (uint32_t)result; return failure(result); }
		lifecycle.cancelled_tx += dev_tx.owned;
		ar_dev_tx_init(&dev_tx); receiver.tx_owned = 0;
		if (receive_operation >= 67) { dev_mcs_apply(ar_dev_mcs_connect(&dev_mcs, 0)); }
		receiver.dev_state = 0; receiver.assigned_slot = 8; receiver.acquisition_ready = 0;
		field(0xcf41501, 0); receiver.sync_ack_pending = 0;
		lifecycle.stops++;
	}
	host_prepared = 0; host_start_required = 1;
	fch_monitor.enabled = 0; recovery.pending = 0; recovery_reset_pending = 0;
	return 0;
}

static int prepare_saved_host(void)
{
	/* configure_monitor resets native protocol/PHY state, but the original
	 * host's current candidate list belongs to configuration, not that state. */
	uint8_t candidates[AR_DEV_MAX_CANDIDATES][4];
	size_t count = dev_config.candidate_count;
	memcpy(candidates, dev_candidates, count * 4);
	int result = configure_monitor(host_config.frequency, host_config.milliseconds,
		host_config.identity, 76, host_config.channels, host_config.length, host_config.deferred);
	if (!result) {
		memcpy(dev_candidates, candidates, count * 4); dev_config.candidate_count = count;
	}
	return result;
}
static void recover_host(void)
{
	unsigned reason = recovery.pending;
	recovery.attempts++; recovery.last_reason = reason; recovery.last_ms = elapsed_ms();
	int result = ar_rf_stop_host();
	if (!result) { result = prepare_saved_host(); }
	if (!result) {
		host_prepared = 0; host_start_required = 0;
		result = start_prepared_receive();
	}
	recovery.pending = 0;
	if (result) { recovery.failures++; recovery.last_error = (uint32_t)result; }
	else { recovery.completed++; }
}
const struct ar_rf_recovery_info *ar_rf_recovery_information(void)
{
	recovery.monitor_state = fch_monitor.enabled | fch_monitor.locked << 1 | fch_monitor.forced << 2;
	recovery.monitor_missed = fch_monitor.missed; recovery.monitor_threshold = fch_monitor.threshold;
	recovery.monitor_interval_ms = fch_monitor.interval_ms;
	return &recovery;
}
int ar_rf_host_control(unsigned operation, size_t payload_length)
{
	if (payload_length) { return -22; }
	if (operation > 3) { return -95; }
	if (!host_config.valid || receive_operation != 76) { return -7; }
	if (information.error) { return (int32_t)information.error; }
	if (operation == 0) {
		if (!host_initialized || information.phase == AR_RF_RECEIVING) { return -7; }
		int result = host_prepared ? 0 : prepare_saved_host();
		if (result) { return result; }
		host_prepared = 0; host_start_required = 0;
		return start_prepared_receive();
	}
	if (operation == 1) {
		if (!host_initialized || information.phase != AR_RF_RECEIVING) { return -7; }
		return ar_rf_stop_host();
	}
	if (operation == 2) {
		if (host_initialized) { return -7; }
		int result = prepare_saved_host(); if (result) { return result; }
		/* Original sync-object creation in 0x22de32 clears time validity.
		 * Stop/start and link loss retain the offset; deinit/init recreates it.
		 * Do not bind AP time to receive_started, which changes on restart. */
		ar_ap_time_reset(&ap_time);
		host_initialized = 1; host_start_required = 1; lifecycle.initializations++;
		return 0;
	}
	if (!host_initialized || information.phase == AR_RF_RECEIVING) { return -7; }
	host_initialized = host_prepared = 0; host_start_required = 1; lifecycle.deinitializations++;
	return 0;
}
const struct ar_rf_lifecycle_info *ar_rf_lifecycle_information(void)
{
	lifecycle.state = information.error ? 3 : !host_initialized ? 0 : information.phase == AR_RF_RECEIVING ? 2 : 1;
	return &lifecycle;
}

const struct ar_rf_rx_info *ar_rf_receive_information(void) { return &receiver; }

int ar_rf_host_registers(unsigned offset, unsigned length, uint8_t output[256])
{
	if (!output) { return -22; }
	/* Original 0x24612c accepts unaligned and empty windows in a 4 KiB
	 * page. Check bounds before MMIO, including offset 4096 with length 0. */
	if (length > 256 || offset > 4096 || length > 4096 - offset) { return -6; }
	if (information.phase == AR_RF_OFF) { return -11; }
	/* Stock's core handler leaves the unused reply tail untouched. Do not
	 * expose previous buffer contents through the fixed-size host reply. */
	memset(output, 0, 256);
	for (unsigned i = 0; i < length; i++) {
		unsigned byte = offset + i;
		/* Preserve one aligned volatile word read per byte, as in stock.
		 * Coalescing reads would change behavior for changing registers. */
		output[i] = read32(BB + (byte & ~3u)) >> ((byte & 3) * 8);
	}
	return 0;
}

int ar_rf_read_registers(unsigned offset, unsigned length, uint8_t *output)
{
	/* Diagnostic readback is restricted to the known configuration page,
	 * outside active monitoring, and never exposes arbitrary MCU memory. */
	if (information.phase != AR_RF_PSRAM || !output || !length || length > 256 ||
	    (offset & 3) || (length & 3) || offset > 0xe00 - length) { return -22; }
	for (unsigned i = 0; i < length; i += 4) {
		uint32_t word = read32(BB + offset + i);
		for (unsigned byte = 0; byte < 4; byte++) { output[i + byte] = word >> (byte * 8); }
	}
	return 0;
}
