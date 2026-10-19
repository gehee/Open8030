/* SPDX-License-Identifier: MIT */
#include "ar_frame_stamp.h"
#include <assert.h>
#include <stdio.h>

/* An AP and a ground on one frame: the ground's timer runs ahead by `skew`
 * ticks and drifts ppm against the AP's; each AP stamp reaches the ground in
 * the header after the AP's next IRQ 5. */
static void run(int64_t skew, double ppm, uint64_t start_us, unsigned frames, uint32_t frame_ticks)
{
	struct ar_frame_stamp ap = {0}, gnd = {0};
	uint64_t ap_t = start_us * 40, pending = 0;
	unsigned have = 0;
	for (unsigned f = 0; f < frames; f++) {
		ap_t += frame_ticks;
		const uint64_t gnd_t = (uint64_t)((int64_t)ap_t + skew + (int64_t)((double)ap_t * ppm * 1e-6));
		ar_frame_stamp_irq5(&ap, ap_t);
		ar_frame_stamp_irq5(&gnd, gnd_t);
		/* The header carrying the stamp written after the previous IRQ 5. */
		if (have && f % 20 == 0) { assert(ar_frame_stamp_receive(&gnd, pending) == 0); }
		pending = ar_frame_stamp_value(&ap); have = 1;
		uint64_t a, g;
		if (gnd.valid) {
			assert(ar_frame_stamp_ap_us(&ap, 1, &a) == 0 && ar_frame_stamp_ap_us(&gnd, 0, &g) == 0);
			int64_t d = (int64_t)((g - a) & AR_FRAME_STAMP_MASK);
			if (d >= (INT64_C(1) << 31)) { d -= INT64_C(1) << 32; }
			assert(d >= -5 && d <= 5);   /* same moment, to the us (drift between stamps) */
		}
	}
	assert(gnd.taken >= frames / 20 - 1 && !gnd.rejected);
}

int main(void)
{
	run(123456789, 0, 1000000, 2000, 5080);         /* 3250.56 us frames */
	run(-987654321, 20, 5000000, 4000, 8803);       /* 5633.92 us, 20 ppm */
	run(4242, -15, (UINT64_C(1) << 32) - 2000000, 2000, 7267);   /* across the 32-bit wrap */
	/* A stamp paired with the wrong IRQ 5 (a frame off): refused, and three
	 * in a row taken as a new clock. */
	struct ar_frame_stamp g = {0};
	const uint64_t T0 = 400000, F = 130022;   /* a 3250.56 us frame, in ticks */
	ar_frame_stamp_irq5(&g, T0); ar_frame_stamp_irq5(&g, T0 + F);
	assert(ar_frame_stamp_receive(&g, 10000) == 0 && g.valid);   /* the AP's 10000 us at our T0 */
	ar_frame_stamp_irq5(&g, T0 + 2 * F);
	const uint64_t ahead = 10000 + 2 * F / 40;   /* paired a frame off */
	assert(ar_frame_stamp_receive(&g, ahead) == -1 && g.rejected == 1);
	assert(ar_frame_stamp_receive(&g, ahead) == -1);
	assert(ar_frame_stamp_receive(&g, ahead) == 0 && g.misses == 0);
	uint64_t us; assert(ar_frame_stamp_ap_us(&g, 0, &us) == 0 && us == (uint64_t)(((ahead * 40) + F) / 40));
	ar_frame_stamp_reset(&g); assert(!g.valid && ar_frame_stamp_ap_us(&g, 0, &us) == -1);
	puts("frame stamps: the AP's clock on the ground to the us, across drift and the 32-bit wrap; a frame off refused");
	return 0;
}
