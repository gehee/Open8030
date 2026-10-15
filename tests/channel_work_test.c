/* SPDX-License-Identifier: MIT */
#include "ar_channel_work.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct ar_channel_request_state request_state;
static unsigned posts, writes;
static int post_result;
static uint32_t expected_selector;
static uint16_t expected_channel;
static int post(uint32_t destination, uint32_t message, const uint8_t *payload,
	size_t length, uint32_t timeout, void *context)
{
	assert(context == &request_state && destination == 1 && message == 0x108);
	assert(length == 8 && timeout == UINT32_MAX);
	uint32_t selector = 0;
	for (unsigned i = 0; i < 4; i++) { selector |= (uint32_t)payload[i] << (8*i); }
	assert(selector == expected_selector);
	assert(((unsigned)payload[4] | (unsigned)payload[5] << 8) == expected_channel);
	assert(!payload[6] && !payload[7]);
	if (selector == 1) {
		assert(request_state.pending_channel == expected_channel);
		assert(request_state.flags & 8);
	} else {
		assert(request_state.broadcast_channel == (uint8_t)expected_channel);
		assert(request_state.flags & 16);
	}
	posts++;
	return post_result;
}
static struct ar_channel_scan_state scan;
static void write_field(uint32_t field, uint32_t value, void *context)
{
	assert(context == &scan && field == 0x08080f01 && value == 1);
	for (unsigned i = 0; i < 6; i++) { assert(!scan.group_counts[i]); }
	for (unsigned i = 0; i < 128; i++) { assert(!scan.channel_counts[i]); }
	/* The other state is deliberately still present during BOTH writes. */
	assert(scan.active == 0xa5 && scan.best_group == 0xa5);
	assert(scan.winners[0] == 0xa5 && scan.marks[127] == 0xa5);
	assert(scan.records[127][13] == 0xa5);
	writes++;
}
int main(void)
{
	uint32_t rows[8]; uint8_t groups[8];
	for (unsigned i = 0; i < 8; i++) { rows[i] = 0x01000000; groups[i] = 0xf0 | (i & 1); }
	int32_t scores[8] = {INT32_MIN, -1, 65535, 32768, -32768, INT32_MAX, 0, 42};
	struct ar_channel_candidate output[4];
	assert(ar_channel_candidates(rows, groups, scores, 8, 3, 7, output) == 4);
	assert(output[0].channel == 3 && output[0].score == -32768);
	assert(output[1].channel == 4 && output[1].score == -32768);
	assert(output[2].channel == 1 && output[2].score == -1);
	assert(output[3].channel == 2 && output[3].score == -1);
	assert(ar_channel_candidates(rows, groups, scores, 8, 0, 7, output) == 4);
	assert(output[0].channel == 4 && output[1].channel == 2);
	assert(output[2].channel == 0 && output[3].channel == 6);
	rows[4] = 0;
	assert(ar_channel_candidates(rows, groups, scores, 8, 0, 7, output) == 3);
	assert(output[0].channel == 2 && output[3].channel == 255 && !output[3].score);
	/* No eligible group: current wins regardless of membership/enabled bit. */
	assert(ar_channel_candidates(rows, groups, scores, 8, 15, 4, output) == 1);
	assert(output[0].channel == 4 && output[0].score == -32768);
	struct ar_channel_candidate saved[4]; memcpy(saved, output, sizeof(saved));
	assert(ar_channel_candidates(rows, groups, scores, 8, 3, 8, output) == -22);
	assert(ar_channel_candidates(rows, groups, scores, 0, 3, 0, output) == -22);
	assert(ar_channel_candidates(rows, groups, scores, 129, 3, 0, output) == -22);
	assert(ar_channel_candidates(NULL, groups, scores, 8, 3, 0, output) == -22);
	assert(ar_channel_candidates(rows, NULL, scores, 8, 3, 0, output) == -22);
	assert(ar_channel_candidates(rows, groups, NULL, 8, 3, 0, output) == -22);
	assert(ar_channel_candidates(rows, groups, scores, 8, 3, 0, NULL) == -22);
	assert(!memcmp(saved, output, sizeof(saved)));

	for (unsigned flag = 0; flag < 256; flag++) {
		for (unsigned selector = 0; selector < 3; selector++) {
			for (unsigned failure = 0; failure < 2; failure++) {
				request_state = (struct ar_channel_request_state){0x9876, 0x54, flag};
				expected_selector = selector; expected_channel = 0xabcd;
				post_result = failure ? -6 : 0;
				assert(ar_channel_request(&request_state, selector, expected_channel,
					post, &request_state) == post_result);
				assert(request_state.flags == (flag | (selector == 1 ? 8 : 16) |
					(failure ? 0 : selector == 1 ? 32 : 64)));
				assert(request_state.pending_channel == (selector == 1 ? 0xabcd : 0x9876));
				assert(request_state.broadcast_channel == (selector == 1 ? 0x54 : 0xcd));
			}
		}
	}
	assert(posts == 1536);
	struct ar_channel_request_state saved_request = request_state;
	assert(ar_channel_request(&request_state, 1, 0, NULL, NULL) == -22);
	assert(ar_channel_request(NULL, 1, 0, post, NULL) == -22);
	assert(!memcmp(&saved_request, &request_state, sizeof(saved_request)));
	assert(posts == 1536);

	memset(&scan, 0xa5, sizeof(scan));
	assert(ar_channel_scan_reset(NULL, write_field, &scan) == -22);
	assert(ar_channel_scan_reset(&scan, NULL, &scan) == -22);
	assert(!writes && scan.group_counts[0] == 0xa5a5);
	assert(ar_channel_scan_reset(&scan, write_field, &scan) == 0 && writes == 2);
	assert(scan.best_group == 6 && !scan.active);
	for (unsigned i = 0; i < 6; i++) { assert(scan.winners[i] == 255); }
	for (unsigned i = 0; i < 128; i++) {
		assert(!scan.marks[i]);
		for (unsigned j = 0; j < 14; j++) { assert(!scan.records[i][j]); }
	}
	puts("Channel work tests passed");
	return 0;
}
