/* SPDX-License-Identifier: MIT */
#ifndef AR_FRAME_STAMP_H
#define AR_FRAME_STAMP_H
#include <stdint.h>

/* Open8030 frame stamps: the AP's clock on the ground, to the frame (Open8030
 * at both ends; a stock AP sends none, and the ground then knows it is stock).
 * The AP's clock is its 40 MHz timer. At its IRQ 5 (between the broadcast and
 * the receive slot) the AP takes the time; the PHY control block it writes
 * next (IRQ 17, the next frame's broadcast under way) carries it in the
 * header after that, as command 10: 32 bits of us packed as command 7 packs
 * its ms - bits 0..23 in payload bytes 0..2, 24..27 in the low nibble of
 * byte 3 (its high nibble does not go over the air: bits 28..31 were lost
 * there, a 2^28 us jump on the bench), 28..31 in the TTL nibble - wrapping
 * every 71.6 minutes, which both ends unwrap. A ground decodes that header before its
 * own IRQ 5 of that frame, so the stamp is the AP's time at the IRQ 5 before
 * the ground's latest. Both ends then read the AP's clock at their own latest
 * IRQ 5 - the same moment of the same frame, give or take the IRQs' jitter. */
#define AR_PHY_STAMP 10u
#define AR_FRAME_STAMP_MASK ((UINT64_C(1) << 32) - 1)
struct ar_frame_stamp {
	uint64_t irq5[2];          /* our timer (40 MHz ticks) at the latest IRQ 5 and the one before */
	uint32_t frames;           /* IRQ 5s seen */
	int64_t offset;            /* ground: the AP's timer minus ours, ticks (valid) */
	uint32_t taken, rejected;  /* ground: stamps taken, refused (a frame or more off) */
	uint32_t misses;           /* ... refused in a row */
	int32_t residual_us;       /* the last one against the offset */
	uint8_t valid;             /* ground: taken on this link */
};
/* A new link (or none): the offset is not known any more. */
void ar_frame_stamp_reset(struct ar_frame_stamp *stamp);
void ar_frame_stamp_irq5(struct ar_frame_stamp *stamp, uint64_t ticks);
/* AP: the stamp its next PHY control block carries (32 bits of us). */
uint64_t ar_frame_stamp_value(const struct ar_frame_stamp *stamp);
/* Ground: a stamp from the AP's header. 0 taken, -1 refused: a stamp more
 * than 500 us off the offset is a frame off (an IRQ 5 missed between) - three
 * in a row are a new clock and taken. */
int ar_frame_stamp_receive(struct ar_frame_stamp *stamp, uint64_t stamp_us);
/* The AP's clock at our latest IRQ 5, us modulo 2^32 (the ground through its
 * offset). 0: known. */
int ar_frame_stamp_ap_us(const struct ar_frame_stamp *stamp, unsigned ap_role, uint64_t *us);
#endif
