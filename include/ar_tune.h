/* SPDX-License-Identifier: MIT */
#ifndef AR_TUNE_H
#define AR_TUNE_H
#include "ar_command.h"

struct ar_subchannel_config { int32_t offset_khz; uint8_t main_bandwidth, count; };
/* Original channel resolver: low byte indexes the catalog; all remaining bits
 * form the subchannel number. NULL config disables subchannel adjustment.
 * Invalid catalog index returns frequency0; native bad storage returns -22.
 * main_bandwidth0..5 means1250<<code kHz; other codes use zero bandwidth. */
int ar_channel_resolve(const uint32_t *rows, size_t count,
	const struct ar_subchannel_config *config, uint32_t channel, uint32_t *frequency);

struct ar_tune_gain { uint32_t below_khz; uint8_t value; };
struct ar_tune_config {
	struct ar_tune_gain gain[8];
	int8_t band_offset[2];
	uint8_t dual_band, rf_mode, gain_count;
};
struct ar_tune_power {
	int8_t offset[12];
	uint8_t level[12], enabled;
};
struct ar_tune_ops {
	ar_command_write field, byte;
	void (*band_offset)(unsigned user, int offset, void *context);
	void (*lock)(void *context);
	void (*unlock)(void *context);
	int (*power)(unsigned user, unsigned level, void *context);
	void (*metadata)(unsigned user, unsigned direction, unsigned property,
		uint32_t value, void *context);
};
/* Per-user tuning, TX0/RX1. Caller supplies a validated 40-bit PLL word and
 * serializes the whole operation. Five PLL bytes precede RF path fields;
 * RX gain hints or TX offset/power effects precede bank/frequency metadata.
 * Power refresh errors do not undo original state changes. TX requires power
 * state even when disabled; RX may pass NULL. Gain tables preserve supplied
 * order: first threshold strictly above frequency, otherwise the last row.
 * Native validation returns -22 before effects. Physical tuning, PLL arithmetic
 * and implementations of the TX power callbacks remain caller responsibilities. */
int ar_tune_apply(unsigned user, unsigned direction, uint32_t frequency,
	uint64_t pll, int32_t offset, const struct ar_tune_config *config,
	struct ar_tune_power *power, const struct ar_tune_ops *ops, void *context);
#endif
