/* SPDX-License-Identifier: MIT */
#include "ar_data_filter.h"

int ar_data_filter_match(const struct ar_data_filter *filter,
	const struct ar_data_filter_input *input, const struct ar_data_filter_ops *ops,
	void *context)
{
	if (!filter || !input) { return -22; }
	uint16_t id = 0;
	if (filter->flags & 1) {
		if (!ops || !ops->identity) { return -22; }
		if (ops->identity(&input->source, &id, context) || id != filter->source) { return 0; }
	}
	if (filter->flags & 2) {
		if (!ops || !ops->identity || !ops->framed) { return -22; }
		if (ops->identity(&input->source, &id, context)) { return 0; }
		uint8_t framed = 0;
		if (ops->framed(id, &framed, context)) { return 0; }
		if (framed && (input->metadata < filter->minimum ||
			input->metadata > filter->maximum)) { return 0; }
	}
	if (filter->flags & 4) {
		if (!ops || !ops->identity) { return -22; }
		if (ops->identity(&input->secondary, &id, context) || id != filter->secondary) { return 0; }
	}
	if (filter->flags & 16) {
		if (input->length < filter->pattern_length) { return 0; }
		if (!filter->pattern_length) { return 1; }
		if (!input->data || !filter->pattern || filter->offset > input->length ||
			filter->pattern_length > input->length - filter->offset) { return -22; }
		for (uint16_t i = 0; i < filter->pattern_length; ++i) {
			if (input->data[filter->offset + i] != filter->pattern[i]) { return 0; }
		}
	}
	return 1;
}
