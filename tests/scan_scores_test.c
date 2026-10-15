/* SPDX-License-Identifier: MIT */
#include "ar_scan_scores.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
 const struct { uint32_t power; int32_t score; } values[] = {
  {0,-156},{1,-153},{9,-146},{98,-137},{99,-137},{100,-136},
  {999,-127},{1000,-126},{1999,-121},{UINT32_MAX,-58},
  {3054198,-90},{9162596,-85}
 };
 for (unsigned i=0;i<sizeof values/sizeof values[0];i++) { assert(ar_scan_power_score(values[i].power)==values[i].score); }
 struct ar_scan_scores s={.band_mask=4,.initial_band=2,.startup=1,.selected=255,.group_count=1};
 struct ar_scan_history h[3]={0};
 uint32_t rows[3]={0x11400000,0x11410000,0x11420000}; uint8_t groups[3]={0,0,0};
 /* Global ring advances at the last enabled channel, not every event. */
 for (unsigned round=0;round<10;round++) {
  for (unsigned channel=0;channel<3;channel++) {
   int action=ar_scan_score_feed(&s,h,rows,groups,3,channel,0);
   assert(h[channel].score==-156 && h[channel].latest==0);
   if (channel!=2) { assert(!action && s.ring==round); }
   else if (round!=9) { assert(action==AR_SCAN_SCORE_ROUND && s.ring==round+1 && !s.ready); }
   else { assert(action==(AR_SCAN_SCORE_ROUND|AR_SCAN_SCORE_READY|AR_SCAN_SCORE_SELECTED)); }
  }
 }
 assert(s.ready && !s.ring && !s.startup && s.selected==0 && s.best_group[0]==0);
 assert(s.best_band[0]==255 && s.best_band[1]==255 && s.best_band[2]==0 && s.band_score[2]==-156);
 /* Disabled trailing rows don't delay round completion or enter ranking. */
 rows[2]&=~0x01000000u; h[2].score=INT32_MIN;
 assert(!ar_scan_score_feed(&s,h,rows,groups,3,0,100));
 assert(ar_scan_score_feed(&s,h,rows,groups,3,1,100)==(AR_SCAN_SCORE_ROUND|AR_SCAN_SCORE_READY));
 assert(s.ring==1 && s.best_band[2]==0 && s.best_group[0]==0);
 assert(h[0].raw[0]==100 && h[0].means[0]==10 && h[0].score==-153);
 /* Both sums must be 64-bit: ten maximum uint32 samples cannot wrap. */
 s=(struct ar_scan_scores){.band_mask=4,.initial_band=2}; memset(h,0,sizeof h);
 for (unsigned i=0;i<10;i++) { h[0].raw[i]=h[0].means[i]=UINT32_MAX; }
 assert(ar_scan_score_feed(&s,h,rows,NULL,1,0,UINT32_MAX)==AR_SCAN_SCORE_ROUND);
 assert(h[0].latest==UINT32_MAX && h[0].means[0]==UINT32_MAX && h[0].score==-58);
 /* Signed ranking, stable ties and original signed32 wrap in band sums. */
 rows[0]=rows[1]=0x01000000; rows[2]=0x11000000;
 s=(struct ar_scan_scores){.ready=1,.band_mask=5,.initial_band=0,.startup=1,.group_count=4};
 memset(h,0,sizeof h);h[0].score=h[1].score=INT32_MAX;
 assert(ar_scan_score_feed(&s,h,rows,groups,3,2,0)==7);
 assert(s.band_score[0]==-1 && s.best_band[0]==0 && s.selected==0);
 assert(s.best_group[3]==2 && s.best_group[1]==255 && s.best_group[2]==255);
 /* An invalid startup winner is not posted or dereferenced. */
 s=(struct ar_scan_scores){.ready=1,.band_mask=0,.startup=1,.initial_band=2};
 assert(ar_scan_score_feed(&s,h,rows,NULL,3,2,0)==11);
 assert(s.startup && s.best_band[2]==255);
 /* Reject malformed state before writing any history or selection state. */
 struct ar_scan_scores prior=s;struct ar_scan_history saved[3];memcpy(saved,h,sizeof h);
 assert(ar_scan_score_feed(&s,h,rows,NULL,0,0,0)==-22);
 assert(ar_scan_score_feed(&s,h,rows,NULL,256,0,0)==-22);
 assert(ar_scan_score_feed(&s,h,rows,NULL,3,3,0)==-22);
 assert(ar_scan_score_feed(NULL,h,rows,NULL,3,0,0)==-22);
 assert(!memcmp(h,saved,sizeof h) && !memcmp(&s,&prior,sizeof s));
 s.ring=10;prior=s;assert(ar_scan_score_feed(&s,h,rows,NULL,3,0,0)==-22);
 assert(!memcmp(h,saved,sizeof h) && !memcmp(&s,&prior,sizeof s));
 puts("scan averaging, score conversion, ranking and selection passed");
}
