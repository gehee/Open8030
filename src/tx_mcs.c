/* SPDX-License-Identifier: MIT */
#include "ar_tx_mcs.h"
#include <string.h>

static int code_length(unsigned bw,unsigned interleave,unsigned modulation,
	unsigned rate,unsigned special)
{
	static const uint8_t bits[]={1,2,4,6,8};
	if (bw>7 || interleave>7 || modulation>4 || rate>2 || special>1) { return -95; }
	uint32_t value=(bits[modulation] << (bw+2-rate+interleave+special)) >> 2;
	return value==2 ? 0 : value==4 ? 1 : value && !(value&7) ? 2 : -95;
}
static int program(struct ar_interleaver *state,unsigned user,unsigned mcs,
	const uint32_t keys[25],unsigned chip,const struct ar_tx_mcs_ops *ops,void *context)
{
	uint32_t base=(user+0x2c0u)<<18,key=keys[mcs];
	unsigned modulation=(key>>16)&255,code=(key>>8)&255,rate=key&255;
	if (modulation>4 || code>2 || rate>2) { return -95; }
	unsigned bw=ops->read(base|3,context),interleave=ops->read(base|0x1203,context);
	unsigned mode=user==10 ? (ops->word(0xb60,context)>>27)&3 : ops->read(base|0x1602,context);
	if ((key>>24) ? mode!=2 : mode==2) {
		if (user<=10) { ops->field(user==10 ? 0xb601b02 : base|0x1602,(key>>24) ? 2 : 1,context); }
	}
	unsigned special=ops->read(base|0x1602,context)==2;
	int length=code_length(bw,interleave,modulation,rate,special);
	if (length<0) { return length; }
	ops->field(base|0x803,modulation,context);
	ops->field(base|0xc04,code+3*(unsigned)length,context);
	ops->field(base|0x1002,rate,context);
	if (chip==1) {
		unsigned normalized=bw==5 ? 4 : bw;
		int n=code_length(normalized,interleave,modulation,rate,special);
		if (n<0) { return n; }
		static const uint8_t bits[]={1,2,4,6,8},divisor[]={4,4,8};
		uint32_t data=(bits[modulation]*288u) << ((normalized+interleave+special-rate)&31);
		uint32_t blocks=(data/(576u<<n)+divisor[code]-1)/divisor[code];
		if (blocks==24) { ops->field(base|0xc04,code+3,context); }
	}
	state->users[user].mcs=mcs; return 0;
}
int ar_tx_mcs_control(struct ar_interleaver *state,enum ar_tx_mcs_action action,
	unsigned user,unsigned mcs,const uint32_t keys[25],unsigned chip,
	unsigned dev_role,const struct ar_tx_mcs_ops *ops,void *context)
{
	if (!state || !keys || !ops || !ops->read || !ops->word || !ops->field || !ops->event ||
	    user>=12 || mcs>=25 || action>AR_TX_MCS_RETRY_RESET || (int)action<0 || dev_role>1) { return -22; }
	struct ar_interleaver_user *u=&state->users[user];
	if (action==AR_TX_MCS_RETRY_RESET) {
		uint8_t retries=u->retries;u->active=0;
		uint8_t payload[9]={(uint8_t)(dev_role ? 0 : user),0,u->mcs,u->previous_mcs,
			(uint8_t)u->history,(uint8_t)(u->history>>8),(uint8_t)(u->history>>16),
			(uint8_t)(u->history>>24),(uint8_t)(retries-1)};
		ops->event(9,payload,context);u->retries=0;u->history=0;return 0;
	}
	if (action==AR_TX_MCS_INITIALIZE) {
		memset(u,0,sizeof *u);u->previous_mcs=25;
		uint32_t base=(user+0x2c0u)<<18;
		unsigned modulation=ops->read(base|0x803,context),code=ops->read(base|0xc04,context);
		unsigned rate=ops->read(base|0x1002,context),mode=ops->read(base|0x1602,context);
		uint32_t key=((modulation&255)<<16)|((code%3)<<8)|(rate&255)|((mode==2)<<24);
		unsigned found=0;while (found<25 && keys[found]!=key) { found++; }u->mcs=found;
		ops->field(0xb4c0305,dev_role ? 1 : 17,context);return 0;
	}
	if (action==AR_TX_MCS_REQUEST) {
		if (u->mcs==mcs || u->active) { return 0; }
		unsigned previous=u->mcs;u->mcs=mcs;u->previous_mcs=previous;
		if (previous>=mcs) { u->drain=6;u->active=1;return 0; }
	}
	int result=program(state,user,mcs,keys,chip,ops,context);
	if (!result && action==AR_TX_MCS_DRAIN_APPLY) { u->active=0; }
	return result;
}
