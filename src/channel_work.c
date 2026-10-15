/* SPDX-License-Identifier: MIT */
#include "ar_channel_work.h"
#include <string.h>

static int16_t short_score(int32_t score)
{
	uint16_t low = (uint16_t)score;
	return low < 32768 ? (int16_t)low : (int16_t)((int32_t)low - 65536);
}

int ar_channel_candidates(const uint32_t *rows, const uint8_t *groups,
	const int32_t *scores, size_t count, uint8_t group, uint16_t current,
	struct ar_channel_candidate output[4])
{
	if (!rows || !groups || !scores || !output || !count || count > 128 ||
		current >= count) { return -22; }
	for (unsigned i = 0; i < 4; i++) {
		output[i] = (struct ar_channel_candidate){255, 0};
	}
	unsigned used = 0;
	for (size_t i = 0; i < count; i++) {
		if (!(rows[i] & 0x01000000) || (group != 3 && (groups[i] & 15) != group)) { continue; }
		int16_t score = short_score(scores[i]);
		for (unsigned j = 0; j < 4; j++) {
			if (output[j].channel == 255 || score < output[j].score) {
				for (unsigned k = 3; k > j; k--) { output[k] = output[k-1]; }
				output[j] = (struct ar_channel_candidate){(uint16_t)i, score};
				if (used < 4) { used++; }
				break;
			}
		}
	}
	if (!used) {
		output[0] = (struct ar_channel_candidate){current, short_score(scores[current])};
		used = 1;
	}
	return (int)used;
}

int ar_channel_request(struct ar_channel_request_state *state,
	uint32_t selector, uint16_t channel, ar_channel_post post, void *context)
{
	if (!state || !post) { return -22; }
	if (selector == 1) { state->flags |= 8; state->pending_channel = channel; }
	else { state->flags |= 16; state->broadcast_channel = (uint8_t)channel; }
	uint8_t payload[8] = {0};
	for (unsigned i = 0; i < 4; i++) { payload[i] = selector >> (8*i); }
	payload[4] = (uint8_t)channel; payload[5] = channel >> 8;
	int result = post(1, 0x108, payload, sizeof(payload), UINT32_MAX, context);
	if (!result) { state->flags |= selector == 1 ? 32 : 64; }
	return result;
}

int ar_channel_scan_reset(struct ar_channel_scan_state *state,
	ar_channel_field write, void *context)
{
	if (!state || !write) { return -22; }
	memset(state->group_counts, 0, sizeof(state->group_counts));
	memset(state->channel_counts, 0, sizeof(state->channel_counts));
	write(0x08080f01, 1, context);
	write(0x08080f01, 1, context);
	memset(state->winners, 255, sizeof(state->winners));
	state->best_group = 6;
	state->active = 0;
	memset(state->records, 0, sizeof(state->records));
	memset(state->marks, 0, sizeof(state->marks));
	return 0;
}
