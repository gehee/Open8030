/* SPDX-License-Identifier: MIT */
#include "ar_power_host.h"

int ar_power_host_request(struct ar_power_control *power, uint32_t mode,
	uint32_t request, const uint8_t *payload, size_t length,
	uint8_t output[2], size_t *output_length,
	const struct ar_power_host_ops *ops, void *context)
{
	if (!output_length) { return -22; }
	*output_length = 0;
	if (!power) { return -22; }
	if (request == UINT32_C(0x01000007)) {
		if (length || !output) { return -22; }
		output[0] = mode; *output_length = 1; return 0;
	}
	if (request == UINT32_C(0x01000008)) {
		if (!payload || length != 1 || !output) { return -22; }
		unsigned user = payload[0];
		output[0] = user; output[1] = user < AR_POWER_USERS ? power->level[user] : 255;
		*output_length = 2; return 0;
	}
	if (request != UINT32_C(0x02000008)) { return -95; }
	if (!payload || length != 2 || payload[0] >= AR_POWER_USERS || !ops ||
		!ops->lock || !ops->unlock || !ops->field) { return -22; }
	ops->lock(context);
	int result = ar_power_control_apply(power,payload[0],payload[1],ops->field,context);
	ops->unlock(context);
	return result;
}
