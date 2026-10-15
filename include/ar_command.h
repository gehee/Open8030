/* SPDX-License-Identifier: MIT */
#ifndef AR_COMMAND_H
#define AR_COMMAND_H
#include <stddef.h>
#include <stdint.h>
#define AR_COMMAND_LIMIT 20u
#define AR_COMMAND_DESCRIPTORS 47u
#define AR_COMMAND_USERS 12u
struct ar_command_descriptor { uint16_t word; int16_t displacement; };
struct ar_command_profile { uint32_t frequency[2]; uint8_t bandwidth[2], rx_flag; };
/* Native metadata representation: opcode[3:0], raw user[7:4], mode[9:8],
 * bandwidth[12:10], calibration bank[14:13], RX flag[15]. Other bits retained. */
struct ar_command_entry { uint32_t bits, frequency; };
struct ar_command_program {
	struct ar_command_entry entries[AR_COMMAND_LIMIT];
	uint8_t count;
	int8_t cursor;
};
typedef void (*ar_command_write)(uint32_t field, uint32_t value, void *context);
/* Caller must hold command execution while installing. No allocation; all
 * input checks precede writes. Unchanged metadata fields retain their values.
 * Descriptor: opcode[3:0], mode[5:4], raw user[9:6], pre-command enable[10],
 * pre-command mode[13:11], then signed displacement in expanded slots. */
int ar_command_build(struct ar_command_program *program,
	const struct ar_command_descriptor *descriptors, size_t count, unsigned dev_role,
	const struct ar_command_profile profiles[AR_COMMAND_USERS], ar_command_write write, void *context);
/* First matching entry only. Properties: 0 bandwidth, 1 bank, 2 mode,
 * 3 frequency, 4 RX flag. Mode updates match either direction; RX flags only
 * change for receive updates. Returns 1 if matched, 0 if absent, -22 if invalid. */
int ar_command_update(struct ar_command_program *program, unsigned dev_role,
	unsigned user, unsigned direction, unsigned property, uint32_t value, unsigned rx_flag);
struct ar_command_state {
	uint8_t calibration[3][5], valid_banks, cached_bank;
	int8_t board_band;
	uint8_t tx_flags, pending_tx;
};
/* Preparation and TX look-ahead effects. Initialize cached_bank=3 and
 * board_band=-1 after calibration/default writes. A missing calibration bank
 * is never substituted with another bank. Byte writes use baseband offsets.
 * Returns the next entry index, -1 if no selection, or -22 for invalid input.
 * The caller still owns non-preparation scan actions, TX buffer flag lifetime
 * and the board's separate PA-sleep policy. This is not the whole IRQ handler. */
int ar_command_step(struct ar_command_program *program, uint32_t command,
	struct ar_command_state *state, unsigned dev_role, unsigned work_mode,
	unsigned bypass, uint32_t compensation, ar_command_write write_field,
	ar_command_write write_byte, void *context);
#endif
