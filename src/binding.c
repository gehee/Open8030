/* SPDX-License-Identifier: MIT */
#include "ar_binding.h"
#include <string.h>

int ar_binding_set(struct ar_binding *binding, const uint8_t *data, size_t length)
{
	if (!binding || !data || length != 14) { return -22; }
	/* Original DEV branch ignores the AP slot mask in byte 1 and treats
	 * every nonzero enable byte alike. The trailing three identities exclude
	 * peers from pairing interpretation, not from ordinary admission. */
	binding->enabled = !!data[0];
	binding->complete = 0;
	if (binding->enabled) { memcpy(binding->skip, data + 2, 12); }
	return 0;
}

void ar_binding_complete(struct ar_binding *binding, unsigned slot,
	const uint8_t peer[4], const uint8_t sample[16])
{
	if (!binding || !binding->enabled || binding->complete || slot >= 8 || !peer) { return; }
	binding->slot = slot;
	memcpy(binding->peer, peer, 4);
	memset(binding->sample, 0, 16);
	if (sample) { memcpy(binding->sample, sample, 16); }
	binding->complete = 1;
}

int ar_binding_result(const struct ar_binding *binding, uint8_t output[164])
{
	if (!binding || !output) { return -22; }
	/* Stock leaves unused entries/padding unwritten. Deterministic zeros do
	 * not announce a peer: only bitmap entries describe completed results. */
	memset(output, 0, 164);
	if (binding->enabled && binding->complete && binding->slot < 8) {
		output[0] = 1u << binding->slot;
		memcpy(output + 1 + 4 * binding->slot, binding->peer, 4);
		memcpy(output + 36 + 16 * binding->slot, binding->sample, 16);
	}
	return 0;
}
