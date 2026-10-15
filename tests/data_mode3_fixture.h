/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_MODE3_FIXTURE_H
#define AR_DATA_MODE3_FIXTURE_H
#include "ar_data_builder.h"
/* Semantic matched mode-3 routes for tests. UART/console preflight and actual
 * interface provisioning must precede runtime construction; neither is supplied
 * by this fixture. These declarations are not an original binary table. */
static const struct ar_data_route_spec mode3_routes[] = {
	{.flags = 9, .source = {3, 0, 0}, .destination = {2, 1, 0}, .role = 1, .worker = 0, .actions = 8},
	{.flags = 9, .source = {2, 1, 0}, .destination = {3, 0, 0}, .role = 1, .worker = 0, .actions = 8},
	{.flags = 9, .source = {2, 1, 0}, .destination = {0, 2, 0}, .role = 0, .worker = 1, .actions = 8},
	{.flags = 9, .source = {0, 2, 0}, .destination = {2, 1, 0}, .role = 0, .worker = 1, .actions = 8},
	{.flags = 1, .source = {0, 1, 0}, .destination = {2, 0, 0}, .worker = 0, .actions = 8},
	{.flags = 1, .source = {2, 0, 0}, .destination = {0, 1, 0}, .worker = 1, .actions = 8},
};
#endif
