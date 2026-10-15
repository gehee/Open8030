/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_INTERFACE_H
#define AR_DATA_INTERFACE_H
#include "ar_data_service.h"
#include "ar_data_action.h"

/* Profile and callback selectors are native policy, not original function
 * addresses or binary framing records. Only C8 parsing is implemented so far. */
enum ar_data_interface_profile { AR_DATA_PROFILE_NONE, AR_DATA_PROFILE_SBUS, AR_DATA_PROFILE_C8 };
enum ar_data_interface_writer {
	AR_DATA_WRITE_NONE, AR_DATA_WRITE_KIND1, AR_DATA_WRITE_SOCKET,
	AR_DATA_WRITE_EVENT, AR_DATA_WRITE_KIND4, AR_DATA_WRITE_KIND7
};
struct ar_data_interface {
	struct ar_data_service_interface service;
	uint32_t read_timeout, write_timeout;
	uint8_t framed, prefix;
	enum ar_data_interface_profile profile;
	enum ar_data_interface_writer writer;
	uint8_t socket_reader;
	int32_t *socket_handle;
	struct ar_data_action_profile action;
};
struct ar_data_interface_spec {
	uint32_t type, selector;
	uint16_t port;
	uint8_t role_mask, framed;
	uint32_t read_timeout, write_timeout, socket_tx_capacity, socket_rx_capacity;
	int32_t *socket_handle;
	struct ar_data_service_config service;
};
struct ar_data_interface_ops {
	struct ar_data_service_ops service;
	int32_t (*socket_open)(uint32_t selector, uint16_t port, uint32_t flags,
		uint32_t tx_capacity, uint32_t rx_capacity, void *context);
};
/* Source-only non-UART constructor composed with service publication. The caller
 * has applied preflight and skips disabled records. Type0 needs physical UART
 * setup and is explicitly unsupported (-95); that guard performs no effects.
 * Role mask uses role modulo32; zero mask admits all roles. Duplicate lookup
 * compares the configured selector's low16 with existing full32 selectors;
 * newly created objects retain all32 bits. Duplicates return -9996.
 * Allocation failure is -9999, socket-open failure -9998. If socket open succeeds
 * but handle storage allocation fails, free only the interface and return0:
 * the original neither publishes the interface nor closes that open stream.
 * Supplied handle storage is borrowed and bypasses opening/allocation.
 * Successful type setup invokes service publication, retaining its partial
 * state/errors. Caller owns all successful allocations and borrowed storage,
 * prevents concurrent mutation/task execution, and supplies actual operations
 * for the selected writer/read profile; selectors alone do not implement them.
 * Native null/callback guards return -22. No binary table parser or UART/SBUS
 * signaling, generic profile parser, backend integration or teardown is provided. */
int ar_data_interface_create(struct ar_data_services *,
	const struct ar_data_interface_spec *, uint8_t role,
	const struct ar_data_interface_ops *, void *context);
#endif
