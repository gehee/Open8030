/* SPDX-License-Identifier: MIT */
#ifndef AR_BINDING_H
#define AR_BINDING_H
#include <stddef.h>
#include <stdint.h>

/* DEV pairing control (0x02000002) and result (0x01000001).
 * Persistence belongs to the Linux backend. A result is published only after
 * radio login completes, never merely upon seeing an available AP. */
struct ar_binding {
	uint8_t enabled, complete, slot, peer[4], skip[3][4], sample[16];
};
int ar_binding_set(struct ar_binding *binding, const uint8_t *data, size_t length);
void ar_binding_complete(struct ar_binding *binding, unsigned slot,
	const uint8_t peer[4], const uint8_t sample[16]);
int ar_binding_result(const struct ar_binding *binding, uint8_t output[164]);
#endif
