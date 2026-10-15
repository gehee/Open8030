/* SPDX-License-Identifier: MIT */
#include "ar_tx_irq.h"
#include <stddef.h>

static void put32(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		p[i] = (uint8_t)(value >> (i * 8));
}

int ar_tx_irq_history(struct ar_interleaver *state, unsigned user,
	const struct ar_tx_irq_ops *ops, void *context)
{
	if (!state || user >= AR_INTERLEAVER_USERS || !ops || !ops->word || !ops->read)
		return -22;
	struct ar_interleaver_user *u = &state->users[user];
	if (u->active && u->retries) {
		uint32_t interleave = ops->read(((user + 0x2c0) << 18) | 0x401, context);
		unsigned shift = (uint8_t)(u->retries - 1);
		uint32_t history = u->history;
		uint32_t bit = 1;
		if (interleave == 1) {
			bit <<= ops->word(0xf58, context) & 1;
			shift *= 2;
		}
		u->history = history | (bit << (shift & 31));
	}
	return 0;
}

int ar_tx_irq_preencode(struct ar_tx_irq *irq, struct ar_interleaver *state,
	const struct ar_tx_irq_ops *ops, void *context)
{
	if (!irq || !state || !ops || !ops->clock || !ops->word ||
	    !ops->read || !ops->post || !ops->notify)
		return -22;
	uint8_t payload[12] = {0};
	irq->busy = 1;
	uint64_t timestamp = ops->clock(context);
	unsigned user = (ops->word(0xf70, context) >> 12) & 15;
	irq->timestamp = timestamp;
	put32(payload, user);
	irq->user = (uint8_t)user;
	put32(payload + 4, ops->word(0xf5c, context) >> 16);
	payload[8] = (ops->word(0xf58, context) >> 4) & 15;
	(void)ops->post(0x301, payload, sizeof(payload), context);
	user = (ops->word(0xf70, context) >> 12) & 15;
	int result = ar_tx_irq_history(state,user,ops,context);
	ops->notify(19, payload, context);
	return result;
}

int ar_tx_irq_end(struct ar_tx_irq *irq, const struct ar_tx_irq_ops *ops,
	void *context)
{
	if (!irq || !ops || !ops->post || !ops->notify)
		return -22;
	if (irq->busy == 1)
		irq->busy = 0;
	uint8_t payload[4];
	put32(payload, irq->user);
	(void)ops->post(0x309, payload, sizeof(payload), context);
	ops->notify(11, NULL, context);
	return 0;
}
