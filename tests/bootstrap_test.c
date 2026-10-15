/* SPDX-License-Identifier: MIT */
#include "bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct { uint32_t address, value; } cells[96];
static unsigned used, writes, reset_requested, ready, fch, stable;
static uint32_t first_address, last_address, last_value;
static unsigned locate(uint32_t address)
{
	address &= ~3u;
	for (unsigned i=0;i<used;i++) { if (cells[i].address==address) { return i; } }
	assert(used<96); cells[used].address=address; cells[used].value=0x5a5a5a5a;
	return used++;
}
uint32_t ar_bootstrap_read(uint32_t address, unsigned width)
{
	if (address==0x0a100284) { return ready ? 0x400000 : 0; }
	if (address==0x0a110f00) { return fch ? 8 : 0; }
	if (address==0xe000c004) { return stable ? 0 : 1; }
	uint32_t value=cells[locate(address)].value >> ((address&3)*8);
	return width==4 ? value : value & ((1u << (width*8))-1);
}
void ar_bootstrap_write(uint32_t address, uint32_t value, unsigned width)
{
	assert((address>=0x0a090000 && address<0x0a112000) ||
	       (address>=0xeffff000 && address<0xeffff040));
	assert((width==1 || width==2 || width==4) && address%width==0);
	unsigned i=locate(address),shift=(address&3)*8;
	uint32_t mask=width==4 ? 0xffffffffu : ((1u << (width*8))-1)<<shift;
	cells[i].value=(cells[i].value&~mask)|(value<<shift&mask);
	if (!writes++) { first_address=address; }
	last_address=address; last_value=value;
	if (address==0x0a090100 && value==1) { reset_requested++; }
}
static void seed(uint32_t address, uint32_t value) { cells[locate(address)].value=value; }
static void init(unsigned flag, int adjustment)
{
	memset(cells,0,sizeof cells); used=writes=reset_requested=0;
	ready=fch=stable=1;
	seed(0x0a098ff8,1000 | flag<<16 | (uint32_t)(uint8_t)adjustment<<24);
	seed(0x0a098ffc,10000);seed(0xe000c000,0);seed(0xe000bff8,13000);seed(0xe000bffc,0);
}
int main(void)
{
	assert(ar_bootstrap_trim(9,0,13000,10000)==10);
	assert(ar_bootstrap_trim(10,0,13000,10000)==10);
	assert(ar_bootstrap_trim(-9,0,10000,13000)==-10);
	assert(ar_bootstrap_trim(-10,0,10000,13000)==-10);
	assert(ar_bootstrap_trim(3,0,12000,10000)==3);
	assert(ar_bootstrap_trim(3,0,2010001,10000)==0);
	assert(ar_bootstrap_trim(-3,1,13000,10000)==-2);
	init(1,7);
	assert(ar_bootstrap_run()==AR_BOOTSTRAP_RESET && reset_requested==1);
	assert(first_address==0xeffff000 && last_address==0x0a090100 && last_value==1);
	assert(ar_bootstrap_read(0x0a090134,4)==0x0a098a01);
	assert(ar_bootstrap_read(0x0a098ffa,2)==0); /* halfword clears flag and adjustment */
	assert(ar_bootstrap_read(0x0a090004,4)==1000);
	assert(ar_bootstrap_read(0x0a110328,4)==(0x5a5a5a5a & ~0xff0000u));
	assert(ar_bootstrap_read(0x0a110800,4)==(0x5a5a5a5a & ~9u));
	init(0,3);
	assert(ar_bootstrap_run()==AR_BOOTSTRAP_RESET);
	assert(ar_bootstrap_read(0x0a098ffb,1)==4 && ar_bootstrap_read(0x0a090004,4)==1004);
	init(1,0); ready=0;
	assert(ar_bootstrap_run()==AR_BOOTSTRAP_ROM_FALLBACK && !reset_requested);
	assert(ar_bootstrap_read(0x0a090134,4)==0);
	init(1,0); fch=0;
	assert(ar_bootstrap_run()==AR_BOOTSTRAP_ROM_FALLBACK && !reset_requested);
	init(0,3); stable=0;
	assert(ar_bootstrap_run()==AR_BOOTSTRAP_ROM_FALLBACK && !reset_requested);
	puts("bootstrap trim bounds, register preservation, reset handoff and failure paths passed");
	return 0;
}
