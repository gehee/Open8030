/* SPDX-License-Identifier: MIT */
#include "ar_tx_mcs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
	struct ar_interleaver state;
	uint32_t words[0x1000/4],keys[25];
	unsigned writes,events,applies,posts,mutate,event_user,code_count;
	uint8_t event[9];uint32_t codes[8];
};
static uint32_t word(uint32_t offset,void *context)
{
	struct fixture *f=context;assert(!(offset&3) && offset<0x1000);return f->words[offset/4];
}
static uint32_t read_field(uint32_t address,void *context)
{
	return word(address>>16,context)>>(address>>8&31)&((1u<<(address&31))-1);
}
static void field(uint32_t address,uint32_t value,void *context)
{
	struct fixture *f=context;unsigned shift=address>>8&31;
	uint32_t mask=((1u<<(address&31))-1)<<shift;
	f->words[(address>>16)/4]=(word(address>>16,context)&~mask)|((value<<shift)&mask);f->writes++;
	if ((address&65535)==0xc04) { assert(f->code_count<8);f->codes[f->code_count++]=value; }
}
static void event(unsigned id,const uint8_t payload[9],void *context)
{
	struct fixture *f=context;assert(id==9 && !f->state.users[f->event_user].active);
	f->events++;memcpy(f->event,payload,9);
	if (f->mutate) {
		struct ar_interleaver_user *u=&f->state.users[f->event_user];
		u->active=3;u->retries=99;u->history=0xffffffff;u->drain=77;
	}
}
static const struct ar_tx_mcs_ops ops={read_field,word,field,event};
static uint32_t exchange(uint32_t address,uint32_t value,void *context)
{ uint32_t old=read_field(address,context);field(address,value,context);return old; }
static void reset(void *context) { (void)context;assert(0); }
static void control(unsigned u,unsigned d,unsigned v,void *context)
{ (void)u;(void)d;(void)v;(void)context;assert(0); }
static void apply(unsigned user,unsigned mcs,void *context)
{
	struct fixture *f=context;f->applies++;
	assert(!ar_tx_mcs_control(&f->state,AR_TX_MCS_DRAIN_APPLY,user,mcs,f->keys,0,1,&ops,f));
}
static void retry(unsigned user,void *context)
{
	struct fixture *f=context;f->event_user=user;
	assert(!ar_tx_mcs_control(&f->state,AR_TX_MCS_RETRY_RESET,user,0,f->keys,0,1,&ops,f));
}
static void post(const uint8_t payload[8],void *context) { (void)payload;((struct fixture *)context)->posts++; }
static void notify(unsigned source,const uint8_t payload[8],void *context)
{ (void)payload;(void)context;assert(source==18); }
static const struct ar_interleaver_ops service={field,exchange,read_field,reset,control,apply,retry,post,notify};
int main(void)
{
	struct fixture f={0};for(unsigned i=0;i<25;i++) { f.keys[i]=0xffffffff; }
	f.keys[0]=0;f.keys[1]=1;f.keys[2]=0x101;f.keys[3]=0x01030000;
	f.words[0xb14/4]=1|(2u<<18)|(1u<<22);
	memset(&f.state.users[5],0xa5,sizeof f.state.users[5]);
	assert(!ar_tx_mcs_control(&f.state,AR_TX_MCS_INITIALIZE,5,0,f.keys,0,1,&ops,&f));
	assert(f.state.users[5].mcs==0 && f.state.users[5].previous_mcs==25 && !f.state.users[5].active && !f.state.users[5].history);
	assert(!ar_tx_mcs_control(&f.state,AR_TX_MCS_REQUEST,5,2,f.keys,0,1,&ops,&f));
	assert(f.state.users[5].mcs==2 && f.state.users[5].previous_mcs==0 && !f.state.users[5].active);
	unsigned writes=f.writes;uint32_t before=f.words[0xb14/4];
	assert(!ar_tx_mcs_control(&f.state,AR_TX_MCS_REQUEST,5,0,f.keys,0,1,&ops,&f));
	assert(f.writes==writes && f.state.users[5].active==1 && f.state.users[5].drain==6);
	assert(!ar_tx_mcs_control(&f.state,AR_TX_MCS_REQUEST,5,1,f.keys,0,1,&ops,&f));
	assert(f.state.users[5].mcs==0); /* Pending request owns its target. */
	struct ar_interleaver_input input={.user=0,.second_user=5};
	for(unsigned i=0;i<6;i++) { assert(!ar_interleaver_service(&f.state,&input,18,&service,&f)); }
	assert(!f.applies && f.words[0xb14/4]==before && !f.state.users[5].drain);
	assert(!ar_interleaver_service(&f.state,&input,18,&service,&f));
	assert(f.applies==1 && !f.state.users[5].active && f.words[0xb14/4]!=before);
	f.state.users[5].active=2;f.state.users[5].retries=3;f.state.users[5].limit=2;
	f.state.users[5].history=0x12345678;f.mutate=1;
	assert(!ar_interleaver_service(&f.state,&input,18,&service,&f));
	assert(f.events==1 && !memcmp(f.event,(uint8_t[]){0,0,0,2,0x78,0x56,0x34,0x12,2},9));
	assert(!f.state.users[5].history && !f.state.users[5].retries && f.state.users[5].active==3 && f.state.users[5].drain==77);
	f.words[0xb00/4]=4|(2u<<18)|(1u<<22);f.code_count=0;
	assert(!ar_tx_mcs_control(&f.state,AR_TX_MCS_PROGRAM,0,3,f.keys,1,1,&ops,&f));
	assert(f.code_count==2 && f.codes[0]==6 && f.codes[1]==3);
	struct fixture saved=f;
	assert(ar_tx_mcs_control(&f.state,AR_TX_MCS_PROGRAM,12,0,f.keys,0,1,&ops,&f)==-22);
	assert(ar_tx_mcs_control(&f.state,AR_TX_MCS_PROGRAM,0,25,f.keys,0,1,&ops,&f)==-22);
	assert(ar_tx_mcs_control(&f.state,AR_TX_MCS_PROGRAM,0,24,f.keys,0,1,&ops,&f)==-95);
	assert(!memcmp(&saved,&f,sizeof f));
	puts("TX MCS producer/drain application, selected-user retry event, callback mutation and 24-block coding adjustment passed");
}
