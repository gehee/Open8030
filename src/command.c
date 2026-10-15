/* SPDX-License-Identifier: MIT */
#include "ar_command.h"

static uint32_t byte_field(unsigned base, unsigned slot, unsigned bit, unsigned width)
{
	return (base + slot / 4 * 4) << 16 | (slot % 4 * 8 + bit) << 8 | width;
}
static uint32_t user_field(unsigned slot)
{
	return (0x870 + slot / 8 * 4) << 16 | (slot % 8 * 4) << 8 | 4;
}
static void replace(uint32_t *word, uint32_t mask, uint32_t value)
{
	*word = (*word & ~mask) | (value & mask);
}
int ar_command_build(struct ar_command_program *program,
	const struct ar_command_descriptor *descriptors, size_t count, unsigned dev_role,
	const struct ar_command_profile profiles[AR_COMMAND_USERS], ar_command_write write, void *context)
{
	if (!program || (!descriptors && count) || count > AR_COMMAND_DESCRIPTORS || dev_role > 1 || !profiles || !write) { return -22; }
	for (size_t i = 0; i < count; i++) {
		if (((descriptors[i].word >> 6) & 15) == 15) { return -22; }
	}
	for (unsigned i = 0; i < AR_COMMAND_USERS; i++) {
		if (profiles[i].bandwidth[0] > 7 || profiles[i].bandwidth[1] > 7 || profiles[i].rx_flag > 1) { return -22; }
	}
	struct ar_command_descriptor slots[2 * AR_COMMAND_DESCRIPTORS];
	for (size_t i = 0; i < count; i++) {
		unsigned word = descriptors[i].word, opcode = word & 15, mode = word >> 4 & 3;
		if (dev_role) {
			if (opcode == 1) { mode = 1; }
			else if (opcode == 2 || opcode == 4) { mode = 2; }
			else if (opcode == 3 || opcode == 5) { mode = mode == 1 ? 2 : 1; }
		}
		word = (word & ~0x30u) | mode << 4;
		if (mode == 1) { word &= ~0x400u; }
		slots[2*i] = (struct ar_command_descriptor){0x3c0 | (mode == 1 ? 0x3a : 9), 0};
		slots[2*i+1] = (struct ar_command_descriptor){word, descriptors[i].displacement};
	}
	unsigned n = count * 2;
	for (unsigned i = 0; i <= n; i++) {
		unsigned word = i < n ? slots[i].word : 0;
		write(byte_field(0x810,i,7,1),i == n,context);
		write(byte_field(0x810,i,4,2),word >> 4 & 3,context);
		write(byte_field(0x810,i,0,4),word & 15,context);
		unsigned raw = word >> 6 & 15;
		if (i < n && raw != 15) { write(user_field(i-1),raw,context); write(user_field(i),raw,context); }
	}
	write(user_field(n),15,context); write(user_field(n+1),0,context);
	for (unsigned i = 0; i < 96; i++) {
		write(byte_field(0x8a0,i,7,1),0,context);
		write(byte_field(0x8a0,i,4,3),0,context);
		write(byte_field(0x8a0,i,0,4),0,context);
	}
	for (unsigned i = 0; i < n; i++) {
		unsigned word = slots[i].word, raw = word >> 6 & 15;
		if (raw == 15 || !(word & 0x400)) { continue; }
		int position = ((int)i + slots[i].displacement) % (int)n;
		if (position < 0) { position += n; }
		write(byte_field(0x8a0,position,7,1),1,context);
		write(byte_field(0x8a0,position,4,3),word >> 11 & 7,context);
		write(byte_field(0x8a0,position,0,4),raw,context);
	}
	program->count = 0; program->cursor = -1;
	for (unsigned i = 0; i < n && program->count < AR_COMMAND_LIMIT; i++) {
		unsigned word = slots[i].word, opcode = word & 15;
		if (opcode == 6 || opcode == 7 || opcode == 9 || opcode == 10) { continue; }
		unsigned raw = word >> 6 & 15, mode = word >> 4 & 3;
		struct ar_command_entry *entry = &program->entries[program->count++];
		replace(&entry->bits,0x3ff,opcode | raw << 4 | mode << 8);
		unsigned user = raw < 8 ? raw : raw < 12 ? 8 + (raw - 8) / 2 : raw < 14 ? raw - 2 : 12;
		if (user == 12) { replace(&entry->bits,0x7c00,2 << 10 | 1 << 13); continue; }
		unsigned direction = mode != 2;
		entry->frequency = profiles[user].frequency[direction];
		unsigned bank = entry->frequency <= 1000000 ? 0 : entry->frequency <= 4300000 ? 1 : 2;
		replace(&entry->bits,0x7c00,(unsigned)profiles[user].bandwidth[direction] << 10 | bank << 13);
		if (mode == 1) { replace(&entry->bits,0x8000,(unsigned)profiles[user].rx_flag << 15); }
	}
	return 0;
}
int ar_command_update(struct ar_command_program *program, unsigned dev_role,
	unsigned user, unsigned direction, unsigned property, uint32_t value, unsigned rx_flag)
{
	if (!program || program->count > AR_COMMAND_LIMIT || dev_role > 1 || direction > 1 || property > 4 || rx_flag > 1) { return -22; }
	unsigned raw = user < 8 ? user : user == 8 ? (dev_role ? (direction ? 8 : 9) : (direction ? 9 : 8)) :
		user == 9 ? (dev_role ? (direction ? 10 : 11) : (direction ? 11 : 10)) : user < 12 ? user + 2 : 15;
	for (unsigned i = 0; i < program->count; i++) {
		struct ar_command_entry *entry = &program->entries[i];
		if ((entry->bits >> 4 & 15) != raw || (property != 2 && (entry->bits >> 8 & 3) != (direction ? 1u : 2u))) { continue; }
		if (property == 0) { replace(&entry->bits,0x1c00,value << 10); }
		else if (property == 1) { replace(&entry->bits,0x6000,value << 13); }
		else if (property == 2) { replace(&entry->bits,0x300,value << 8); }
		else if (property == 3) { entry->frequency = value; }
		else if (direction) { replace(&entry->bits,0x8000,rx_flag << 15); }
		return 1;
	}
	return 0;
}
