/* SPDX-License-Identifier: MIT */
#include "ar_flash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h> /* Independent host CRC oracle; never linked into firmware. */

static uint8_t primary[4096], backup[4096], scratch[4096];
static const uint8_t factory[8] = {4,0,0,0,1,2,3,4};
static unsigned calls, fail_primary, fail_backup, fail_record;
static uint32_t record_address;
static void put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) { p[i]=v>>(i*8); } }
static void put64(uint8_t *p, uint64_t v) { put32(p,v); put32(p+4,v>>32); }
static void checksum(uint8_t *p, unsigned count)
{
	put32(p+592,count); put32(p+600,(uint32_t)crc32(0,p+1024,count*128));
	put32(p+528,0); put32(p+528,(uint32_t)crc32(0,p+512,92));
}
static void entry(uint8_t *p, unsigned index, const char *name, uint64_t first, uint64_t last)
{
	uint8_t *e=p+1024+index*128; memset(e,0,128); e[0]=1;
	put64(e+32,first); put64(e+40,last);
	for (unsigned i=0;i<36 && name[i];i++) { e[56+i*2]=name[i]; }
}
static void table(uint8_t *p, const char *name)
{
	memset(p,0,4096); p[510]=0x55; p[511]=0xaa; memcpy(p+512,"EFI PART",8);
	put32(p+520,0x10000); put32(p+524,92); put32(p+596,128);
	entry(p,0,"usersetting",8,15); entry(p,1,name,16,23); checksum(p,3);
}
static int read_flash(void *context, uint32_t offset, uint8_t *out, size_t length)
{
	assert(context==&calls); calls++;
	if (offset==0x8000 || offset==0x9000) {
		assert(length==4096);
		if (offset==0x8000 ? fail_primary : fail_backup) { memset(out,0x33,17); return -110; }
		memcpy(out,offset==0x8000?primary:backup,4096); return 0;
	}
	assert(offset==record_address && length==8);
	if (fail_record) { memset(out,0x44,3); return -110; }
	memcpy(out,factory,8); return 0;
}
static void rejected(uint8_t *p, uint32_t capacity, int expected)
{
	struct ar_flash_partition part={0x12345678,0xabcdef12}, saved=part;
	assert(ar_flash_find_partition(p,4096,0x8000,capacity,"factory",&part)==expected);
	assert(!memcmp(&part,&saved,sizeof part));
}
int main(void)
{
	assert(crc32(0,(const uint8_t *)"123456789",9)==0xcbf43926UL);
	table(primary,"factory"); memcpy(scratch,primary,4096);
	struct ar_flash_partition part;
	assert(!ar_flash_find_partition(primary,4096,0x8000,0x1000000,"factory",&part));
	assert(part.offset==0xa000 && part.length==4096 && !memcmp(primary,scratch,4096));
	assert(!ar_flash_find_partition(primary,4096,0x9000,0x1000000,"factory",&part) && part.offset==0xb000);
	assert(!ar_flash_find_partition(primary,4096,0x8000,0xb000,"factory",&part)); /* Exact end. */
	rejected(primary,0xafff,-74);
	primary[528]^=1; rejected(primary,0x1000000,-74); primary[528]^=1;
	primary[1024]^=1; rejected(primary,0x1000000,-74); primary[1024]^=1;
	primary[510]=0; rejected(primary,0x1000000,-74); primary[510]=0x55;
	put32(primary+596,256); checksum(primary,3); rejected(primary,0x1000000,-74);
	put32(primary+596,128); put32(primary+592,UINT32_MAX); rejected(primary,0x1000000,-74);
	table(primary,"factory"); entry(primary,1,"factory",UINT64_C(1)<<32,(UINT64_C(1)<<32)+1);
	checksum(primary,3); rejected(primary,0x1000000,-74);
	table(primary,"factory"); entry(primary,1,"factory",16,UINT64_MAX); checksum(primary,3); rejected(primary,0x1000000,-74);
	table(primary,"factory"); entry(primary,2,"factory",24,31); checksum(primary,3); rejected(primary,0x1000000,-74);
	table(primary,"factoryX"); rejected(primary,0x1000000,-2);
	table(primary,"factory"); primary[1024+128+57]=1; checksum(primary,3); rejected(primary,0x1000000,-2);
	table(primary,"factory"); entry(primary,0,"single",8,8); checksum(primary,3); rejected(primary,0x1000000,-2);
	table(primary,"factory"); checksum(primary,0); rejected(primary,0x1000000,-2);
	assert(ar_flash_find_partition(NULL,4096,0x8000,0x1000000,"factory",&part)==-22);
	assert(ar_flash_find_partition(primary,4095,0x8000,0x1000000,"factory",&part)==-22);
	assert(ar_flash_find_partition(primary,4096,0x8000,0x8fff,"factory",&part)==-22);
	assert(ar_flash_find_partition(primary,4096,0x7000,0x1000000,"factory",&part)==-22);
	assert(ar_flash_find_partition(primary,4096,0x8000,0x1000000,"",&part)==-22);
	assert(ar_flash_find_partition(primary,4096,0x8000,0x1000000,"1234567890123456789012345678901234567",&part)==-22);
	table(primary,"factory"); table(backup,"factory"); record_address=0xa000;
	uint8_t record[8]={0}; struct ar_flash_factory_info info;
	assert(!ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4096,record,&info));
	assert(calls==2 && info.table_attempts==1 && info.table_base==0x8000 && info.partition_offset==0xa000 && info.partition_length==4096);
	assert(!memcmp(record,factory,8));
	primary[528]^=1; calls=0; record_address=0xb000;
	assert(!ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4096,record,&info));
	assert(calls==3 && info.table_attempts==2 && info.table_base==0x9000 && info.table_errors[0]==-74);
	fail_primary=1; calls=0;
	assert(!ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4096,record,&info));
	assert(calls==3 && info.table_errors[0]==-110);
	fail_primary=0; table(primary,"absent"); calls=0;
	assert(ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4096,record,&info)==-2);
	assert(calls==1 && info.table_base==0x8000); /* A valid primary is authoritative. */
	table(primary,"factory"); record_address=0xa000; fail_record=1; calls=0;
	memset(record,0xa5,8); uint8_t saved[8]; memcpy(saved,record,8);
	assert(ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4096,record,&info)==-110);
	assert(calls==2 && info.record_error==-110 && !memcmp(record,saved,8));
	fail_primary=fail_backup=1; calls=0;
	assert(ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4096,record,&info)==-110);
	assert(calls==2 && info.table_attempts==2 && !info.table_base && !memcmp(record,saved,8));
	calls=0;
	assert(ar_flash_read_factory(read_flash,&calls,0x1000000,scratch,4095,record,&info)==-22 && !calls);
	puts("flash partition CRCs, relative LBAs, bounds, backup selection and atomic reads passed");
}
