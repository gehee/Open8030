/* SPDX-License-Identifier: MIT */
#include "ar_power_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct capture { unsigned locked, locks, unlocks, count; struct ar_power_write writes[64]; };
static void lock(void *context) { struct capture *c=context; assert(!c->locked); c->locked=1; c->locks++; }
static void unlock(void *context) { struct capture *c=context; assert(c->locked); c->locked=0; c->unlocks++; }
static void field(uint32_t address,uint32_t value,void *context)
{
	struct capture *c=context; assert(c->count<64);
	c->writes[c->count++]=(struct ar_power_write){address,value};
}
int main(void)
{
	struct ar_power_control power={.config={.valid=0x7fffffff,.closed={1,1},.high_clock=1,.dev_role=1,.csma_offset=8}};
	for (unsigned i=0;i<31;i++) {
		power.config.calibration[i][0]=40-i; power.config.calibration[i][1]=11+7*i;
		power.config.calibration[i][2]=42-i; power.config.calibration[i][3]=13+7*i;
	}
	struct capture c={0};
	struct ar_power_host_ops ops={lock,unlock,field};
	assert(!ar_power_control_initialize(&power,25,field,&c));
	assert(c.count==48);
	for (unsigned i=0;i<12;i++) { assert(power.level[i]==25); }
	assert(!c.writes[33].value && !c.writes[35].value); /* DEV broadcast amplitudes. */
	c.count=0;
	uint8_t payload[2]={0,14},out[2]={0xa5,0xa5};size_t size=99;
	assert(!ar_power_host_request(&power,1,0x02000008,payload,2,out,&size,&ops,&c));
	assert(!size && c.count==4 && c.locks==1 && c.unlocks==1 && !c.locked);
	assert(power.level[0]==14 && out[0]==0xa5 && out[1]==0xa5);
	payload[0]=5;payload[1]=25;
	assert(!ar_power_host_request(&power,1,0x02000008,payload,2,out,&size,&ops,&c));
	assert(c.count==8 && power.level[0]==25 && power.level[5]==25);
	/* A second user changes the report cache; it does not rewrite user0 fields. */
	assert(c.writes[0].field==0x9a40806 && c.writes[4].field==0x9cc0806);
	payload[0]=0;
	assert(!ar_power_host_request(&power,1,0x01000008,payload,1,out,&size,NULL,NULL));
	assert(size==2 && out[0]==0 && out[1]==25);
	payload[0]=255;
	assert(!ar_power_host_request(&power,1,0x01000008,payload,1,out,&size,NULL,NULL));
	assert(size==2 && out[0]==255 && out[1]==255);
	assert(!ar_power_host_request(&power,257,0x01000007,NULL,0,out,&size,NULL,NULL));
	assert(size==1 && out[0]==1);
	struct ar_power_control saved=power; unsigned writes=c.count;
	payload[0]=0;payload[1]=8;
	assert(ar_power_host_request(&power,1,0x02000008,payload,2,out,&size,&ops,&c)==-3);
	assert(!size && c.count==writes && c.locks==3 && c.unlocks==3);
	for (unsigned n=0;n<4;n++) {
		if (n!=2) { assert(ar_power_host_request(&power,1,0x02000008,payload,n,out,&size,&ops,&c)==-22); }
		if (n!=1) { assert(ar_power_host_request(&power,1,0x01000008,payload,n,out,&size,&ops,&c)==-22); }
	}
	payload[0]=12;
	assert(ar_power_host_request(&power,1,0x02000008,payload,2,out,&size,&ops,&c)==-22);
	assert(ar_power_host_request(&power,1,0x02000009,payload,2,out,&size,&ops,&c)==-95);
	assert(!memcmp(&power,&saved,sizeof power) && c.count==writes && c.locks==3);
	c.count=0;
	power.config.user_offset[11]=1;power.config.valid &= ~(1u<<17);
	saved=power;
	assert(ar_power_control_initialize(&power,25,field,&c)==-95 && !c.count);
	assert(!memcmp(&power,&saved,sizeof power));
	assert(ar_power_control_initialize(&power,8,field,&c)==-3 && !c.count);
	puts("Host power framing, cached-level alias, lock/error order and initialization preflight passed");
}
