/* SPDX-License-Identifier: MIT */
#include "flash_spi_fixture.h"
#include "ar_power_config.h"
#include "ar_factory.h"
#include <stdio.h>
#include <zlib.h>

static uint8_t array[0xc000], scratch[4096];
static void put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) { p[i]=v>>(8*i); } }
static void table(unsigned base, const char *name)
{
	uint8_t *p=array+base; memset(p,0,4096); p[510]=0x55; p[511]=0xaa;
	memcpy(p+512,"EFI PART",8); put32(p+520,0x10000); put32(p+524,92);
	put32(p+592,1); put32(p+596,128);
	put32(p+1024+32,16); put32(p+1024+40,23);
	for (unsigned i=0;name[i];i++) { p[1024+56+2*i]=name[i]; }
	put32(p+600,crc32(0,p+1024,128)); put32(p+528,crc32(0,p+512,92));
}
int main(void)
{
	uint8_t metadata[64]={'S','F','D','P',0,1,0,0xff,0,0,1,9,16,0,0,0xff};
	put32(metadata+20,sizeof array*8-1);
	table(0x8000,"factory"); table(0x9000,"factory");
	memcpy(array+0xa000,(uint8_t[]){4,0,0,0,255,9,2,15},8);
	memcpy(array+0xb000,(uint8_t[]){3,0,0,0,255,9,2,15},8);
	struct device d; reset(&d); d.sfdp=metadata; d.sfdp_length=sizeof metadata;
	d.array=array; d.array_length=sizeof array; d.id[0]=0xef;
	struct ar_flash_spi spi; struct ar_sfdp_info discovered; uint8_t id[6], record[8];
	assert(!ar_flash_spi_init(&spi,&io,&d,0));
	assert(!ar_flash_spi_discover(&spi,scratch,sizeof scratch,id,&discovered));
	assert(spi.capacity==sizeof array);
	struct ar_flash_factory_info location;
	assert(!ar_flash_read_factory(ar_flash_spi_read,&spi,spi.capacity,scratch,sizeof scratch,record,&location));
	assert(location.partition_offset==0xa000 && spi.transfers==6);
	struct ar_power_calibration cal={.has_fallback=1,.fallback_offsets={1,0,-1,0},.csma_offset=8};
	for (unsigned level=13;level<=16;level++) {
		cal.valid |= 1u<<(level-9);
		memcpy(cal.rows[level-9],(uint8_t[]){level,level+30,level+1,level+40},4);
	}
	struct ar_power_config power={.closed={1,1},.high_clock=1,.dev_role=1};
	assert(ar_power_calibration_resolve(&power,&cal,AR_POWER_FACTORY_RECORD,record,8)==AR_POWER_OFFSET_FACTORY_V4);
	struct ar_power_plan plan;
	assert(!ar_power_plan(&power,0,14,&plan));
	assert(plan.count==4 && plan.writes[0].field==0x9a40806 && plan.writes[0].value==13);
	assert(plan.writes[1].value==43 && plan.writes[2].field==0xda40006 && plan.writes[2].value==17 && plan.writes[3].value==56);
	/* Corrupt primary CRC selects the backup and its own relative LBA base. */
	array[0x8000+528]^=1;
	assert(!ar_flash_read_factory(ar_flash_spi_read,&spi,spi.capacity,scratch,sizeof scratch,record,&location));
	assert(location.table_base==0x9000 && location.partition_offset==0xb000 && location.table_errors[0]==-74);
	assert(ar_power_calibration_resolve(&power,&cal,AR_POWER_FACTORY_RECORD,record,8)==AR_POWER_OFFSET_FACTORY_V3);
	int8_t offsets[4]; assert(!ar_power_factory_decode(offsets,record,8));
	assert(!memcmp(offsets,(int8_t[]){-1,-3,2,3},4));
	/* Unknown factory version permits the configured JSON offset fallback. */
	array[0xb000]=2;
	assert(!ar_flash_read_factory(ar_flash_spi_read,&spi,spi.capacity,scratch,sizeof scratch,record,&location));
	assert(ar_power_calibration_resolve(&power,&cal,AR_POWER_FACTORY_RECORD,record,8)==AR_POWER_OFFSET_JSON);
	assert(!ar_power_plan(&power,0,14,&plan) && plan.writes[0].value==15 && plan.writes[2].value==14);
	table(0x8000,"other"); unsigned commands=d.commands;
	assert(ar_flash_read_factory(ar_flash_spi_read,&spi,spi.capacity,scratch,sizeof scratch,record,&location)==-2);
	assert(d.commands==commands+1 && location.table_base==0x8000);
	assert(ar_power_calibration_resolve(&power,&cal,AR_POWER_FACTORY_UNAVAILABLE,NULL,0)==AR_POWER_OFFSET_JSON);
	/* Failed IO cannot expose a partial factory record or issue a backup
 * command on a faulted transport. Policy must explicitly select fallback. */
	table(0x8000,"factory"); uint8_t saved[8]; memcpy(saved,record,8);
	d.fifo_stall=1; d.step=1000; commands=d.commands;
	assert(ar_flash_read_factory(ar_flash_spi_read,&spi,spi.capacity,scratch,sizeof scratch,record,&location)==-110);
	assert(d.commands==commands+1 && spi.error==-110 && !memcmp(record,saved,8));
	assert(location.table_errors[0]==-110 && location.table_errors[1]==-110);
	/* Production orchestration makes one bounded attempt and records which
 * stage failed, while exposing a record only on a complete read. */
	struct ar_factory_info info;
	struct ar_flash_part part={{0xef,0,0,0,0,0},1,0,sizeof array};
	reset(&d); d.sfdp=metadata; d.sfdp_length=sizeof metadata; d.id[0]=0xef;
	d.array=array; d.array_length=sizeof array;
	assert(!ar_factory_read(&io,&d,&part,1,scratch,sizeof scratch,record,&info));
	assert(info.stage==AR_FACTORY_READY && !info.error && info.transfers==3 && d.commands==3);
	assert(info.identity.source==AR_FLASH_ID_TABLE && info.partition.partition_offset==0xa000);
	assert(!memcmp(record,array+0xa000,8));
	memcpy(saved,record,8); reset(&d); d.idle_stall=1;
	assert(ar_factory_read(&io,&d,&part,1,scratch,sizeof scratch,record,&info)==-110);
	assert(info.stage==AR_FACTORY_CONTROLLER && info.error==-110 && !d.writes && !memcmp(record,saved,8));
	reset(&d); d.sfdp=metadata; d.sfdp_length=sizeof metadata; d.id[0]=0xef; metadata[0]=0;
	assert(ar_factory_read(&io,&d,NULL,0,scratch,sizeof scratch,record,&info)==-74);
	assert(info.stage==AR_FACTORY_IDENTIFY && info.error==-74 && !memcmp(record,saved,8));
	assert(info.id_valid && info.observed_id[0]==0xef && !info.identity.source);
	metadata[0]='S'; reset(&d); d.sfdp=metadata; d.sfdp_length=sizeof metadata; d.id[0]=0xef;
	d.array=array; d.array_length=sizeof array; d.stall_command=3;
	assert(ar_factory_read(&io,&d,&part,1,scratch,sizeof scratch,record,&info)==-110);
	assert(info.stage==AR_FACTORY_PARTITION && info.partition.record_error==-110 && info.transfers==2);
	assert(d.commands==3 && !memcmp(record,saved,8));
	reset(&d); unsigned writes=d.writes; part.capacity=0;
	assert(ar_factory_read(&io,&d,&part,1,scratch,sizeof scratch,record,&info)==-22 && d.writes==writes);
	puts("SPI discovery to factory partition, offsets and native power plan passed");
}
