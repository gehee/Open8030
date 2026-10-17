/* SPDX-License-Identifier: MIT */
#ifndef AR_SOCKET_RX_H
#define AR_SOCKET_RX_H
#include "ar_frame.h"

/* Mode-0 sockets. Storage belongs to the endpoint until SDIO/MAC copies it.
 * This is a polling implementation; no calls may run concurrently. */
#define AR_SOCKET_RX_PORTS 4u
#define AR_SOCKET_RX_CAPACITY 56320u
#define AR_SOCKET_TX_CAPACITY 4096u
#define AR_SOCKET_TX_PENDING 2u
struct ar_socket_write {
	uint8_t bytes[AR_MAX_PAYLOAD - 8];
	uint64_t position;
	uint32_t length, copied;
	uint8_t active, cancelled;
};
struct ar_socket_rx_port {
	uint32_t capacity, head, used, msgid;
	uint32_t offered, accepted, delivered, dropped;
	uint32_t flags;
	uint32_t tx_capacity, tx_head, tx_used;
	uint32_t pending_head, pending_count;
	uint64_t position;
	uint32_t enqueued, completed, submitted, rejected;
	uint32_t closing, close_msgid;
	/* Queue metadata above can be reset without clearing payload storage in
	 * the time-sensitive polling loop. Every newly admitted write initializes
	 * its own pending metadata; empty queues expose none of the old bytes. */
	uint8_t bytes[AR_SOCKET_RX_CAPACITY];
	uint8_t tx_bytes[AR_SOCKET_TX_CAPACITY];
	struct ar_socket_write pending[AR_SOCKET_TX_PENDING];
};
struct ar_socket_close_info {
	uint32_t requested, cleaned, replied, cancelled;
	uint32_t discarded_writes, discarded_rx_bytes, discarded_tx_bytes, pending_mask;
};
struct ar_socket_rx {
	struct ar_socket_rx_port ports[AR_SOCKET_RX_PORTS];
	uint32_t next_port, next_tx_port, next_completion;
	uint8_t error_payload[16];
	struct ar_socket_close_info close_info;
};
void ar_socket_rx_init(struct ar_socket_rx *sockets);
/* 0: unhandled; 1: immediate response; 2: close deferred (do not send response).
 * Other socket commands return an immediate -95. */
int ar_socket_rx_request(struct ar_socket_rx *sockets, const struct ar_frame *request,
	struct ar_frame *response);
size_t ar_socket_rx_feed(struct ar_socket_rx *sockets, unsigned slot, unsigned mac_port,
	const uint8_t *data, size_t length);
/* Peek copies into caller scratch; commit only after a successful SDIO copy. */
int ar_socket_rx_peek(struct ar_socket_rx *sockets, uint8_t *scratch, size_t capacity,
	struct ar_frame *frame);
void ar_socket_rx_commit(struct ar_socket_rx *sockets, const struct ar_frame *frame);
/* Progress blocking stock writes without blocking cooperative RF/SDIO polling.
 * Completion means copied into the transport, not received over the air. */
void ar_socket_tx_service(struct ar_socket_rx *sockets);
int ar_socket_tx_completion(struct ar_socket_rx *sockets, uint8_t scratch[8],
	struct ar_frame *frame);
void ar_socket_tx_complete(struct ar_socket_rx *sockets, const struct ar_frame *frame);
/* Cooperative copy-mode shutdown. An active write's completion must reach SDIO
 * before cleanup; unstarted writes are discarded. A close reply is retryable,
 * and the port cannot be reused until SDIO owns its immutable copy. */
void ar_socket_close_service(struct ar_socket_rx *sockets);
int ar_socket_close_peek(struct ar_socket_rx *sockets, struct ar_frame *frame);
void ar_socket_close_commit(struct ar_socket_rx *sockets, const struct ar_frame *frame);
/* MAC port is host port+1. Retry a peek without consuming bytes; commit only
 * after RF owns an immutable copy. These calls cannot interleave with close. */
size_t ar_socket_tx_peek(struct ar_socket_rx *sockets, uint8_t *scratch,
	size_t capacity, unsigned *mac_port);
void ar_socket_tx_commit(struct ar_socket_rx *sockets, unsigned mac_port, size_t length);
void ar_socket_rx_totals(uint32_t *offered, uint32_t *dropped);
#endif
