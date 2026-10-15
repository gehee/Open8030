/* SPDX-License-Identifier: MIT */
#include "ar_data_builder.h"
#include <string.h>

static int resolve(const struct ar_data_identity *identity, uint16_t *id,
	const struct ar_data_builder_ops *ops, void *context)
{
	int result = ops->interfaces.identity(identity, id, context);
	return result ? result : *id < 64 ? 0 : -10000;
}
static int same(const struct ar_data_built_route *node,
	const struct ar_data_filter *filter, uint64_t actions,
	const struct ar_data_builder_ops *ops, void *context)
{
	const struct ar_data_filter *old = &node->dispatch.filter;
	if (old->flags != filter->flags ||
		((filter->flags & 1) && old->source != filter->source)) { return 0; }
	if (filter->flags & 2) {
		uint8_t framed = 0;
		if (ops->interfaces.framed(filter->source, &framed, context)) { return 0; }
		if (framed && (old->minimum != filter->minimum || old->maximum != filter->maximum)) { return 0; }
	}
	if ((filter->flags & 4) && old->secondary != filter->secondary) { return 0; }
	if ((filter->flags & 16) && (old->pattern_length != filter->pattern_length ||
		old->offset != filter->offset)) { return 0; }
	return !node->action || node->action->flags == actions;
}
int ar_data_routes_build(struct ar_data_built_route **head,
	const struct ar_data_route_spec *specs, size_t count, uint8_t role,
	const struct ar_data_builder_ops *ops, void *context)
{
	if (!head || (count && !specs) || count > UINT16_MAX || !ops ||
		!ops->interfaces.identity || !ops->interfaces.framed || !ops->node ||
		!ops->action) { return -22; }
	for (size_t i = 0; i < count; ++i) {
		const struct ar_data_route_spec *spec = &specs[i];
		if (spec->disabled || ((spec->flags & 8) && spec->role != role)) { continue; }
		struct ar_data_filter filter = {.flags = spec->flags, .minimum = spec->minimum,
			.maximum = spec->maximum, .offset = spec->offset,
			.pattern = spec->pattern, .pattern_length = spec->pattern_length};
		uint16_t destination = 0;
		int result;
		if (spec->flags & 3) {
			result = resolve(&spec->source, &filter.source, ops, context);
			if (result) { return result; }
		}
		if (spec->flags & 4) {
			result = resolve(&spec->secondary, &filter.secondary, ops, context);
			if (result) { return result; }
		}
		if ((spec->flags & 16) && (uint32_t)spec->offset + spec->pattern_length > 4114) { return -10000; }
		if ((spec->actions & 0x28) == 0x28) { return -10000; }
		if (spec->actions & 0xc8) {
			result = resolve(&spec->destination, &destination, ops, context);
			if (result) { return result; }
		}
		if (spec->pattern_length > sizeof (*head)->pattern ||
			(spec->pattern_length && !spec->pattern)) { return -22; }
		struct ar_data_built_route *node = *head;
		while (node && !same(node, &filter, spec->actions, ops, context)) {
			node = (struct ar_data_built_route *)node->dispatch.next;
		}
		int fresh = !node;
		if (fresh) {
			node = ops->node(context);
			if (!node) { return -9999; }
			memset(node, 0, sizeof *node);
			node->dispatch.filter = filter;
			if (spec->pattern_length) { memcpy(node->pattern, spec->pattern, spec->pattern_length); }
			node->dispatch.filter.pattern = node->pattern;
		}
		if (!node->action) {
			node->action = ops->action(context);
			if (!node->action) { return -9999; }
			memset(node->action, 0, sizeof *node->action);
		}
		node->action->flags = node->dispatch.actions = spec->actions;
		node->action->metadata_override = spec->metadata_override;
		if (spec->actions & 0xc8) { node->action->route.destinations |= UINT64_C(1) << destination; }
		if (fresh) {
			node->dispatch.next = *head ? &(*head)->dispatch : NULL;
			*head = node;
		}
		node->dispatch.worker = (uint8_t)spec->worker;
	}
	return 0;
}
