/* SPDX-License-Identifier: MIT */
#ifndef AR_CATALOG_H
#define AR_CATALOG_H
#include <stddef.h>
#include <stdint.h>

/* Native views of catalog state, not original firmware structure declarations.
 * Rows use the same frequency/enable/band fields as ar_scan. Groups are separate
 * low-nibble values. The full configured frequencies and processed scan scores
 * must be supplied independently when serializing channel information. */
enum ar_catalog_action {
	AR_CATALOG_MODE = 1,
	AR_CATALOG_CHANNEL,
	AR_CATALOG_AP_LOCK,
	AR_CATALOG_AP_TUNE,
	AR_CATALOG_AP_UNLOCK,
	AR_CATALOG_RESET_SCAN,
};
struct ar_catalog_selection {
	uint16_t channel, ap_channel;
	uint8_t role, policy_flags, ap_state, group_count, current_group;
	uint16_t best_group[16];
};
/* MODE/CHANNEL correspond to the channel-policy helper with selector 1/user12.
 * AP_TUNE uses user8/TX. Callbacks run synchronously and may update selection;
 * current_group is read from the resulting channel after the policy callback.
 * Callbacks must preserve the structural validity of the supplied state.
 * RESET_SCAN represents the original auxiliary-scan reset helper, not a no-op.
 * Lock callbacks must be paired by the caller's implementation. */
typedef void (*ar_catalog_effect)(enum ar_catalog_action action, uint32_t value,
	void *context);

int ar_catalog_information(const uint32_t *frequencies, const int32_t *scores,
	size_t count, uint8_t mode, uint8_t selected, uint8_t working,
	uint8_t output[1028]);
int ar_catalog_enabled(const uint32_t *rows, size_t count, uint8_t output[129]);
/* indices contains the list only, without its count byte. A count outside
 * 1..128 or a NULL list returns original -6. Native preflight validation uses
 * -22 and leaves state untouched. Invalid/forbidden indices are skipped, but
 * the original fallback/AP request still uses indices[0], even if it was
 * skipped. The callback must perform the policy helper's own validation. */
int ar_catalog_set_work(uint32_t *rows, const uint8_t *groups, size_t count,
	uint8_t band_mask, const uint8_t *indices, size_t length,
	struct ar_catalog_selection *selection, ar_catalog_effect effect, void *context);
#endif
