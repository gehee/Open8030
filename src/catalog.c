/* SPDX-License-Identifier: MIT */
#include "ar_catalog.h"
#include <string.h>

static void put32(uint8_t *out, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++) { out[i] = value >> (8*i); }
}
int ar_catalog_information(const uint32_t *frequencies, const int32_t *scores,
	size_t count, uint8_t mode, uint8_t selected, uint8_t working,
	uint8_t output[1028])
{
	if (!output || count > 128 || (count && (!frequencies || !scores))) { return -22; }
	/* The original only writes count entries. Initialize the unused wire tail
	 * instead of exposing previous response-buffer contents. */
	memset(output,0,1028);
	output[0] = count; output[1] = mode; output[2] = selected; output[3] = working;
	for (size_t i = 0; i < count; i++) {
		put32(output+4+4*i,frequencies[i]); put32(output+516+4*i,(uint32_t)scores[i]);
	}
	return 0;
}
int ar_catalog_enabled(const uint32_t *rows, size_t count, uint8_t output[129])
{
	if (!output) { return -6; }
	if (count > 128) { return -22; }
	memset(output,0,129);
	if (rows) {
		for (size_t i = 0; i < count; i++) {
			if (rows[i] & 0x01000000) { output[++output[0]] = i; }
		}
	}
	return 0;
}
int ar_catalog_set_work(uint32_t *rows, const uint8_t *groups, size_t count,
	uint8_t band_mask, const uint8_t *indices, size_t length,
	struct ar_catalog_selection *selection, ar_catalog_effect effect, void *context)
{
	if (!indices || length < 1 || length > 128) { return -6; }
	if (!rows || !groups || !selection || !effect || !count || count > 128 ||
		selection->group_count > 16 || (selection->channel & 255) >= count) { return -22; }
	uint8_t enabled[16] = {0};
	unsigned missing = 1;
	for (size_t i = 0; i < length; i++) {
		unsigned index = indices[i];
		if (index >= count || !(band_mask & (1u << ((rows[index] >> 27) & 3)))) { continue; }
		enabled[index >> 3] |= 1u << (index & 7);
		if (index == selection->channel) { missing = 0; }
	}
	for (size_t i = 0; i < count; i++) {
		rows[i] = (rows[i] & ~UINT32_C(0x01000000)) |
			(uint32_t)((enabled[i >> 3] >> (i & 7)) & 1) << 24;
	}
	if ((selection->policy_flags & 6) == 6) { effect(AR_CATALOG_MODE,2,context); }
	else if (missing) { effect(AR_CATALOG_CHANNEL,indices[0],context); }
	if (!selection->role) {
		effect(AR_CATALOG_AP_LOCK,0,context);
		if (!selection->ap_state) {
			selection->ap_channel = indices[0];
			effect(AR_CATALOG_AP_TUNE,indices[0],context);
		}
		effect(AR_CATALOG_AP_UNLOCK,0,context);
	}
	for (unsigned group = 0; group < selection->group_count; group++) {
		unsigned found = 0;
		for (size_t i = 0; i < count; i++) {
			if ((rows[i] & 0x01000000) && (group == 3 || (groups[i] & 15) == group)) { found = 1; break; }
		}
		if (!found) { selection->best_group[group] = 255; }
	}
	/* A policy callback must leave a valid current channel, as required by the
	 * original unchecked group lookup. Reject a broken callback safely. */
	if ((selection->channel & 255) >= count) { return -22; }
	selection->current_group = groups[selection->channel & 255] & 15;
	effect(AR_CATALOG_RESET_SCAN,0,context);
	return 0;
}
