/* SPDX-License-Identifier: MIT */
#ifndef AR_DATA_INGRESS_H
#define AR_DATA_INGRESS_H
#include <stddef.h>
#include <stdint.h>

/* Source-only host callbacks for event-interface ports 0/1/2 (core setters
 * 0x020000c9/ca/cb). These adapters do not provision a service or implement the
 * common control handler. Parser ownership and serialization belong to caller. */
struct ar_data_ingress_ops {
	void *(*find)(unsigned type, unsigned port, void *context);
	int (*feed)(void *parser, const uint8_t *data, size_t length, void *context);
	int (*control)(const uint8_t *request, size_t length, void *context);
};

/* Subtype 6 returns zero even for missing interfaces, rejected frame headers,
 * oversized framed messages or parser failure. Success is not delivery.
 * Other subtypes delegate to control. Native extent/adapter guards and wrapped
 * embedded lengths return -22; the original callbacks lack these checks.
 * Request bytes are borrowed only for this call; feed must consume/copy them. */
int ar_data_ingress(unsigned port, const uint8_t *request, size_t length,
	const struct ar_data_ingress_ops *, void *context);
#endif
