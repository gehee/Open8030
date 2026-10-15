/* SPDX-License-Identifier: MIT */
#ifndef AR_POWER_HOST_H
#define AR_POWER_HOST_H
#include "ar_power_control.h"
#include <stddef.h>
struct ar_power_host_ops {
	void (*lock)(void *context);
	void (*unlock)(void *context);
	ar_power_field field;
};
/* Compared source-only helper; runtime dispatch remains unsupported.
 * Original mode/level getters and direct per-user setter. Caller supplies the
 * original mode value separately from per-path closed/open configuration.
 * Native malformed requests and unsafe setter users return -22. Invalid getter
 * users return {user,255}. Cached levels include the original user0 alias.
 * output_length is zero on failure or a setter; getters return one/two bytes. */
int ar_power_host_request(struct ar_power_control *power, uint32_t mode,
	uint32_t request, const uint8_t *payload, size_t length,
	uint8_t output[2], size_t *output_length,
	const struct ar_power_host_ops *ops, void *context);
#endif
