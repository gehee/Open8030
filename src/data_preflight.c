/* SPDX-License-Identifier: MIT */
#include "ar_data_preflight.h"

static struct ar_data_preflight_interface *find(
	struct ar_data_preflight_interface *interfaces, size_t count,
	const struct ar_data_identity *identity)
{
	for (size_t i = 0; i < count; ++i) {
		if (interfaces[i].type == identity->type && interfaces[i].port == identity->port &&
			interfaces[i].selector == identity->selector) { return &interfaces[i]; }
	}
	return NULL;
}
int ar_data_preflight(struct ar_data_preflight_interface *interfaces, size_t count,
	struct ar_data_route_spec *routes, size_t route_count, uint32_t console_port,
	const uint8_t uart_at[3])
{
	if ((count && !interfaces) || (route_count && !routes) || !uart_at ||
		count > UINT16_MAX || route_count > UINT16_MAX) { return -22; }
	for (size_t i = 0; i < count; ++i) {
		struct ar_data_preflight_interface *interface = &interfaces[i];
		if (interface->type != 0) { continue; }
		if (interface->port == console_port) { interface->console = 1; }
		if (interface->port >= 1 && interface->port <= 3 && uart_at[interface->port - 1]) {
			interface->at = 1;
		}
	}
	for (size_t i = 0; i < route_count; ++i) {
		struct ar_data_route_spec *route = &routes[i];
		struct ar_data_preflight_interface *source = find(interfaces, count, &route->source);
		if (source && (source->at || source->console)) { route->disabled = 1; continue; }
		struct ar_data_preflight_interface *destination = find(interfaces, count, &route->destination);
		if (destination) {
			if (destination->at) { route->disabled = 1; continue; }
			if (!source) { return -22; }
			if (source->console) { route->disabled = 1; continue; }
		}
		if (source) {
			++source->references;
			if (destination) { ++destination->references; }
		}
	}
	for (size_t i = 0; i < count; ++i) {
		struct ar_data_preflight_interface *interface = &interfaces[i];
		if (!interface->at && !interface->console && !interface->references) { interface->disabled = 1; }
	}
	return 0;
}
