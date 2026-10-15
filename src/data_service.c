/* SPDX-License-Identifier: MIT */
#include "ar_data_service.h"
#include <string.h>

static int reader_create(struct ar_data_services *services,
	struct ar_data_service_interface *interface,
	const struct ar_data_service_config *config,
	const struct ar_data_service_ops *ops, void *context)
{
	struct ar_data_service_reader *reader = ops->allocate(AR_DATA_SERVICE_READER,
		sizeof *reader, context);
	if (!reader) { return -9999; }
	memset(reader, 0, sizeof *reader);
	reader->interface = interface;
	if (config->read_capacity) {
		reader->ring = ops->allocate(AR_DATA_SERVICE_RING, sizeof *reader->ring, context);
		if (!reader->ring) { ops->release(reader, context); return -9999; }
		reader->ring->bytes = ops->allocate(AR_DATA_SERVICE_BYTES, config->read_capacity, context);
		if (!reader->ring->bytes) {
			ops->release(reader->ring, context); ops->release(reader, context); return -9999;
		}
		reader->ring->capacity = config->read_capacity;
		reader->capacity = config->read_capacity;
		reader->mutex = ops->mutex(context);
		(void)ops->lock(reader->mutex, -1, context);
		reader->ring->read = reader->ring->write = reader->ring->used = 0;
		(void)ops->unlock(reader->mutex, context);
	}
	if (config->read_stack) {
		(void)ops->task(AR_DATA_SERVICE_READ, reader, 18, config->read_stack,
			&reader->task, context);
	}
	reader->next = services->readers;
	services->readers = reader;
	return 0;
}
int ar_data_service_publish(struct ar_data_services *services,
	struct ar_data_service_interface *interface,
	const struct ar_data_service_config *config,
	const struct ar_data_service_ops *ops, void *context)
{
	if (!services || !interface || !config || !ops || !ops->allocate ||
		!ops->release || !ops->mutex || !ops->lock || !ops->unlock ||
		!ops->queue || !ops->task) { return -22; }
	for (struct ar_data_service_interface *i = services->interfaces; i; i = i->next) {
		if (i == interface) { return -22; }
	}
	interface->id = services->next_id++;
	interface->next = services->interfaces;
	services->interfaces = interface;
	if (!interface->type && config->uart_at) {
		void *task = NULL;
		(void)ops->task(AR_DATA_SERVICE_AT, interface, 3, 4096, &task, context);
		return 0;
	}
	int result = reader_create(services, interface, config, ops, context);
	if (config->write_stack) {
		interface->writer_queue = ops->queue(config->write_capacity, 32, context);
		if (!interface->writer_queue) { return -9999; }
		interface->writer_mutex = ops->mutex(context);
		if (ops->task(AR_DATA_SERVICE_WRITE, interface, 19, config->write_stack,
			&interface->writer_task, context)) { return -9998; }
	}
	interface->read_enabled = interface->write_enabled = 1;
	return result;
}
