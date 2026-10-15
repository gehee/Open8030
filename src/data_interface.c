/* SPDX-License-Identifier: MIT */
#include "ar_data_interface.h"
#include <string.h>

static void profile(struct ar_data_interface *interface,
	enum ar_data_interface_profile kind)
{
	interface->profile = kind;
	interface->framed = 1;
	interface->prefix = kind == AR_DATA_PROFILE_SBUS ? 0x0f : 0xc8;
	interface->action = (struct ar_data_action_profile){1,
		kind == AR_DATA_PROFILE_SBUS ? 1 : 2, 1, 1, &interface->prefix};
}
int ar_data_interface_create(struct ar_data_services *services,
	const struct ar_data_interface_spec *spec, uint8_t role,
	const struct ar_data_interface_ops *ops, void *context)
{
	if (!services || !spec || !ops || !ops->service.allocate ||
		!ops->service.release || !ops->service.mutex || !ops->service.lock ||
		!ops->service.unlock || !ops->service.queue || !ops->service.task ||
		!ops->socket_open) { return -22; }
	if (!spec->type) { return -95; }
	if (spec->role_mask && !((uint32_t)spec->role_mask & (UINT32_C(1) << (role & 31)))) { return 0; }
	for (const struct ar_data_service_interface *i = services->interfaces; i; i = i->next) {
		if (i->type == spec->type && i->port == spec->port &&
			i->selector == (uint16_t)spec->selector) { return -9996; }
	}
	struct ar_data_interface *interface = ops->service.allocate(
		AR_DATA_SERVICE_INTERFACE, sizeof *interface, context);
	if (!interface) { return -9999; }
	memset(interface, 0, sizeof *interface);
	interface->service.type = spec->type;
	interface->service.port = spec->port;
	interface->service.selector = spec->selector;
	interface->framed = spec->framed;
	interface->read_timeout = spec->read_timeout;
	interface->write_timeout = spec->write_timeout;
	switch (spec->type) {
	case 1: interface->writer = AR_DATA_WRITE_KIND1; break;
	case 2:
		interface->socket_handle = spec->socket_handle;
		if (!interface->socket_handle) {
			int32_t handle = ops->socket_open(spec->selector, spec->port,
				spec->port ? 3 : 19, spec->socket_tx_capacity, spec->socket_rx_capacity, context);
			if (handle < 0) { ops->service.release(interface, context); return -9998; }
			interface->socket_handle = ops->service.allocate(AR_DATA_SERVICE_SOCKET_HANDLE,
				sizeof *interface->socket_handle, context);
			if (!interface->socket_handle) { ops->service.release(interface, context); return 0; }
			*interface->socket_handle = handle;
		}
		interface->writer = AR_DATA_WRITE_SOCKET;
		interface->socket_reader = 1;
		if (spec->port == 0) { profile(interface, AR_DATA_PROFILE_SBUS); }
		else if (spec->port == 1) { profile(interface, AR_DATA_PROFILE_C8); }
		break;
	case 3:
		interface->writer = AR_DATA_WRITE_EVENT;
		if (spec->port == 0) { profile(interface, AR_DATA_PROFILE_C8); }
		break;
	case 4: interface->writer = AR_DATA_WRITE_KIND4; break;
	case 7: interface->writer = AR_DATA_WRITE_KIND7; break;
	default: break;
	}
	return ar_data_service_publish(services, &interface->service, &spec->service,
		&ops->service, context);
}
