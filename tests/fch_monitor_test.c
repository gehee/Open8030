/* SPDX-License-Identifier: MIT */
#include "ar_fch_monitor.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
	struct ar_fch_monitor m;
	const uint32_t frames[] = {500,1000,2000,3000,4000,10000,125000,255000};
	const unsigned thresholds[] = {248,232,244,77,248,100,8,8};
	for (unsigned i=0;i<sizeof frames/sizeof frames[0];i++) {
		assert(!ar_fch_monitor_init(&m,frames[i],0));
		assert(m.threshold==thresholds[i]);
	}
	assert(ar_fch_monitor_init(&m,0,0)==-95);
	assert(ar_fch_monitor_init(&m,255500,0)==-95);
	assert(ar_fch_monitor_init(&m,UINT32_MAX,0)==-95);
	assert(ar_fch_monitor_init(NULL,10000,0)==-22);
	assert(!ar_fch_monitor_init(&m,10000,0));
	ar_fch_monitor_force(&m); assert(!m.forced);
	assert(!ar_fch_monitor_observe(&m,1));
	assert(!ar_fch_monitor_poll(&m,10000) && !m.missed);
	ar_fch_monitor_enable(&m,0);
	assert(!ar_fch_monitor_observe(&m,1) && !m.locked);
	assert(ar_fch_monitor_observe(&m,0)==AR_FCH_RESET_RX && !m.streak);
	assert(!ar_fch_monitor_observe(&m,1) && !m.locked);
	assert(!ar_fch_monitor_observe(&m,1) && m.locked);
	assert(!ar_fch_monitor_poll(&m,10009));
	assert(!ar_fch_monitor_poll(&m,10010) && m.missed==1);
	ar_fch_monitor_enable(&m,0); assert(m.missed==1 && m.locked);
	unsigned resets=0, losses=0;
	for(unsigned i=2;i<=100;i++) {
		unsigned a=ar_fch_monitor_poll(&m,10000+10*i);
		resets+=!!(a&AR_FCH_RESET_RX); losses+=!!(a&AR_FCH_LOST);
		assert((a&AR_FCH_LOST)==(i==100 ? AR_FCH_LOST : 0));
	}
	assert(resets==5 && losses==1 && !m.missed && !m.locked);
	assert(!ar_fch_monitor_init(&m,10000,0)); ar_fch_monitor_enable(&m,1);
	for(unsigned i=1;i<100;i++) { assert(!(ar_fch_monitor_poll(&m,i*10)&AR_FCH_LOST)); }
	assert(!ar_fch_monitor_observe(&m,1)); /* Refresh at the expiry boundary. */
	assert(!(ar_fch_monitor_poll(&m,1000)&AR_FCH_LOST) && m.missed==1);
	assert(!ar_fch_monitor_poll(&m,UINT64_C(1)<<40) && m.missed==2);
	assert(!ar_fch_monitor_poll(&m,(UINT64_C(1)<<40)+9) && m.missed==2);
	ar_fch_monitor_force(&m);assert(m.forced);
	assert(ar_fch_monitor_poll(&m,(UINT64_C(1)<<40)+10)==AR_FCH_LOST);
	assert(!m.forced && !m.locked);
	ar_fch_monitor_force(&m);assert(!m.forced);
	/* The scheduler clock is 64-bit, independent of diagnostic 32-bit wrap. */
	assert(!ar_fch_monitor_init(&m,10000,UINT32_MAX-4)); ar_fch_monitor_enable(&m,1);
	assert(!ar_fch_monitor_poll(&m,(uint64_t)UINT32_MAX+6) && m.missed==1);
	puts("FCH loss monitor contracts passed");
	return 0;
}
