/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_PREFLIGHT_H
#define AR_DATA_PREFLIGHT_H
#include "ar_data_builder.h"

/* Native view of fields used by configuration preflight, not an original
 * interface record. Selector is 32-bit here: matching requires equality with
 * the route identity's 16-bit selector, without truncating the interface. */
struct ar_data_preflight_interface {
	uint32_t type, selector;
	uint16_t port, references;
	uint8_t disabled, at, console;
};

/* Source-only policy on caller-owned mutable tables, before role admission and
 * route construction. UART type0 sets console/AT flags but never clears them;
 * uart_at[0..2] corresponds to ports1..3. Counts accumulate modulo65536.
 * Every record is visited, including disabled and role-ineligible records.
 * Conflicts disable a route; otherwise found endpoints gain references. The
 * original checks destination AT but rechecks source console, so destination
 * console alone does not prevent counting. A missing destination still counts
 * a found source. Interfaces with no refs/AT/console become disabled; other
 * existing disabled flags remain unchanged. No field is reset on entry.
 * Return 0 on completion, -22 for native pointer/count guards or the original's
 * unsafe missing-source/non-AT-destination combination. Initial guards mutate
 * nothing; a guard during traversal preserves preceding mutations and skips
 * final interface disabling. Real UART configuration/conversion, mode-table
 * selection, construction and runtime ownership are separate. */
int ar_data_preflight(struct ar_data_preflight_interface *, size_t interface_count,
	struct ar_data_route_spec *, size_t route_count, uint32_t console_port,
	const uint8_t uart_at[3]);
#endif
