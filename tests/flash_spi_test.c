/* SPDX-License-Identifier: MIT */
#include "ar_flash_spi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "flash_spi_fixture.h"

int main(void)
{
	struct device d; struct ar_flash_spi spi; reset(&d); d.stale=3;
	assert(!ar_flash_spi_init(&spi,&io,&d,0x1000000));
	assert(spi.ready && !spi.error && !d.stale && !d.commands);
	assert(d.clock==((0xa5a5a5a5u&~0x137fu)|0x1004));
	assert(d.regs[0x44/4]==1 && !d.regs[0x40/4]);
	assert(d.regs[0x20/4]==(0x5a5a5a5au&~0x8080u));
	assert(d.regs[8/4]==(0x5a5a5a5au&~0x3c00u));
	assert(d.regs[4/4]==(0x5a5a5a5au&~0x80000u));
	assert(d.regs[0x10/4]==(0x5a5a5a5au&~0x3fcu));
	uint8_t output[4098]; memset(output,0xa5,sizeof output); d.delay=3;
	assert(!ar_flash_spi_read_id(&spi,output+1));
	assert(output[0]==0xa5 && output[7]==0xa5 && spi.transfers==1 && spi.bytes==6);
	for (unsigned i=0;i<6;i++) { assert(output[i+1]==data_byte(i)); }
	const unsigned lengths[]={1,2,3,4,5,7,8,4095,4096};
	for (unsigned n=0;n<sizeof lengths/sizeof lengths[0];n++) {
		unsigned length=lengths[n], address=0x12345+n;
		memset(output,0xa5,sizeof output);
		assert(!ar_flash_spi_read(&spi,address,output+1,length));
		assert(output[0]==0xa5 && output[length+1]==0xa5 && spi.last_offset==address && spi.last_opcode==3);
		for (unsigned i=0;i<length;i++) { assert(output[i+1]==data_byte(address+i)); }
	}
	assert(!ar_flash_spi_read(&spi,0xffffff,output,1));
	unsigned writes=d.writes;
	assert(ar_flash_spi_read(&spi,0xffffff,output,2)==-22);
	assert(ar_flash_spi_read(&spi,0x1000000,output,1)==-22);
	assert(ar_flash_spi_read(&spi,0,output,4097)==-22);
	assert(ar_flash_spi_read(&spi,0,output,0)==-22);
	assert(ar_flash_spi_read(&spi,0,NULL,8)==-22 && d.writes==writes);
	/* A timeout during transfer latches failure and prevents another command. */
	d.fifo_stall=1; d.step=1000;
	assert(ar_flash_spi_read(&spi,0,output,8)==-110 && spi.error==-110 && !spi.ready);
	writes=d.writes;
	assert(ar_flash_spi_read(&spi,0,output,8)==-110 && d.writes==writes);
	assert(ar_flash_spi_init(&spi,&io,&d,0x1000000)==-110 && d.writes==writes);
	reset(&d); d.idle_stall=1; d.step=1000;
	assert(ar_flash_spi_init(&spi,&io,&d,0x1000000)==-110 && !d.writes);
	reset(&d); assert(!ar_flash_spi_init(&spi,&io,&d,0x1000000)); d.end_stall=1; d.step=1000;
	assert(ar_flash_spi_read(&spi,0,output,8)==-110 && !spi.transfers);
	reset(&d); assert(!ar_flash_spi_init(&spi,&io,&d,0x1000000)); d.stale=1; writes=d.writes;
	assert(ar_flash_spi_read(&spi,0,output,8)==-16 && d.writes==writes);
	reset(&d); assert(!ar_flash_spi_init(&spi,&io,&d,0x1000000)); d.fifo_stall=1; d.step=0;
	assert(ar_flash_spi_read(&spi,0,output,8)==-110); /* Frozen clock still terminates. */
	reset(&d); d.regs[0x10/4]|=1;
	assert(ar_flash_spi_init(&spi,&io,&d,0x1000000)==-16 && !d.writes && !d.commands);
	reset(&d); memset(&spi,0,sizeof spi);
	assert(ar_flash_spi_init(&spi,&io,&d,0x1000001)==-22 && !d.writes);
	assert(ar_flash_spi_init(&spi,&io,&d,0x9fff)==-22 && !d.writes);
	assert(ar_flash_spi_read_id(&spi,output)==-11);
	/* Discovery is the only source of capacity after init with zero. */
	uint8_t metadata[64]={'S','F','D','P',0,1,0,0xff,0,0,1,9,16,0,0,0xff};
	metadata[20]=metadata[21]=metadata[22]=0xff; metadata[23]=3; /* 8 MiB. */
	uint8_t id[6], saved_id[6]; struct ar_sfdp_info found, saved_found;
	reset(&d); d.sfdp=metadata; d.sfdp_length=sizeof metadata; d.id[0]=0xef;
	assert(!ar_flash_spi_init(&spi,&io,&d,0)); writes=d.writes;
	assert(ar_flash_spi_read(&spi,0,output,8)==-11 && d.writes==writes);
	assert(!ar_flash_spi_discover(&spi,output,sizeof output,id,&found));
	assert(spi.capacity==0x800000 && found.readable_bytes==0x800000 && !memcmp(id,d.id,6));
	assert(d.commands==4 && spi.transfers==4 && spi.last_opcode==0x5a && spi.last_offset==16);
	assert(!ar_flash_spi_read(&spi,0x7fffff,output,1)); writes=d.writes;
	assert(ar_flash_spi_read(&spi,0x800000,output,1)==-22 && d.writes==writes);
	memset(id,0xa5,6); memset(&found,0xa5,sizeof found); memcpy(saved_id,id,6); saved_found=found;
	metadata[0]=0;
	assert(ar_flash_spi_discover(&spi,output,sizeof output,id,&found)==-74 && !spi.capacity);
	assert(!memcmp(id,saved_id,6) && !memcmp(&found,&saved_found,sizeof found));
	writes=d.writes; assert(ar_flash_spi_read(&spi,0,output,1)==-11 && d.writes==writes);
	metadata[0]='S'; d.id[0]=0xff;
	assert(ar_flash_spi_discover(&spi,output,sizeof output,id,&found)==-19 && !spi.capacity);
	d.id[0]=0;
	assert(ar_flash_spi_discover(&spi,output,sizeof output,id,&found)==-19 && !spi.capacity);
	d.id[0]=0xef;
	assert(!ar_flash_spi_discover(&spi,output,sizeof output,id,&found));
	d.fifo_stall=1; d.step=1000;
	assert(ar_flash_spi_discover(&spi,output,sizeof output,id,&found)==-110 && !spi.capacity);
	writes=d.writes; assert(ar_flash_spi_read(&spi,0,output,1)==-110 && d.writes==writes);
	/* A matched profile can identify older flash without any SFDP request. */
	reset(&d); d.sfdp=metadata; d.sfdp_length=sizeof metadata; d.id[0]=0xef; d.id[1]=0x12; d.id[2]=0x34;
	assert(!ar_flash_spi_init(&spi,&io,&d,0)); metadata[0]=0;
	struct ar_flash_part parts[]={{{0xef,0x12,0x35},3,0,0x800000},{{0xef,0x12,0x34},3,0,0x400000}};
	struct ar_flash_identity identity;
	assert(!ar_flash_spi_identify(&spi,parts,2,output,sizeof output,&identity));
	assert(identity.source==AR_FLASH_ID_TABLE && identity.capacity==0x400000 && identity.part_index==1 && d.commands==1);
	assert(!identity.sfdp.physical_bytes && !memcmp(identity.id,d.id,6));
	parts[0].id_length=1;
	assert(!ar_flash_spi_identify(&spi,parts,2,output,sizeof output,&identity) && identity.part_index==0 && spi.capacity==0x800000);
	writes=d.writes; parts[0].id_length=7;
	assert(ar_flash_spi_identify(&spi,parts,2,output,sizeof output,&identity)==-22 && d.writes==writes);
	parts[0].id_length=3; parts[1].capacity=0x1000001;
	assert(ar_flash_spi_identify(&spi,parts,2,output,sizeof output,&identity)==-22 && d.writes==writes);
	parts[1].capacity=0x400000; d.id[2]=0x36;
	struct ar_flash_identity saved_identity=identity;
	assert(ar_flash_spi_identify(&spi,parts,2,output,sizeof output,&identity)==-74 && !spi.capacity);
	assert(!memcmp(&identity,&saved_identity,sizeof identity));
	metadata[0]='S';
	assert(!ar_flash_spi_identify(&spi,parts,2,output,sizeof output,&identity));
	assert(identity.source==AR_FLASH_ID_SFDP && identity.part_index==UINT32_MAX && spi.capacity==0x800000);
	puts("read-only SPI commands, FIFO tails, bounds, clock setup and timeout containment passed");
}
