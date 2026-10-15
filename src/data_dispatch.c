/* SPDX-License-Identifier: MIT */
#include "ar_data_dispatch.h"

int ar_data_dispatch(struct ar_data_dispatch_node *routes,
	struct ar_data_packet *packet, const struct ar_data_filter_input *input,
	uint8_t metadata, uint16_t source,
	struct ar_data_dispatch_worker *workers, uint32_t worker_count,
	const uint16_t *worker_map, size_t map_count,
	struct ar_data_dispatch_stats *stats, uint32_t *matches,
	const struct ar_data_dispatch_ops *ops, void *context)
{
	if (!packet || !packet->data || !input || !stats || !matches || !ops ||
		!ops->copy || !ops->retain || !ops->release || !ops->enqueue || !ops->wake ||
		(worker_count && !workers) || (map_count && !worker_map)) { return -22; }
	const uint32_t length = packet->length;
	int grouped = 0, result = 0;
	*matches = 0;
	for (struct ar_data_dispatch_node *node = routes; node; node = node->next) {
		int match = ar_data_filter_match(&node->filter, input, &ops->filter, context);
		if (match < 0) { result = match; goto done; }
		if (!match) { continue; }
		++*matches;
		if ((node->actions & 0x28) && grouped) { continue; }
		unsigned worker = 0;
		if (!(node->actions & 0x20)) {
			if (node->worker >= map_count) { result = -22; goto done; }
			worker = worker_map[node->worker];
		}
		if (worker >= worker_count) {
			++stats->invalid_worker;
			result = -4998;
			goto done;
		}
		struct ar_data_packet *selected = packet;
		if (node->actions & 6) { selected = ops->copy(packet, context); }
		if (selected) {
			ops->retain(selected, context);
			if (ops->enqueue(worker, node, selected, metadata, source, context)) {
				++stats->queue_failures;
				ops->release(selected, context);
			} else {
				--workers[worker].free;
				if (workers[worker].free < workers[worker].low) {
					workers[worker].low = workers[worker].free;
				}
				(void)ops->wake(255, context);
			}
		}
		node->bytes += length;
		++node->packets;
		if (node->actions & 0x28) { grouped = 1; }
	}
	if (!*matches) { ++stats->unmatched; result = -4996; }
done:
	ops->release(packet, context);
	return result;
}
