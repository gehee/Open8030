/* SPDX-License-Identifier: MIT */
#include "ar_interleaver.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct capture {
	struct ar_interleaver state;
	unsigned events, posts, notifications, applications, resets, retries, control;
	unsigned user, mcs, source, mutate, enabled;
	uint8_t payload[8];
};
static void field(uint32_t address,uint32_t value,void *context)
{
	struct capture *c=context; c->events++;
	assert(address==0xb4c0305 || address==0x8000501 || address==0x8081b01);
	if (address==0x8081b01) { assert(value==0); }
}
static uint32_t exchange(uint32_t address,uint32_t value,void *context)
{
	struct capture *c=context; c->events++;
	assert(address==0x8081b01 && value==1); return 0;
}
static uint32_t read_field(uint32_t address,void *context)
{
	struct capture *c=context; c->events++; assert((address&0x3ffff)==0x401); return c->enabled;
}
static void reset(void *context)
{
	struct capture *c=context; c->events++;c->resets++;
	/* Caller must clear drift after the reset callback while preserving its
	 * unrelated mutation; this catches reset ordering and overbroad clearing. */
	c->state.initialized=7; c->state.reserved=0xcafe;
}
static void control(unsigned user,unsigned direction,unsigned value,void *context)
{
	struct capture *c=context; c->events++;
	assert(user==11 && direction==1); c->control=value;
	assert(c->state.correction_pending==(value!=100));
}
static void apply(unsigned user,unsigned mcs,void *context)
{
	struct capture *c=context; c->events++;c->applications++;c->user=user;c->mcs=mcs;
	if (c->mutate) { c->state.users[user].active=0; }
}
static void retry(void *context) { struct capture *c=context;c->events++;c->retries++; }
static void post(const uint8_t payload[8],void *context)
{
	struct capture *c=context;c->events++;c->posts++;memcpy(c->payload,payload,8);
	assert(!payload[6] && !payload[7]);
}
static void notify(unsigned source,const uint8_t payload[8],void *context)
{
	struct capture *c=context;c->events++;c->notifications++;c->source=source;
	assert(c->posts==c->notifications && !memcmp(c->payload,payload,8));
}
static const struct ar_interleaver_ops ops={field,exchange,read_field,reset,control,apply,retry,post,notify};
int main(void)
{
	struct capture c={.enabled=1};
	struct ar_interleaver_input input={.user=0,.second_user=5,.request=0x00a50000};
	struct ar_interleaver_user *u=&c.state.users[5];
	*u=(struct ar_interleaver_user){.mcs=7,.active=1,.limit=255,.drain=6};
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(u->drain==5 && u->retries==1 && !c.applications);
	assert(!memcmp(c.payload,(uint8_t[]){0,0,0,0,10,5,0,0},8));
	for (unsigned i=0;i<5;i++) { assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c)); }
	assert(!u->drain && u->retries==6 && !c.applications);
	c.mutate=1;
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(c.applications==1 && c.user==5 && c.mcs==7 && !u->active && u->retries==6);
	u->active=2;u->retries=255;u->limit=255;
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(u->retries==0 && !c.retries); /* byte increment wraps */
	u->retries=3;u->limit=2;
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c) && c.retries==1);
	c.enabled=0;input.user=15;
	assert(!ar_interleaver_service(&c.state,&input,31,&ops,&c));
	assert(c.source==31 && c.payload[0]==15 && c.payload[5]==255);

	memset(&c,0,sizeof c);input=(struct ar_interleaver_input){.user=8,.second_user=8,.high_clock=1,.distance_enabled=1,.timestamps={0,0x9001,0}};
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(c.state.initial_delta==400 && c.state.previous_delta==400 && !c.control);
	input.timestamps[1]=0x9101;
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(c.control==98 && c.state.correction_pending==1);
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(c.control==100 && !c.state.correction_pending);
	input.timestamps[1]=0x0080; /* signed low halfword -32768 */
	assert(!ar_interleaver_service(&c.state,&input,18,&ops,&c));
	assert(c.resets==1 && !c.state.initialized && !c.state.initial_delta && !c.state.previous_delta && c.state.reserved==0xcafe);
	struct capture saved=c;input.second_user=12;
	assert(ar_interleaver_service(&c.state,&input,18,&ops,&c)==-22);
	input.second_user=8;
	assert(ar_interleaver_service(&c.state,&input,32,&ops,&c)==-22);
	input.high_clock=2;
	assert(ar_interleaver_service(&c.state,&input,18,&ops,&c)==-22);
	assert(!memcmp(&saved,&c,sizeof c));
	puts("Interleaver drain sequence, callback mutation, signed drift, reserved bytes and invalid-input isolation passed");
}
