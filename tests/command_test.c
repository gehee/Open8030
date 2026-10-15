/* SPDX-License-Identifier: MIT */
#include "ar_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t registers[1024];
static unsigned writes;
static void write_field(uint32_t encoded, uint32_t value, void *context)
{
	(void)context;
	unsigned offset = encoded >> 16, shift = encoded >> 8 & 31, width = encoded & 31;
	assert(offset >= 0x810 && offset <= 0x8fc && !(offset & 3) && width && shift + width <= 32);
	uint32_t mask = ((1u << width) - 1) << shift;
	registers[offset / 4] = (registers[offset / 4] & ~mask) | (value << shift & mask);
	writes++;
}
int main(void)
{
	struct ar_command_program p = {0};
	struct ar_command_profile profiles[AR_COMMAND_USERS];
	for (unsigned i = 0; i < AR_COMMAND_USERS; i++) {
		profiles[i] = (struct ar_command_profile){{5770000,5805000},{1,2},0};
	}
	struct ar_command_descriptor frame[] = {{0x3621,0},{0x358,0},{0x3413,0}};
	assert(!ar_command_build(&p,frame,3,1,profiles,write_field,NULL));
	assert(writes == 320 && p.count == 3 && p.cursor == -1);
	assert(registers[0x810/4] == 0x183a113a && registers[0x814/4] == 0x00802309);
	assert(registers[0x870/4] == 0x0f00dd88 && registers[0x8a4/4] == 0xe000);
	assert(p.entries[0].bits == 0x4981 && p.entries[1].bits == 0x49d8 && p.entries[2].bits == 0x4603);
	assert(p.entries[0].frequency == 5805000 && p.entries[2].frequency == 5770000);
	/* Reserved command bits and unused metadata survive rebuilding. */
	registers[0x810/4] |= 0x40404040;
	p.entries[2].bits |= 0xabcd8000; p.entries[5].bits = 0xfeedbeef;
	assert(!ar_command_build(&p,frame,3,1,profiles,write_field,NULL));
	assert(registers[0x810/4] == (0x183a113a | 0x40404040));
	assert(p.entries[2].bits == 0xabcdc603 && p.entries[5].bits == 0xfeedbeef);
	/* First-match updates must not alter another entry with the same user. */
	p.entries[3] = p.entries[0]; p.count = 4;
	assert(ar_command_update(&p,1,8,1,0,5,0) == 1);
	assert((p.entries[0].bits >> 10 & 7) == 5 && (p.entries[3].bits >> 10 & 7) == 2);
	assert(ar_command_update(&p,1,8,0,3,123,0) == 0);
	assert(ar_command_update(&p,1,8,1,4,0,1) == 1 && (p.entries[0].bits & 0x8000));
	struct ar_command_program saved = p;
	unsigned before = writes;
	frame[1].word = 15 << 6;
	assert(ar_command_build(&p,frame,3,1,profiles,write_field,NULL) == -22);
	assert(ar_command_build(&p,NULL,48,1,profiles,write_field,NULL) == -22);
	profiles[0].bandwidth[0] = 8;
	assert(ar_command_build(&p,NULL,0,1,profiles,write_field,NULL) == -22);
	assert(ar_command_update(&p,1,0,2,0,0,0) == -22);
	assert(writes == before && !memcmp(&saved,&p,sizeof p));
	profiles[0].bandwidth[0] = 1;
	assert(!ar_command_build(&p,NULL,0,1,profiles,write_field,NULL));
	assert(!p.count && p.cursor == -1 && p.entries[5].bits == 0xfeedbeef);
	puts("command expansion, metadata retention and validation passed");
}
