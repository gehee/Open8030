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

static void prepare_board(struct ar_command_state *state, unsigned direction,
	uint32_t frequency, ar_command_write write, void *context)
{
	if (direction || frequency < 1000000 || frequency >= 8000000) { return; }
	unsigned band = frequency < 2000000 ? 5 : frequency < 3000000 ? 3 :
		frequency < 4000000 ? 4 : frequency < 5000000 ? 0 : frequency < 6000000 ? 1 : 2;
	if (state->board_band == (int)band) { return; }
	static const uint8_t offsets[2][6] = {{9,0x5b,0x83,0x85,0x58,0x5e},{0x54,0x55,0x50,0x4d,0x51,0}};
	static const uint8_t values[6][6] = {
		{0x1e,1,8,0x11,0,0}, {0x0e,3,8,0x11,0,0}, {0,2,6,0x19,0xdb,8},
		{0,0,3,0,0,0}, {0x0a,0xc0,0,0xdb,3,0}, {8,0xc0,0x18,0xdb,3,0}};
	for (unsigned i = 0; i < (band < 3 ? 6u : 5u); i++) {
		write(offsets[band >= 3][i],values[band][i],context);
	}
	state->board_band = band;
}

int ar_command_step(struct ar_command_program *program, uint32_t command,
	struct ar_command_state *state, unsigned dev_role, unsigned work_mode,
	unsigned bypass, uint32_t compensation, ar_command_write write_field,
	ar_command_write write_byte, void *context)
{
	unsigned raw = command >> 16 & 15, mode = command >> 20 & 3;
	unsigned preparing = mode == 0 || mode == 3;
	if (!program || !state || !write_field || !write_byte || dev_role > 1 || work_mode > 3 ||
	    program->count > AR_COMMAND_LIMIT || program->cursor < -1 || state->cached_bank > 3 ||
	    (!preparing && program->count && program->cursor >= program->count)) { return -22; }
	if (!program->count) { return -1; }
	if (preparing) {
		program->cursor = program->cursor < program->count - 1 ? program->cursor + 1 : 0;
		if ((program->entries[program->cursor].bits >> 4 & 15) != raw) {
			program->cursor = -1;
			for (unsigned i = 0; i < program->count; i++) {
				uint32_t bits = program->entries[i].bits;
				/* The original fallback compares the hardware mode (0/3),
				 * unlike the fast path which compares only the raw user. */
				if ((bits >> 4 & 15) == raw && (bits >> 8 & 3) == mode) {
					program->cursor = i; break;
				}
			}
		}
	}
	if (program->cursor < 0) { return -1; }
	unsigned next = (program->cursor + 1) % program->count;
	uint32_t bits = program->entries[program->cursor].bits;
	if (preparing) {
		prepare_board(state,mode != 0,program->entries[program->cursor].frequency,write_byte,context);
	} else if ((program->entries[next].bits >> 8 & 3) == 2) {
		unsigned bw = program->entries[next].bits >> 10 & 7;
		write_field(0xa480020,bw == 4 ? 0x40404040 : bw == 5 ? 0x80808080 : 0,context);
	}
	if (bypass || !preparing) { return next; }
	unsigned bank = bits >> 13 & 3;
	if ((bits >> 8 & 3) == 2 && bank != state->cached_bank && bank < 3 && (state->valid_banks & (1u << bank))) {
		if (state->cached_bank == 3) {
			write_field(0x4800101,1,context);
			write_field(0x4a01008,255,context); write_field(0x4a80008,255,context);
		}
		static const uint32_t fields[5] = {0x4a00008,0x4a00808,0x4a41008,0x4a41808,0x48c0008};
		for (unsigned i = 0; i < 5; i++) {
			write_field(fields[i],state->calibration[bank][i] | (i == 4 ? compensation & 0x33 : 0),context);
		}
		state->cached_bank = bank;
	}
	if (!dev_role && raw == 9) {
		write_field(0xbb00002,0,context); write_field(0xbb01701,1,context);
	} else if (!raw && (state->tx_flags & 4)) {
		if (dev_role) { write_field(0x8000201,0,context); state->pending_tx |= 1; }
		state->tx_flags &= ~4u;
	}
	if (work_mode == 1) {
		write_field(0xc000301,bits >> 15 & 1,context); write_field(0xc001001,bits >> 15 & 1,context);
	}
	return next;
}
