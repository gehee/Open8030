/* SPDX-License-Identifier: MIT */
#include "ar_sfdp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t metadata[8192], scratch[AR_SFDP_SCRATCH_BYTES];
static unsigned calls, fail_at;
static void put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) { p[i]=v>>(8*i); } }
static void header(unsigned index, unsigned minor, unsigned major, unsigned words, unsigned offset)
{
	uint8_t *p=metadata+8+index*8;
	p[0]=0; p[1]=minor; p[2]=major; p[3]=words;
	p[4]=offset; p[5]=offset>>8; p[6]=offset>>16; p[7]=0xff;
}
static void reset(void)
{
	memset(metadata,0,sizeof metadata); memcpy(metadata,"SFDP",4); metadata[5]=1; metadata[7]=0xff;
	header(0,0,1,9,4096); put32(metadata+4100,0x03ffffff); calls=fail_at=0;
}
static int read_sfdp(void *context, uint32_t offset, uint8_t *out, size_t length)
{
	assert(context==metadata && offset<=sizeof metadata && length<=sizeof metadata-offset);
	if (++calls==fail_at) { memset(out,0xaa,length/2); return -110; }
	memcpy(out,metadata+offset,length); return 0;
}
static void rejected(int expected)
{
	struct ar_sfdp_info info; memset(&info,0xa5,sizeof info); struct ar_sfdp_info saved=info;
	assert(ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info)==expected);
	assert(!memcmp(&info,&saved,sizeof info));
}
int main(void)
{
	struct ar_sfdp_info info; reset();
	assert(!ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info));
	assert(calls==3 && info.physical_bytes==0x800000 && info.readable_bytes==0x800000 && info.table_offset==4096);
	assert(info.major==1 && !info.minor && info.table_words==9 && !info.address_mode);
	/* Larger devices retain physical density but expose only the 24-bit window. */
	for (unsigned exponent=3;exponent<=63;exponent++) {
		reset(); put32(metadata+4100,0x80000000u|exponent);
		if (exponent<19) { rejected(-74); continue; }
		assert(!ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info));
		uint64_t bytes=UINT64_C(1)<<(exponent-3);
		assert(info.physical_bytes==bytes && info.readable_bytes==(bytes>0x1000000?0x1000000:bytes));
	}
	const uint32_t bad[]={0,7,0x4fff7,0x50000,0x80000002,0x80000040,0xffffffff};
	for (unsigned i=0;i<sizeof bad/sizeof bad[0];i++) { reset(); put32(metadata+4100,bad[i]); rejected(-74); }
	reset(); put32(metadata+4100,0x4ffff); assert(!ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info) && info.readable_bytes==0xa000);
	for (unsigned mode=0;mode<4;mode++) {
		reset(); put32(metadata+4096,mode<<17);
		if (mode>1) { rejected(-95); }
		else { assert(!ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info) && info.address_mode==mode); }
	}
	/* All 256 parameter headers fit; select minor first, then word count.
 * Unsupported major revisions and unrelated parameter IDs do not win. */
	reset(); metadata[6]=255;
	header(1,5,1,9,4096); header(2,5,1,16,4160); put32(metadata+4164,0x07ffffff);
	header(3,9,2,16,4224); header(4,10,1,16,4288); metadata[8+4*8]=1;
	header(255,4,1,64,4352);
	assert(!ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info));
	assert(info.minor==5 && info.table_words==16 && info.table_offset==4160 && info.readable_bytes==0x1000000);
	header(255,6,1,9,4352); put32(metadata+4356,0x00ffffff);
	assert(!ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,&info));
	assert(info.minor==6 && info.table_offset==4352 && info.readable_bytes==0x200000);
	reset(); metadata[0]^=1; rejected(-74);
	reset(); metadata[5]=2; rejected(-74);
	reset(); metadata[8]=1; rejected(-74);
	reset(); metadata[15]=0; rejected(-74);
	reset(); metadata[10]=2; rejected(-74);
	reset(); header(0,1,1,8,4096); rejected(-74);
	reset(); header(0,1,1,9,0xffffe0); rejected(-74); assert(calls==2);
	reset(); metadata[6]=1; header(1,1,1,8,4096); rejected(-74); /* No fallback to stale revision. */
	for (unsigned failure=1;failure<=3;failure++) { reset(); fail_at=failure; rejected(-110); assert(calls==failure); }
	reset();
	assert(ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch-1,&info)==-22 && !calls);
	assert(ar_sfdp_discover(NULL,metadata,scratch,sizeof scratch,&info)==-22 && !calls);
	assert(ar_sfdp_discover(read_sfdp,metadata,NULL,sizeof scratch,&info)==-22 && !calls);
	assert(ar_sfdp_discover(read_sfdp,metadata,scratch,sizeof scratch,NULL)==-22 && !calls);
	puts("SFDP version selection, density, address modes, bounds and atomic failure passed");
}
