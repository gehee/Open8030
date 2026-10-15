/* SPDX-License-Identifier: MIT */
#include "ar_catalog.h"
#include "ar_scan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct calls {
 unsigned count;
 enum ar_catalog_action actions[8];
 uint32_t values[8];
 struct ar_catalog_selection *selection;
 unsigned mutate;
};
static void effect(enum ar_catalog_action action, uint32_t value, void *context)
{
 struct calls *c = context;
 assert(c->count < 8); c->actions[c->count] = action; c->values[c->count++] = value;
 if (c->mutate && (action == AR_CATALOG_MODE || action == AR_CATALOG_CHANNEL)) {
  c->selection->channel = 1;
 }
 if (action == AR_CATALOG_AP_TUNE) { assert(c->selection->ap_channel == value); }
}
static uint32_t word(const uint8_t *p)
{
 return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
int main(void)
{
 uint32_t frequencies[] = {0x8057ba60,5839000};
 int32_t scores[] = {-150,INT32_MIN};
 uint8_t out[1028], saved[1028];
 memset(out,0xa5,sizeof out);
 assert(!ar_catalog_information(frequencies,scores,2,2,255,24,out));
 assert(!memcmp(out,(uint8_t[]){2,2,255,24},4));
 assert(word(out+4)==frequencies[0] && word(out+8)==frequencies[1]);
 assert(word(out+516)==(uint32_t)-150 && word(out+520)==(uint32_t)INT32_MIN);
 for (unsigned i=12;i<516;i++) { assert(!out[i]); }
 for (unsigned i=524;i<sizeof out;i++) { assert(!out[i]); }
 memcpy(saved,out,sizeof out);
 assert(ar_catalog_information(NULL,scores,2,0,0,0,out)==-22);
 assert(ar_catalog_information(frequencies,NULL,2,0,0,0,out)==-22);
 assert(ar_catalog_information(frequencies,scores,129,0,0,0,out)==-22);
 assert(ar_catalog_information(frequencies,scores,2,0,0,0,NULL)==-22);
 assert(!memcmp(out,saved,sizeof out));
 assert(!ar_catalog_information(NULL,NULL,0,0,0,0,out));
 for (unsigned i=0;i<sizeof out;i++) { assert(!out[i]); }

 uint32_t rows[128]; uint8_t groups[128];
 for (unsigned i=0;i<128;i++) { rows[i]=0xb157ba60; groups[i]=i%6; }
 assert(!ar_catalog_enabled(rows,128,out)); assert(out[0]==128);
 for (unsigned i=0;i<128;i++) { assert(out[i+1]==i); }
 assert(!ar_catalog_enabled(NULL,128,out));
 for (unsigned i=0;i<129;i++) { assert(!out[i]); }
 assert(ar_catalog_enabled(rows,128,NULL)==-6);
 assert(ar_catalog_enabled(rows,129,out)==-22);

 struct ar_catalog_selection state={.role=1,.channel=24,.group_count=6,.current_group=99};
 for (unsigned i=0;i<16;i++) { state.best_group[i]=0x100+i; }
 struct calls calls={.selection=&state};
 uint8_t list[]={15,23,24,25,24};
 assert(!ar_catalog_set_work(rows,groups,128,4,list,sizeof list,&state,effect,&calls));
 assert(calls.count==1 && calls.actions[0]==AR_CATALOG_RESET_SCAN);
 assert(state.current_group==0);
 assert(state.best_group[2]==255 && state.best_group[4]==255);
 assert(state.best_group[0]==0x100 && state.best_group[1]==0x101 && state.best_group[3]==0x103 && state.best_group[5]==0x105);
 assert(!ar_catalog_enabled(rows,128,out) && !memcmp(out,(uint8_t[]){4,15,23,24,25},5));
 for (unsigned i=0;i<128;i++) { assert((rows[i]&~0x01000000u)==0xb057ba60); }
 uint16_t cursor=24;
 const uint16_t sweep[]={25,15,23,24,25};
 for (unsigned i=0;i<sizeof sweep/sizeof sweep[0];i++) {
  assert(!ar_scan_advance(rows,128,&cursor,0,0) && cursor==sweep[i]);
 }

 /* The first invalid entry remains the fallback request; the valid second
  * entry enables its channel. The policy callback owns fallback validation. */
 uint8_t invalid_first[]={255,1}; calls.count=0; calls.mutate=1; state.role=0; state.ap_state=0;
 assert(!ar_catalog_set_work(rows,groups,128,4,invalid_first,2,&state,effect,&calls));
 assert(calls.count==5 && calls.actions[0]==AR_CATALOG_CHANNEL && calls.values[0]==255);
 assert(calls.actions[1]==AR_CATALOG_AP_LOCK && calls.actions[2]==AR_CATALOG_AP_TUNE && calls.values[2]==255);
 assert(calls.actions[3]==AR_CATALOG_AP_UNLOCK && calls.actions[4]==AR_CATALOG_RESET_SCAN);
 assert(state.ap_channel==255 && state.channel==1 && state.current_group==1);
 assert(state.best_group[3]==0x103); /* selector3 matches any enabled group */
 assert(!ar_catalog_enabled(rows,128,out) && out[0]==1 && out[1]==1);

 /* Automatic policy takes precedence over current-channel presence. An active
  * AP still locks/unlocks, but its broadcast channel is not overwritten. */
 calls.count=0; calls.mutate=0; state.policy_flags=6; state.ap_state=2; state.ap_channel=77;
 assert(!ar_catalog_set_work(rows,groups,128,4,invalid_first+1,1,&state,effect,&calls));
 assert(calls.count==4 && calls.actions[0]==AR_CATALOG_MODE && calls.values[0]==2);
 assert(calls.actions[1]==AR_CATALOG_AP_LOCK && calls.actions[2]==AR_CATALOG_AP_UNLOCK);
 assert(calls.actions[3]==AR_CATALOG_RESET_SCAN && state.ap_channel==77);

 /* Full 16-bit current-channel comparison: a subchannel's low byte alone
  * does not suppress the fallback. Group lookup uses the low byte afterward. */
 state.role=1; state.channel=0x101; state.policy_flags=0; calls.count=0;
 assert(!ar_catalog_set_work(rows,groups,128,4,invalid_first+1,1,&state,effect,&calls));
 assert(calls.count==2 && calls.actions[0]==AR_CATALOG_CHANNEL && calls.values[0]==1);
 assert(state.current_group==1);

 /* Forbidden selections clear all enables and all tracked group winners. */
 calls.count=0;
 assert(!ar_catalog_set_work(rows,groups,128,0,list,sizeof list,&state,effect,&calls));
 assert(!ar_catalog_enabled(rows,128,out) && out[0]==0);
 for (unsigned i=0;i<6;i++) { assert(state.best_group[i]==255); }
 for (unsigned i=6;i<16;i++) { assert(state.best_group[i]==0x100+i); }

 uint32_t before[128]; memcpy(before,rows,sizeof rows); struct ar_catalog_selection prior=state;
 calls.count=0;
 assert(ar_catalog_set_work(rows,groups,128,4,NULL,1,&state,effect,&calls)==-6);
 assert(ar_catalog_set_work(rows,groups,128,4,list,0,&state,effect,&calls)==-6);
 assert(ar_catalog_set_work(rows,groups,128,4,list,129,&state,effect,&calls)==-6);
 assert(ar_catalog_set_work(rows,groups,129,4,list,1,&state,effect,&calls)==-22);
 assert(ar_catalog_set_work(rows,groups,0,4,list,1,&state,effect,&calls)==-22);
 assert(ar_catalog_set_work(rows,NULL,128,4,list,1,&state,effect,&calls)==-22);
 assert(ar_catalog_set_work(rows,groups,128,4,list,1,&state,NULL,&calls)==-22);
 assert(!calls.count && !memcmp(before,rows,sizeof rows) && !memcmp(&state,&prior,sizeof state));
 puts("catalog serialization, work-list effects and validation passed");
}
