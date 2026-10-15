/* SPDX-License-Identifier: MIT */
#include "ar_binding.h"
#include <assert.h>
#include <string.h>

int main(void)
{
	struct ar_binding binding = {0};
	uint8_t command[14] = {1, 1, 1, 2, 3, 4};
	uint8_t output[164], zero[164] = {0};
	const uint8_t peer[4] = {4, 3, 2, 1};
	uint8_t sample[16];
	for (unsigned i = 0; i < 16; i++) { sample[i] = i + 1; }
	ar_binding_complete(&binding, 0, peer, sample);
	assert(!ar_binding_result(&binding, output) && !memcmp(output, zero, 164));
	assert(ar_binding_set(&binding, command, 13) == -22 && !binding.enabled);
	for (unsigned slot = 0; slot < 8; slot++) {
		assert(!ar_binding_set(&binding, command, 14));
		assert(!memcmp(binding.skip, command + 2, 12));
		assert(!ar_binding_result(&binding, output) && !memcmp(output, zero, 164));
		ar_binding_complete(&binding, 8, peer, sample);
		assert(!binding.complete);
		ar_binding_complete(&binding, slot, peer, sample);
		assert(!ar_binding_result(&binding, output));
		assert(output[0] == (1u << slot));
		assert(!memcmp(output + 1 + slot * 4, peer, 4));
		assert(!memcmp(output + 36 + slot * 16, sample, 16));
		/* Polling and a subsequent unrelated observation retain the result. */
		ar_binding_complete(&binding, (slot + 1) % 8, command + 2, NULL);
		uint8_t again[164];
		assert(!ar_binding_result(&binding, again) && !memcmp(again, output, 164));
		command[0] = 0;
		assert(!ar_binding_set(&binding, command, 14));
		assert(!ar_binding_result(&binding, output) && !memcmp(output, zero, 164));
		command[0] = 2; /* Original nonzero-enable semantics. */
	}
	assert(!ar_binding_set(&binding, command, 14));
	ar_binding_complete(&binding, 0, peer, NULL);
	assert(!ar_binding_result(&binding, output));
	assert(!memcmp(output + 36, zero, 16));
	return 0;
}
