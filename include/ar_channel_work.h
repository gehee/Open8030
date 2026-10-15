/* SPDX-License-Identifier: MIT */
#ifndef AR_CHANNEL_WORK_H
#define AR_CHANNEL_WORK_H
#include <stddef.h>
#include <stdint.h>

/* Native representations, not original firmware structures or wire layouts. */
struct ar_channel_candidate { uint16_t channel; int16_t score; };
/* Rank up to four enabled channels by the signed low 16 bits of their score.
 * Group 3 is a wildcard; ties retain catalog order. If no channel qualifies,
 * return the current channel even if it is disabled or in a different group.
 * Returns 1..4, or native -22 for invalid storage/current/count (1..128).
 * Unused entries contain channel255/score0. */
int ar_channel_candidates(const uint32_t *rows, const uint8_t *groups,
	const int32_t *scores, size_t count, uint8_t group, uint16_t current,
	struct ar_channel_candidate output[4]);

struct ar_channel_request_state {
	uint16_t pending_channel;
	uint8_t broadcast_channel, flags;
};
/* Post destination1/message0x108/timeoutUINT32_MAX. The 8-byte payload contains
 * LE32 selector and LE16 channel; the two original uninitialized padding bytes
 * are deliberately zeroed. The callback observes pending state already set.
 * Return0 indicates posting success only, not completed tuning. */
typedef int (*ar_channel_post)(uint32_t destination, uint32_t message,
	const uint8_t *payload, size_t length, uint32_t timeout, void *context);
int ar_channel_request(struct ar_channel_request_state *state,
	uint32_t selector, uint16_t channel, ar_channel_post post, void *context);

/* Separate auxiliary scan state, not the ten-sample noise-score histories.
 * Record bytes remain opaque until their consumers have been compared. */
struct ar_channel_scan_state {
	uint16_t group_counts[6], channel_counts[128];
	uint8_t records[128][14], marks[128], winners[6], best_group, active;
};
typedef void (*ar_channel_field)(uint32_t field, uint32_t value, void *context);
/* Clears counts first, writes field0x08080f01=1 twice, then clears the other
 * state. Preserve this ordering; the two identical writes are intentional.
 * Native -22 for a missing state/writer, with no side effects. */
int ar_channel_scan_reset(struct ar_channel_scan_state *state,
	ar_channel_field write, void *context);
#endif
