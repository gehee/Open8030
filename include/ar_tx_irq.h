/* SPDX-License-Identifier: MIT */
#ifndef AR_TX_IRQ_H
#define AR_TX_IRQ_H
#include "ar_interleaver.h"

/* History records pre-encode attempts, not peer ACKs
 * or completed transmissions. Caller serializes state and owns queue delivery. */
struct ar_tx_irq {
	uint64_t timestamp;
	uint32_t busy;
	uint8_t user;
};
struct ar_tx_irq_ops {
	uint64_t (*clock)(void *context);
	uint32_t (*word)(unsigned offset, void *context);
	uint32_t (*read)(uint32_t field, void *context);
	/* Queue 2, flags 0. Failed posts still permit remaining ISR effects. */
	int (*post)(unsigned message, const uint8_t *payload, unsigned length,
		void *context);
	/* Pre-encode: source 19, 12 bytes. End: source 11, NULL payload. */
	void (*notify)(unsigned source, const uint8_t *payload, void *context);
};
/* Native clears the original's three uninitialized pre-encode suffix bytes.
 * The two F70 samples may differ. An invalid second user returns -22 after
 * posting the first sample, but still notifies; no unsafe table access occurs.
 * Callback mutations remain visible. Missing arguments return -22 first. */
int ar_tx_irq_preencode(struct ar_tx_irq *irq, struct ar_interleaver *state,
	const struct ar_tx_irq_ops *ops, void *context);
int ar_tx_irq_end(struct ar_tx_irq *irq, const struct ar_tx_irq_ops *ops,
	void *context);
/* History portion after queue posting and the second hardware-user sample.
 * Only word/read adapters are required. No IRQ/queue state is updated. */
int ar_tx_irq_history(struct ar_interleaver *state, unsigned user,
	const struct ar_tx_irq_ops *ops, void *context);
#endif
