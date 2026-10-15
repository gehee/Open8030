/* SPDX-License-Identifier: MIT */
#include "ar_data_preflight.h"
#include "data_mode3_fixture.h"
#include <assert.h>
#include <string.h>

static const uint8_t off[3];
static struct ar_data_route_spec route(unsigned source, unsigned destination)
{
	return (struct ar_data_route_spec){.source = {2, source, 0},
		.destination = {2, destination, 0}};
}
static void boundaries(void)
{
	struct ar_data_preflight_interface i[3] = {
		{.type = 2, .port = 0}, {.type = 2, .port = 1, .console = 255},
		{.type = 2, .port = 2}};
	struct ar_data_route_spec r[2] = {route(0, 1), route(0, 9)};
	r[0].disabled = 255; r[0].flags = 8; r[0].role = 99;
	assert(!ar_data_preflight(i, 3, r, 2, 0, off));
	/* Disabled/role-ineligible routes still count; destination console is ignored. */
	assert(i[0].references == 2 && i[1].references == 1 && i[2].disabled == 1);
	assert(r[0].disabled == 255 && !r[1].disabled && i[1].console == 255);
	i[1].at = 2;
	assert(!ar_data_preflight(i, 3, r, 2, 0, off));
	assert(r[0].disabled == 1 && i[0].references == 3 && i[1].references == 1);
	r[0] = route(1, 0); i[1].at = 0;
	assert(!ar_data_preflight(i, 3, r, 1, 0, off));
	assert(r[0].disabled == 1); /* Source console does suppress the route. */

	i[0] = (struct ar_data_preflight_interface){.type = 2, .references = 65535, .disabled = 255};
	r[0] = route(0, 0);
	assert(!ar_data_preflight(i, 1, r, 1, 0, off));
	assert(i[0].references == 1 && i[0].disabled == 255);
	i[0].references = 65534; i[0].disabled = 0;
	assert(!ar_data_preflight(i, 1, r, 1, 0, off));
	assert(!i[0].references && i[0].disabled == 1);

	/* First duplicate wins; a high selector must not be truncated to 16 bits. */
	i[0] = (struct ar_data_preflight_interface){.type = 2, .selector = 65536};
	i[1] = i[2] = (struct ar_data_preflight_interface){.type = 2};
	assert(!ar_data_preflight(i, 3, r, 1, 0, off));
	assert(i[0].disabled && i[1].references == 2 && i[2].disabled);

	/* A traversal guard preserves preceding effects and skips the final pass. */
	i[0] = (struct ar_data_preflight_interface){.type = 2};
	i[1] = (struct ar_data_preflight_interface){.type = 2, .port = 1};
	i[2] = (struct ar_data_preflight_interface){.type = 0, .port = 3};
	r[0] = route(0, 9); r[1] = route(9, 1);
	assert(ar_data_preflight(i, 3, r, 2, 0, off) == -22);
	assert(i[0].references == 1 && !i[1].references && !i[2].disabled);
	i[1].at = 1;
	assert(!ar_data_preflight(i, 3, r, 2, 0, off));
	assert(r[1].disabled && i[2].disabled && i[0].references == 2);
	r[0] = route(8, 9);
	assert(!ar_data_preflight(i, 3, r, 1, 0, off));

	struct ar_data_preflight_interface before[3]; memcpy(before, i, sizeof i);
	struct ar_data_route_spec saved = r[0];
	assert(ar_data_preflight(i, 3, r, 1, 3, NULL) == -22);
	assert(ar_data_preflight(i, 65536, r, 1, 3, off) == -22);
	assert(ar_data_preflight(i, 3, r, 65536, 3, off) == -22);
	assert(ar_data_preflight(NULL, 1, r, 1, 3, off) == -22);
	assert(ar_data_preflight(i, 3, NULL, 1, 3, off) == -22);
	assert(!memcmp(before, i, sizeof i) && !memcmp(&saved, r, sizeof saved));
	assert(!ar_data_preflight(NULL, 0, NULL, 0, 0, off));

	i[0] = (struct ar_data_preflight_interface){.port = 0};
	i[1] = (struct ar_data_preflight_interface){.port = 3};
	i[2] = (struct ar_data_preflight_interface){.port = 65535};
	const uint8_t at[3] = {0, 0, 255};
	assert(!ar_data_preflight(i, 3, NULL, 0, 65535, at));
	assert(i[0].disabled && i[1].at == 1 && i[2].console == 1);
	assert(!ar_data_preflight(i, 3, NULL, 0, 0, off));
	assert(i[0].disabled && i[0].console == 1 && i[1].at == 1 && i[2].console == 1);
}
struct graph {
	struct ar_data_preflight_interface interfaces[5];
	struct ar_data_built_route nodes[6];
	struct ar_data_worker_action actions[6];
	unsigned nodes_used, actions_used;
};
static int lookup(const struct ar_data_identity *key, uint16_t *id, void *context)
{
	struct graph *g = context;
	/* Test registry uses table indices; these are not original runtime IDs. */
	for (unsigned n = 0; n < 5; ++n) {
		const struct ar_data_preflight_interface *i = &g->interfaces[n];
		if (!i->disabled && i->type == key->type && i->port == key->port &&
			i->selector == key->selector) { *id = n; return 0; }
	}
	return -9997;
}
static int framed(uint16_t id, uint8_t *enabled, void *context)
{
	(void)id; (void)enabled; (void)context;
	assert(0 && "mode-3 predicates do not request framing lookup"); return -1;
}
static struct ar_data_built_route *node(void *context)
{
	struct graph *g = context; assert(g->nodes_used < 6); return &g->nodes[g->nodes_used++];
}
static struct ar_data_worker_action *action(void *context)
{
	struct graph *g = context; assert(g->actions_used < 6); return &g->actions[g->actions_used++];
}
static void composition(void)
{
	for (unsigned console = 0; console < 5; ++console) {
		for (unsigned mask = 0; mask < 8; ++mask) {
			for (unsigned role = 0; role < 3; ++role) {
				struct graph g = {.interfaces = {{.port = 2}, {.port = 1},
					{.type = 3}, {.type = 2}, {.type = 2, .port = 1}}};
				struct ar_data_route_spec routes[6]; memcpy(routes, mode3_routes, sizeof routes);
				uint8_t at[3] = {mask & 1, mask & 2, mask & 4};
				assert(!ar_data_preflight(g.interfaces, 5, routes, 6, console, at));
				struct ar_data_built_route *head = NULL;
				const struct ar_data_builder_ops ops = {{lookup, framed}, node, action};
				assert(!ar_data_routes_build(&head, routes, 6, role, &ops, &g));
				unsigned expected[5][5] = {{0}}, seen[5][5] = {{0}}, count = 0;
				if (role == 1) { expected[2][4] = expected[4][2] = 1; }
				if (role == 0 && !(mask & 2)) {
					expected[4][0] = 1;
					if (console != 2) { expected[0][4] = 1; }
				}
				if (!(mask & 1)) {
					expected[3][1] = 1;
					if (console != 1) { expected[1][3] = 1; }
				}
				for (struct ar_data_built_route *n = head; n;
					n = (struct ar_data_built_route *)n->dispatch.next) {
					assert(n->dispatch.filter.source < 5 && n->action);
					for (unsigned d = 0; d < 5; ++d) {
						if (n->action->route.destinations & (UINT64_C(1) << d)) {
							seen[n->dispatch.filter.source][d]++; count++;
						}
					}
				}
				assert(!memcmp(seen, expected, sizeof seen));
				assert(g.nodes_used == count && g.actions_used == count);
			}
		}
	}
}
int main(void) { boundaries(); composition(); return 0; }
