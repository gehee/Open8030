/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_FILTER_H
#define AR_DATA_FILTER_H
#include <stdint.h>

/* Native views of an already admitted route's parser predicates. Role filtering
 * and construction precede this stage; worker selection and actions follow it.
 * These are not original object layouts. The caller serializes registry access. */
struct ar_data_identity { uint32_t type; uint16_t port, selector; };
struct ar_data_filter {
	uint64_t flags;
	uint16_t source, secondary;
	uint8_t minimum, maximum;
	uint16_t offset;
	const uint8_t *pattern;
	uint16_t pattern_length;
};
struct ar_data_filter_input {
	struct ar_data_identity source, secondary;
	const uint8_t *data;
	uint32_t length;
	uint8_t metadata;
};
struct ar_data_filter_ops {
	/* Return zero and write output on success; nonzero means lookup failure.
	 * Preserve registry order/first-match behavior, including duplicate IDs. */
	int (*identity)(const struct ar_data_identity *, uint16_t *id, void *context);
	int (*framed)(uint16_t id, uint8_t *enabled, void *context);
};

/* 1 = match, 0 = rejection, -22 = native adapter/storage guard.
 * Bits 0/1/2/4 select source ID, metadata range, secondary ID and byte pattern.
 * Bit 3 (role admission) and other bits have no predicate effect here.
 * Metadata bounds apply only when the source lookup's ID resolves to a framed
 * interface. Range endpoints are inclusive. Lookups are repeated as originally.
 * Pattern matching uses the supplied logical data span; preparation of that
 * pointer/length is outside this helper. The native bounds guard rejects an
 * out-of-span read which the original predicate would perform unchecked. */
int ar_data_filter_match(const struct ar_data_filter *,
	const struct ar_data_filter_input *, const struct ar_data_filter_ops *,
	void *context);
#endif
