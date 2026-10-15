/* SPDX-License-Identifier: MIT */
#include "ar_tx_packet.h"
#include "ar_tx_retire.h"
#include "ar_mac.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_tx_packet_state state;
	struct ar_tx_stream_registry registry;
	struct ar_tx_stream stream;
	struct ar_tx_stream_queue queue;
	struct ar_tx_stream_entry entries[7];
	unsigned locks, unlocks, cache_calls, submits, terminal, buffers, wakes;
	unsigned policy_value_calls, policy_word_calls;
	uint32_t addresses[30], lengths[30];
};
static int lock(uint32_t mutex, uint32_t timeout, void *context)
{
	struct fixture *f = context;
	assert(mutex == 1 || mutex == 2);
	assert(timeout == UINT32_MAX);
	f->locks++;
	return -1;
}
static void unlock(uint32_t mutex, void *context)
{
	struct fixture *f = context;
	assert(mutex == 1 || mutex == 2);
	f->unlocks++;
}
static uint64_t clock_us(void *context) { (void)context; return 100999; }
static int wait(uint32_t signal, uint32_t timeout, void *context)
{
	(void)signal; (void)timeout; (void)context;
	assert(0);
	return -1;
}
static int policy_value(unsigned selector, uint32_t *value, void *context)
{
	struct fixture *f = context;
	assert(selector == 1);
	f->policy_value_calls++;
	*value = 121;
	return 0;
}
static uint32_t policy_word(void *context)
{
	struct fixture *f = context;
	f->policy_word_calls++;
	return 1u << 16;
}
static void cache(uint32_t address, uint32_t length, void *context)
{
	struct fixture *f = context;
	assert(address && length);
	f->cache_calls++;
}
static void submit(uint32_t address, uint32_t length, void *context)
{
	struct fixture *f = context;
	assert(f->submits < 30 && f->locks == f->unlocks);
	f->addresses[f->submits] = address;
	f->lengths[f->submits++] = length;
	if (!address && !length)
		f->terminal++;
}
static void buffer(uint32_t callback, uint32_t argument, void *context)
{
	struct fixture *f = context;
	assert(callback == 0x100 && argument < 7);
	f->buffers++;
}
static void wake(uint32_t signal, void *context)
{
	struct fixture *f = context;
	assert(signal == 3);
	f->wakes++;
}
static const struct ar_tx_stream_acquire_ops stream_ops = {lock, unlock, clock_us, wait};
static const struct ar_tx_stream_ops release_ops = {lock, unlock, wake, buffer};
static const struct ar_tx_packet_ops packet_ops = {policy_value, policy_word, cache, submit};
static void release(unsigned selector, unsigned port, unsigned direction,
	uint32_t length, void *context)
{
	struct fixture *f = context;
	assert(!ar_tx_stream_release(&f->registry, selector, port, direction,
		length, &release_ops, f));
}
static void init(struct fixture *f)
{
	memset(f, 0, sizeof *f);
	f->registry.mutex = 1;
	f->registry.streams[1][1][0] = &f->stream;
	f->stream = (struct ar_tx_stream){.references = 2, .flags = 4,
		.queue = &f->queue, .percentage = 100};
	f->queue = (struct ar_tx_stream_queue){.capacity = 140, .write = 40,
		.used = 40, .pending = 16, .mutex = 2, .signal = 3,
		.entries = f->entries, .entry_count = 7};
	for (unsigned i = 0; i < 7; ++i)
		f->entries[i] = (struct ar_tx_stream_entry){.address = 0x400000 + i * 64,
			.length = 8, .argument = i, .callback = 0x100};
	f->state = (struct ar_tx_packet_state){.configuration = 0x011234ff,
		.packet_address = 0x283000, .outband_address = 0x285000,
		.outband_length = 16, .budget = 64, .request = 7, .outband_flag = 0x85,
		.mode = 3, .policy_control = 0xabcdef12};
	f->state.present[1] = 1;
	memset(f->state.packet, 0xa5, sizeof f->state.packet);
}
static int prepare(struct fixture *f)
{
	return ar_tx_packet_prepare(&f->state, &f->registry, &stream_ops, &packet_ops, f);
}
int main(void)
{
	struct fixture f;
	init(&f);
	assert(!prepare(&f) && f.state.packet[7] == 1 && f.state.packet[8] == 1);
	assert(f.state.configuration == 0x01120700 && f.state.packet[4] == 255);
	assert(f.state.packet[28] == 7 && f.state.packet[6] == 0x85);
	assert(f.state.packet[9] == 5 && f.state.packet[10] == 1);
	uint16_t checksum = ar_mac_checksum16(f.state.packet, 12);
	assert(f.state.packet[2] == (uint8_t)checksum && f.state.packet[3] == (checksum >> 8));
	assert(f.stream.references == 3 && f.entries[0].produced == 8 && f.entries[1].produced == 8);
	assert(f.submits == 5 && f.cache_calls == 4 && f.terminal == 1);
	assert(f.addresses[0] == 0x283000 && f.lengths[0] == 12);
	assert(f.addresses[1] == 0x285000 && f.lengths[1] == 16);
	assert(f.addresses[2] == 0x400000 && f.lengths[2] == 8 && f.lengths[3] == 8);
	struct ar_tx_retire_ring ring = {.configuration = f.state.configuration,
		.head = 1, .owned = 1};
	ring.packets[0] = (struct ar_tx_retire_packet){f.state.packet, sizeof f.state.packet};
	assert(!ar_tx_retire(&ring, 0, release, &f));
	assert(!ring.owned && ring.tail == 1 && f.stream.references == 2);
	assert(f.buffers == 2 && f.wakes == 1 && !f.queue.used && !f.queue.pending);

	init(&f);
	f.stream.percentage = 50;
	assert(prepare(&f) == -95 && !f.terminal && f.submits == 3);
	assert(f.state.configuration == 0x01123400 && f.stream.references == 3);
	assert(f.entries[0].produced == 8 && !f.entries[1].produced && f.queue.used == 40);
	init(&f);
	f.queue.used = 140;
	f.queue.write = 0;
	f.state.budget = 128;
	assert(prepare(&f) == -95 && !f.terminal && f.submits == 7);
	assert(f.entries[4].produced == 8 && !f.entries[5].produced);

	init(&f);
	f.state.policy_enabled = 1;
	f.state.mode = 0;
	assert(!prepare(&f) && !f.state.packet[7]); /* The policy gate limits selection to port 0. */
	assert(f.state.policy_control == 0xab1def12 && f.policy_value_calls == 1 && f.policy_word_calls == 1);
	assert(f.stream.references == 2 && !f.entries[0].produced);
	init(&f);
	f.state.mode = 0;
	f.state.user_flags[1][1] = 1;
	assert(!prepare(&f) && !f.state.packet[7]);
	init(&f);
	f.state.budget = 28;
	assert(!prepare(&f) && !f.state.packet[7]); /* Four remaining bytes cannot hold payload. */
	init(&f);
	f.state.budget = 23;
	assert(prepare(&f) == -22 && !f.locks && !f.submits);
	init(&f);
	f.queue.entry_count = 1;
	assert(prepare(&f) == -22 && f.stream.references == 2 && f.locks == f.unlocks);
	assert(!f.submits);
	puts("TX packet acquisition/retirement, segment order, policy gates and partial assertion outcomes passed");
}
