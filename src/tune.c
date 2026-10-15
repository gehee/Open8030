/* SPDX-License-Identifier: MIT */
#include "ar_tune.h"

int ar_channel_resolve(const uint32_t *rows, size_t count,
	const struct ar_subchannel_config *config, uint32_t channel, uint32_t *frequency)
{
	if (!frequency || (!rows && count) || count > 255) { return -22; }
	unsigned index = channel & 255;
	uint32_t result = 0;
	if (index < count) {
		result = rows[index] & 0xffffff;
		uint32_t sub = channel >> 8;
		if (config && sub && sub <= config->count) {
			uint32_t width = config->main_bandwidth < 6 ? 1250u << config->main_bandwidth : 0;
			result += (uint32_t)config->offset_khz - (width >> 1) +
				((sub*2-1)*width) / ((uint32_t)config->count*2);
		}
	}
	*frequency = result;
	return 0;
}

int ar_tune_apply(unsigned user, unsigned direction, uint32_t frequency,
	uint64_t pll, int32_t offset, const struct ar_tune_config *config,
	struct ar_tune_power *power, const struct ar_tune_ops *ops, void *context)
{
	if (user >= 12 || direction > 1 || pll >> 40 || !config || config->gain_count > 8 ||
		!ops || !ops->field || !ops->byte || !ops->metadata ||
		(!direction && (!power || (config->dual_band && user < 10 && !ops->band_offset) ||
			(power->enabled && (!ops->lock || !ops->unlock || !ops->power))))) { return -22; }
	static const uint16_t offsets[2][12] = {
		{0x910,0x915,0x91a,0x920,0x925,0x92a,0x930,0x935,0x900,0x965,0x90a,0x90a},
		{0x93a,0x940,0x945,0x94a,0x950,0x955,0x95a,0x960,0x905,0x96a,0x90a,0x90a},
	};
	for (unsigned i = 0; i < 5; i++) {
		ops->byte(offsets[direction][user]+i, (uint32_t)(pll >> (32-8*i)) & 255, context);
	}
	unsigned bank = frequency <= 1000000 ? 0 : frequency <= 4300000 ? 1 : 2;
	unsigned dual = config->dual_band && bank == 2;
	uint32_t base = (0x9a4 + 8*user) << 16;
	unsigned select = direction ? (bank == 2 ? 4 : bank == 1 ? 1 : 2) :
		(bank == 2 ? 4 : bank == 1 && config->rf_mode != 1 ? 2 : 1);
	unsigned low = direction ? 0x1601 : 0x1701, high = direction ? 0x1201 : 0x1301;
	ops->field(base | (direction ? 0x1903 : 0x1d03), select, context);
	ops->field(base | (dual ? high : low), 1, context);
	ops->field(base | (dual ? low : high), !config->dual_band, context);
	if (!direction) {
		if (config->dual_band && user < 10) { ops->band_offset(user, config->band_offset[bank == 2], context); }
		if (power->enabled) {
			ops->lock(context);
			if (power->offset[user] != offset) {
				uint8_t byte = (uint8_t)offset;
				power->offset[user] = byte < 128 ? (int8_t)byte : (int8_t)((int)byte - 256);
				(void)ops->power(user, power->level[user], context);
			}
			ops->unlock(context);
		}
	} else if (user < 11) {
		ops->field(((user >> 1) + 0x2a0) * 0x40000 | (user & 1) << 12 | 0x10,
			frequency / 2000, context);
		uint8_t hint = 0x3c;
		if (user != 10 && config->gain_count) {
			unsigned index = 0;
			while (index+1 < config->gain_count && frequency >= config->gain[index].below_khz) { index++; }
			hint = config->gain[index].value;
		}
		ops->field((0x9a0 + 8*user) << 16 | 0x1808, hint, context);
	}
	ops->metadata(user, direction, 1, bank, context);
	ops->metadata(user, direction, 3, frequency, context);
	return 0;
}
