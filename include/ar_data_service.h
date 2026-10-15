/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_SERVICE_H
#define AR_DATA_SERVICE_H
#include <stddef.h>
#include <stdint.h>

/* Native service views, not original layouts. An interface has already passed
 * admission and type-specific initialization before publication here. */
struct ar_data_service_interface {
	uint16_t id;
	uint32_t type;
	uint8_t write_enabled, read_enabled;
	void *writer_queue, *writer_mutex, *writer_task;
	struct ar_data_service_interface *next;
};
struct ar_data_service_ring {
	uint8_t *bytes;
	uint32_t capacity, read, write, used;
};
struct ar_data_service_reader {
	struct ar_data_service_interface *interface;
	struct ar_data_service_ring *ring;
	uint32_t capacity, pending, consumed;
	uint8_t busy;
	void *mutex, *task;
	struct ar_data_service_reader *next;
};
struct ar_data_services {
	struct ar_data_service_interface *interfaces;
	struct ar_data_service_reader *readers;
	uint16_t next_id;
};
struct ar_data_service_config {
	uint16_t read_capacity, read_stack, write_capacity, write_stack;
	uint8_t uart_at;
};
enum ar_data_service_allocation { AR_DATA_SERVICE_READER, AR_DATA_SERVICE_RING, AR_DATA_SERVICE_BYTES };
enum ar_data_service_task { AR_DATA_SERVICE_AT, AR_DATA_SERVICE_READ, AR_DATA_SERVICE_WRITE };
struct ar_data_service_ops {
	/* Allocate native storage; size is a native size, never an original ABI size.
	 * Storage must be suitably aligned and disjoint from existing live objects. */
	void *(*allocate)(enum ar_data_service_allocation, size_t size, void *context);
	void (*release)(void *, void *context);
	void *(*mutex)(void *context);
	int (*lock)(void *mutex, int32_t timeout, void *context);
	int (*unlock)(void *mutex, void *context);
	/* message_bytes records the original 32-byte request. The adapter must
	 * provision its native queue representation independently of that ABI. */
	void *(*queue)(uint16_t capacity, uint32_t message_bytes, void *context);
	int (*task)(enum ar_data_service_task, void *argument, uint32_t priority,
		uint32_t stack, void **handle, void *context);
};

/* Source-only publication and reader/writer construction. The caller owns the
 * interface, serializes the registry and prevents tasks running during setup.
 * Assigns a wrapping ID and prepends the interface before resource allocation.
 * UART AT launches its task and returns 0 without reader/writer initialization.
 * Reader allocation failure cleans that reader's partial storage, but leaves
 * the interface published and continues writer setup. Writer failures retain
 * already created resources. Read-task/AT-task and mutex/lock errors are ignored
 * as originally; write-task failure is -9998, allocation/queue failure -9999.
 * The normal tail enables both directions even after a reader allocation error.
 * No rollback or general teardown is inferred. Caller retains all published
 * and allocated state, including failed constructions, for eventual teardown.
 * -22 guards reject invalid arguments or already-published interfaces atomically.
 * No physical interface setup, task execution, parser, queue transfer or runtime
 * service integration is supplied by this helper. */
int ar_data_service_publish(struct ar_data_services *,
	struct ar_data_service_interface *, const struct ar_data_service_config *,
	const struct ar_data_service_ops *, void *context);
#endif
