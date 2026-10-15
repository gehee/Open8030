/* SPDX-License-Identifier: MIT */
#include "ar_tx_packet.h"
#include "ar_mac.h"
#include <string.h>

static uint32_t word(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
		(uint32_t)p[3] << 24;
}
static void put(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		p[i] = (uint8_t)(value >> (8 * i));
}

int ar_tx_packet_finish(struct ar_tx_packet_state *state,
	struct ar_tx_stream_registry *registry, struct ar_tx_packet_statistics *statistics,
	uint32_t submitted, uint32_t header,
	const struct ar_tx_stream_acquire_ops *ops, void *context)
{
	if (!state || !registry || !ops || !ops->lock || !ops->unlock || !ops->clock_us)
		return -22;
	unsigned selector = (state->configuration >> 24) & 15;
	if (selector < 8 && state->present[selector] && state->reset_masks[selector]) {
		(void)ops->lock(registry->mutex, UINT32_MAX, context);
		for (unsigned port = 1; port < 5; ++port) {
			if (!(state->reset_masks[selector] & (1u << port)))
				continue;
			struct ar_tx_stream *stream = registry->streams[selector][port][0];
			if (!stream || !stream->queue)
				break;
			struct ar_tx_stream_queue *queue = stream->queue;
			(void)ops->lock(queue->mutex, UINT32_MAX, context);
			queue->acquired_bytes = 0;
			ops->unlock(queue->mutex, context);
		}
		state->reset_masks[selector] = 0;
		ops->unlock(registry->mutex, context);
	}
	if (!statistics || !statistics->samples || !statistics->capacity ||
	    statistics->head >= statistics->capacity || statistics->head >= statistics->sample_count)
		return -22;
	struct ar_tx_packet_sample *sample = &statistics->samples[statistics->head];
	uint16_t budget16 = (uint16_t)state->budget, submitted16 = (uint16_t)submitted;
	uint16_t payload16 = (uint16_t)(submitted16 - header);
	sample->budget = budget16;
	sample->submitted = submitted16;
	uint32_t ms = (uint32_t)(ops->clock_us(context) / 1000);
	if (statistics->head >= statistics->capacity || statistics->head >= statistics->sample_count)
		return -22;
	statistics->samples[statistics->head].ms = ms;
	statistics->head = (statistics->head + 1) % statistics->capacity;
	statistics->budget_total += budget16;
	statistics->submitted_total += submitted16;
	statistics->payload_total += payload16;
	return 0;
}

int ar_tx_packet_prepare(struct ar_tx_packet_state *state,
	struct ar_tx_stream_registry *registry,
	const struct ar_tx_stream_acquire_ops *stream_ops,
	const struct ar_tx_packet_ops *ops, void *context)
{
	if (!state || !registry || !stream_ops || !stream_ops->lock ||
	    !stream_ops->unlock || !stream_ops->clock_us || !stream_ops->wait ||
	    !ops || !ops->policy_value || !ops->policy_word || !ops->cache || !ops->submit ||
	    state->outband_length > UINT32_MAX - 8 ||
	    state->budget < 8 + state->outband_length)
		return -22;
	state->packet[28] = state->request;
	memset(state->packet + 32, 0, 200);
	uint32_t available[5];
	for (unsigned port = 0; port < 5; ++port) {
		int result = ar_tx_stream_available(registry,
			(state->configuration >> 24) & 15, port, &available[port], stream_ops, context);
		if (result)
			return result;
	}
	state->packet[7] = 0;
	uint32_t remaining = state->budget - 8 - state->outband_length;
	unsigned limit = 5;
	if (state->policy_enabled && state->mode != 3) {
		uint32_t policy_value = 0;
		if (!ops->policy_value((state->configuration >> 24) & 15, &policy_value, context)) {
			uint32_t threshold = (ops->policy_word(context) >> 16) * 120;
			if (threshold && threshold < policy_value) {
				state->policy_control = (state->policy_control & 0xff0fffff) | 0x100000;
				limit = 1;
			}
		}
	}
	for (unsigned port = 0; port < limit; ++port) {
		if (remaining <= 4)
			break;
		if (available[port]) {
			remaining -= 4;
			uint32_t requested = available[port] < remaining ? available[port] : remaining;
			uint8_t *descriptor = state->packet + 8 + 4 * state->packet[7];
			put(descriptor, (word(descriptor) & 0xf00) | port | (requested << 12));
			struct ar_tx_stream_span spans[5] = {{0}};
			(void)ar_tx_stream_acquire(registry, (state->configuration >> 24) & 15,
				port, 0, requested, spans, 0, stream_ops, context);
			for (unsigned i = 0; i < 5; ++i) {
				put(state->packet + 32 + port * 40 + i * 8, spans[i].address);
				put(state->packet + 36 + port * 40 + i * 8, spans[i].length);
			}
			remaining -= requested;
			state->packet[7]++;
		}
		if (port + 1 == limit)
			break;
		if (!(state->mode == 3 && !state->role)) {
			unsigned selector = (state->configuration >> 24) & 15;
			if (selector > 7 || !state->present[selector] ||
			    (state->user_flags[selector][1] && !state->user_flags[selector][0]))
				break;
		}
	}
	state->packet[4] = (uint8_t)state->configuration;
	state->configuration = (state->configuration & 0xffffff00) |
		(uint8_t)(state->configuration + 1);
	state->packet[6] = state->outband_flag;
	state->packet[2] = state->packet[3] = 0;
	uint32_t header = 8 + 4 * state->packet[7];
	uint16_t checksum = ar_mac_checksum16(state->packet, header);
	state->packet[2] = (uint8_t)checksum;
	state->packet[3] = (uint8_t)(checksum >> 8);
	ops->cache(state->packet_address, header, context);
	ops->submit(state->packet_address, header, context);
	uint32_t submitted = header;
	if (state->outband_length) {
		ops->cache(state->outband_address, state->outband_length, context);
		ops->submit(state->outband_address, state->outband_length, context);
		submitted += state->outband_length;
	}
	for (unsigned i = 0; i < state->packet[7]; ++i) {
		if (i >= 5 || state->packet[8 + 4 * i] >= 5)
			return -22;
		uint8_t *spans = state->packet + 32 + state->packet[8 + 4 * i] * 40;
		for (unsigned j = 0; j < 5; ++j) {
			uint32_t length = word(spans + j * 8 + 4);
			if (!length)
				continue;
			uint32_t address = word(spans + j * 8);
			ops->cache(address, length, context);
			ops->submit(address, length, context);
			submitted += word(spans + j * 8 + 4);
		}
	}
	if (state->budget - remaining != submitted)
		return -95;
	state->configuration = (state->configuration & 0xffff00ff) | (uint32_t)state->request << 8;
	ops->submit(0, 0, context);
	return 0;
}
