/* SPDX-License-Identifier: MIT */
#include "ar_ap_time.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
	struct ar_ap_time time = {0};
	assert(ar_ap_time_get(&time, 1234) == UINT32_MAX);
	/* Time is 0xa1234567, independent of payload high nibble and TTL bit4. */
	uint8_t control[] = {0, 0xa7, 9, 0x67, 0x45, 0x23, 0xf1};
	ar_ap_time_receive(&time, control, 0xfffffff0);
	assert(ar_ap_time_get(&time, 0xfffffff0) == 0xa1234567);
	assert(ar_ap_time_get(&time, 0x10) == 0xa1234587);
	control[2] = 8; control[6] = 1;
	ar_ap_time_receive(&time, control, 1234);
	assert(ar_ap_time_get(&time, 1234) == 0xa1234567);
	/* Received zero is valid; a remote wrap is ordinary modulo arithmetic. */
	control[1] = 7; control[3] = control[4] = control[5] = control[6] = 0;
	ar_ap_time_receive(&time, control, 2000);
	assert(time.valid && ar_ap_time_get(&time, 2001) == 1);
	control[1] = 0xf7; control[3] = control[4] = control[5] = 255; control[6] = 15;
	ar_ap_time_receive(&time, control, 3000);
	assert(ar_ap_time_get(&time, 3001) == 0);
	ar_ap_time_reset(&time);
	assert(!time.valid && !time.offset_ms && ar_ap_time_get(&time, 3002) == UINT32_MAX);
	puts("AP clock header bits, local/remote wrap, unavailable state and reset passed");
}
