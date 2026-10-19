/* SPDX-License-Identifier: MIT */
#include "ar_frame_stamp.h"
#include <string.h>

#define REFUSE_TICKS 20000   /* 500 us: no IRQ jitter, a frame (3.25 ms and up) */

void ar_frame_stamp_reset(struct ar_frame_stamp *stamp)
{
	if (!stamp) { return; }
	stamp->offset = 0; stamp->valid = 0; stamp->misses = 0; stamp->residual_us = 0;
}

void ar_frame_stamp_irq5(struct ar_frame_stamp *stamp, uint64_t ticks)
{
	if (!stamp) { return; }
	stamp->irq5[1] = stamp->irq5[0]; stamp->irq5[0] = ticks; stamp->frames++;
}

uint64_t ar_frame_stamp_value(const struct ar_frame_stamp *stamp)
{
	return stamp ? (stamp->irq5[0] / 40) & AR_FRAME_STAMP_MASK : 0;
}

int ar_frame_stamp_receive(struct ar_frame_stamp *stamp, uint64_t stamp_us)
{
	if (!stamp || !stamp->irq5[1]) { return -1; }
	const uint64_t ref = stamp->irq5[1];
	stamp_us &= AR_FRAME_STAMP_MASK;
	if (!stamp->valid) {
		/* The AP's clock beyond 32 bits is not sent: taken as zero (both ends
		 * read it modulo 2^32). */
		stamp->offset = (int64_t)(stamp_us * 40) - (int64_t)ref;
		stamp->valid = 1; stamp->misses = 0; stamp->residual_us = 0; stamp->taken++;
		return 0;
	}
	/* Its 32 bits nearest the AP's clock as the offset has it (they wrap every
	 * 71.6 min; the offset keeps counting past). */
	const int64_t predicted = ((int64_t)ref + stamp->offset) / 40;
	int64_t delta = (int64_t)((stamp_us - (uint64_t)predicted) & AR_FRAME_STAMP_MASK);
	if (delta >= (INT64_C(1) << 31)) { delta -= INT64_C(1) << 32; }
	const int64_t offset = (predicted + delta) * 40 - (int64_t)ref;
	const int64_t residual = offset - stamp->offset;
	stamp->residual_us = (int32_t)(residual / 40);
	if (residual > REFUSE_TICKS || residual < -REFUSE_TICKS) {
		stamp->rejected++;
		if (++stamp->misses < 3) { return -1; }
		stamp->offset = offset; stamp->misses = 0; stamp->taken++;
		return 0;
	}
	stamp->misses = 0;
	stamp->offset += residual / 4;   /* the IRQs' jitter, and the crystals' drift between stamps */
	stamp->taken++;
	return 0;
}

int ar_frame_stamp_ap_us(const struct ar_frame_stamp *stamp, unsigned ap_role, uint64_t *us)
{
	if (!stamp || !us || !stamp->irq5[0]) { return -1; }
	if (ap_role) { *us = (stamp->irq5[0] / 40) & AR_FRAME_STAMP_MASK; return 0; }
	if (!stamp->valid) { return -1; }
	*us = (uint64_t)(((int64_t)stamp->irq5[0] + stamp->offset) / 40) & AR_FRAME_STAMP_MASK;
	return 0;
}
