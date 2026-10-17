/* SPDX-License-Identifier: MIT */
#include "ar_dev_mcs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_dev_mcs_config config(void)
{
	return (struct ar_dev_mcs_config){
		.enabled = 1, .automatic = 1, .initial_mcs = 1, .count = 3,
		.max_wait_ms = 5000,
		.rows = {{1,0,4,2,40,30,10,5}, {5,0,4,2,100,70,10,5}, {12,0,4,2,200,150,10,5}},
	};
}

static unsigned samples(struct ar_dev_mcs *ctx, uint32_t *now, unsigned count, unsigned snr, unsigned errors)
{
	unsigned actions = 0;
	while (count--) { actions |= ar_dev_mcs_sample(ctx, ++*now, snr, errors); }
	return actions;
}

static void start(struct ar_dev_mcs *ctx, struct ar_dev_mcs_config *cfg, uint32_t *now)
{
	assert(ar_dev_mcs_init(ctx, cfg) == 0);
	assert(ar_dev_mcs_observe(ctx, cfg->initial_mcs) == 0);
	assert(ar_dev_mcs_connect(ctx, 1) == 0);
	*now = 0;
	assert(samples(ctx, now, 7, 500, 0) == 0);
	assert(!ctx->started && !ctx->requests);
	assert(samples(ctx, now, 1, 500, 0) == AR_DEV_MCS_APPLY_BUFFER);
	assert(ctx->started && ctx->requests == 1 && ctx->acknowledgements == 1 && !ctx->pending);
}

int main(void)
{
	struct ar_dev_mcs ctx, before;
	struct ar_dev_mcs_config cfg = config();
	uint32_t now;
	memset(&ctx, 0xa5, sizeof ctx); before = ctx;
	cfg.count = 11;
	assert(ar_dev_mcs_init(&ctx, &cfg) == -22 && !memcmp(&ctx, &before, sizeof ctx));
	cfg = config(); cfg.rows[1].mcs = 1;
	assert(ar_dev_mcs_init(&ctx, &cfg) == -22 && !memcmp(&ctx, &before, sizeof ctx));
	cfg = config(); cfg.policy = 2;
	assert(ar_dev_mcs_init(&ctx, &cfg) == -22);
	cfg = config(); cfg.initial_mcs = 26;
	assert(ar_dev_mcs_init(&ctx, &cfg) == -22);
	assert(ar_dev_mcs_init(NULL, &cfg) == -22);
	assert(ar_dev_mcs_init(&ctx, NULL) == -22);
	uint8_t wire[AR_DEV_MCS_CONFIG_BYTES] = {3,1,2,0,100,0,2,0,0x88,0x13,1,2};
	wire[16] = 1; wire[28] = 5; wire[32] = 0x34; wire[33] = 0x12;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == 0);
	assert(cfg.enabled && cfg.automatic && cfg.hold_ms == 100 && cfg.delay_samples == 2 && cfg.max_wait_ms == 5000);
	assert(cfg.rows[1].mcs == 5 && cfg.rows[1].snr_up == 0x1234);
	struct ar_dev_mcs_config cfg_before = cfg;
	wire[40] = 1; /* Nonzero unused row. */
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == -22 && !memcmp(&cfg, &cfg_before, sizeof cfg));
	wire[40] = 0; wire[28] = 16; /* Above internal 15 (MCS 13). */
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == -22);
	wire[28] = 5; wire[11] = 4;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == -22);
	wire[11] = 2; wire[10] = 2;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == -22);
	wire[10] = 1;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire - 1) == -22);
	/* Open8030: rows to internal 15 (MCS 13); dist_calc enable and offset
	 * (0..100 raw units) ride in wire[10] and wire[12]. */
	wire[28] = 15;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == 0 && cfg.rows[1].mcs == 15);
	wire[28] = 5; wire[12] = 59;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == 0 && cfg.distance_enabled == 1 && cfg.distance_offset == 59);
	wire[12] = 100;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == 0 && cfg.distance_offset == 100);
	cfg_before = cfg; wire[12] = 101;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == -22 && !memcmp(&cfg, &cfg_before, sizeof cfg));
	wire[12] = 0; wire[13] = 1;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == -22);
	wire[13] = 0;
	wire[10] = 0;
	assert(ar_dev_mcs_decode_config(&cfg, wire, sizeof wire) == 0 && !cfg.distance_enabled && !cfg.distance_offset);
	wire[10] = 1;

	/* Open8030 back-off: a down-step forced by errors blocks the row it left
	 * for 30 s, doubling each time (up to 16x); a minute spent on a row
	 * clears that row's level. */
	{
		struct ar_dev_mcs b; struct ar_dev_mcs_config bc = config(); uint32_t t;
		start(&b, &bc, &t);
		samples(&b, &t, 1, 500, 0); samples(&b, &t, 11, 500, 0);
		assert(b.current == 1 && b.pending);
		assert(ar_dev_mcs_observe(&b, 5) == AR_DEV_MCS_APPLY_BUFFER && !b.pending);
		unsigned guard = 0;
		while (b.current == 1 && guard++ < 50) { samples(&b, &t, 1, 500, 1); }
		assert(b.current == 0 && b.backoffs == 1 && b.backoff_level[1] == 1);
		uint32_t blocked = b.blocked_until[1];
		assert(blocked > t && blocked - t <= 30000 && blocked - t >= 29000);
		assert(ar_dev_mcs_observe(&b, 1) == AR_DEV_MCS_APPLY_BUFFER);
		/* Clean and strong, but row 1 is blocked: no promotion for 30 s. */
		samples(&b, &t, 200, 500, 0);
		assert(b.current == 0 && b.requests == 3);
		/* After the block: promoted again. */
		t = blocked + 1; samples(&b, &t, 30, 500, 0);
		assert(b.current == 1 && b.pending);
		assert(ar_dev_mcs_observe(&b, 5) == AR_DEV_MCS_APPLY_BUFFER);
		guard = 0;
		while (b.current == 1 && guard++ < 50) { samples(&b, &t, 1, 500, 1); }
		assert(b.current == 0 && b.backoffs == 2 && b.backoff_level[1] == 2);
		assert(b.blocked_until[1] - t <= 60000 && b.blocked_until[1] - t >= 59000);
		/* A minute on row 0 clears row 0's own level only (row 1 keeps its). */
		assert(ar_dev_mcs_observe(&b, 1) == AR_DEV_MCS_APPLY_BUFFER);
		b.backoff_level[0] = 3; t += 61000; samples(&b, &t, 10, 500, 0);
		assert(b.backoff_level[0] == 0 && b.backoff_level[1] == 2);
	}

	/* Explicit config -1 becomes internal MCS1: high SNR must not bypass it. */
	cfg = config(); start(&ctx, &cfg, &now);
	assert(ctx.current == 0);
	samples(&ctx, &now, 1, 500, 0); /* direction begins at 9 */
	samples(&ctx, &now, 10, 500, 0);
	assert(ctx.current == 0 && ctx.requests == 1); /* strict > up_keep */
	samples(&ctx, &now, 1, 500, 0);
	assert(ctx.current == 1 && ctx.pending && ctx.requests == 2);
	assert(ar_dev_mcs_observe(&ctx, 1) == 0 && ctx.pending);
	samples(&ctx, &now, 100, 500, 0);
	assert(ctx.current == 1 && ctx.requests == 2); /* no policy while pending */
	assert(ar_dev_mcs_observe(&ctx, 5) == AR_DEV_MCS_APPLY_BUFFER);
	assert(!ctx.pending && ctx.acknowledgements == 2);
	assert(ar_dev_mcs_observe(&ctx, 5) == 0); /* repeated observation */
	assert(ar_dev_mcs_observe(&ctx, 12) == AR_DEV_MCS_APPLY_BUFFER);
	assert(ctx.current == 2 && ctx.previous == 1 && ctx.unsolicited == 1);
	assert(ar_dev_mcs_observe(&ctx, 24) == 0 && ctx.current == 2);
	assert(ar_dev_mcs_observe(&ctx, 25) == 0 && ctx.observed == 24);

	/* Request hold applies even to consecutive-error downgrade. */
	cfg = config(); cfg.initial_mcs = 5; cfg.hold_ms = 20;
	start(&ctx, &cfg, &now);
	samples(&ctx, &now, 19, 500, 1);
	assert(ctx.current == 1 && ctx.requests == 1);
	samples(&ctx, &now, 1, 500, 1);
	assert(ctx.current == 0 && ctx.pending && ctx.recent_bad == 8);

	/* Two recent error samples force immediate down, despite high SNR. */
	cfg = config(); cfg.initial_mcs = 5; start(&ctx, &cfg, &now);
	samples(&ctx, &now, 1, 500, 1);
	assert(ctx.current == 1 && ctx.direction == 0);
	samples(&ctx, &now, 1, 500, 1);
	assert(ctx.current == 0 && ctx.pending && ctx.recent_bad == 2);

	/* Low SNR with no LDPC errors downgrades at >= down_keep. */
	cfg = config(); cfg.initial_mcs = 5; start(&ctx, &cfg, &now);
	samples(&ctx, &now, 8, 0, 0);
	assert(ctx.current == 1 && ctx.direction == 2 && ctx.average_snr == 0);
	uint32_t down_since = ctx.direction_ms;
	while (now < down_since + 4) { samples(&ctx, &now, 1, 0, 0); }
	assert(ctx.current == 1);
	samples(&ctx, &now, 1, 0, 0);
	assert(ctx.current == 0 && ctx.pending);

	/* A neutral error count cannot promote solely because SNR is high. */
	cfg = config(); start(&ctx, &cfg, &now);
	for (unsigned i = 0; i < 80; i++) { samples(&ctx, &now, 1, 500, i % 8 == 0); }
	assert(ctx.current == 0 && ctx.requests == 1 && ctx.bad_samples == 1);
	/* The explicit SNR policy ignores those errors. */
	cfg.policy = 1; start(&ctx, &cfg, &now);
	samples(&ctx, &now, 12, 500, 1);
	assert(ctx.current == 1 && ctx.pending);

	/* Automatic initial selection uses minimum SNR, and inclusive down threshold. */
	cfg = config(); cfg.initial_mcs = 25;
	assert(ar_dev_mcs_init(&ctx, &cfg) == 0); ar_dev_mcs_connect(&ctx, 1); now = 0;
	samples(&ctx, &now, 7, 500, 0); samples(&ctx, &now, 1, 149, 0);
	assert(ctx.current == 1 && ctx.pending);
	assert(ar_dev_mcs_init(&ctx, &cfg) == 0); ar_dev_mcs_connect(&ctx, 1); now = 0;
	samples(&ctx, &now, 7, 500, 0); samples(&ctx, &now, 1, 150, 0);
	assert(ctx.current == 2 && ctx.pending);

	/* Startup delay counts ready samples, not elapsed milliseconds. */
	cfg = config(); cfg.delay_samples = 2;
	assert(ar_dev_mcs_init(&ctx, &cfg) == 0); ar_dev_mcs_connect(&ctx, 1); now = 0;
	samples(&ctx, &now, 9, 500, 0); assert(!ctx.started && ctx.delayed == 2);
	samples(&ctx, &now, 1, 500, 0); assert(ctx.started && ctx.pending);

	/* Manual mode sends its initial request but does not adapt afterward. */
	cfg = config(); cfg.automatic = 0; start(&ctx, &cfg, &now);
	samples(&ctx, &now, 100, 500, 0); assert(ctx.requests == 1 && ctx.current == 0);
	cfg.enabled = 0; assert(ar_dev_mcs_init(&ctx, &cfg) == 0);
	ar_dev_mcs_connect(&ctx, 1); samples(&ctx, &now, 20, 500, 0); assert(!ctx.started);

	/* A >200-ms gap needs eight fresh samples. A disconnected receiver
	 * never requests MCS; reconnect also requires a new sample window. */
	cfg = config(); start(&ctx, &cfg, &now);
	now += 200; samples(&ctx, &now, 7, 500, 0);
	assert(!ctx.ready && ctx.requests == 1);
	samples(&ctx, &now, 1, 500, 0); assert(ctx.ready);
	ar_dev_mcs_connect(&ctx, 0); samples(&ctx, &now, 40, 500, 0);
	assert(!ctx.started && ctx.requests == 1);
	ar_dev_mcs_connect(&ctx, 1); samples(&ctx, &now, 7, 500, 0); assert(!ctx.started);
	samples(&ctx, &now, 1, 500, 0); assert(ctx.started && ctx.requests == 2);

	/* Millisecond wrap preserves sample freshness and promotion delay. */
	cfg = config(); start(&ctx, &cfg, &now);
	ctx.sample_ms = UINT32_MAX - 5; ctx.request_ms = UINT32_MAX - 5; now = UINT32_MAX - 5;
	samples(&ctx, &now, 12, 500, 0); assert(ctx.current == 1 && ctx.pending);

	/* Initial request differs from the observed header: advertise four
	 * preencodes, cancel on the fifth; re-advertise on the tenth. */
	cfg = config(); assert(ar_dev_mcs_init(&ctx, &cfg) == 0);
	ar_dev_mcs_connect(&ctx, 1); now = 0; samples(&ctx, &now, 8, 500, 0);
	assert(ctx.pending && ctx.requests == 1);
	assert(ar_dev_mcs_preencode(&ctx, now, 8) == 0 && !ctx.preencodes);
	assert(ar_dev_mcs_interleaver(&ctx) == 0);
	assert(ar_dev_mcs_preencode(&ctx, now, 0) == AR_DEV_MCS_SYNC_ON);
	const uint8_t command[] = {0xf3,5,0,0xc1,0,0};
	assert(!memcmp(ctx.fch + 1, command, sizeof command));
	assert(ar_dev_mcs_interleaver(&ctx) == AR_DEV_MCS_WRITE_FCH);
	for (unsigned i = 0; i < 3; i++) { assert(ar_dev_mcs_preencode(&ctx, ++now, 0) == 0); }
	assert(ar_dev_mcs_preencode(&ctx, ++now, 0) == (AR_DEV_MCS_WRITE_FCH | AR_DEV_MCS_SYNC_OFF));
	assert(ctx.fch[1] == 0xf0 && !memcmp(ctx.fch + 2, command + 1, 5));
	assert(ar_dev_mcs_interleaver(&ctx) == 0 && ctx.pending);
	for (unsigned i = 0; i < 4; i++) { assert(ar_dev_mcs_preencode(&ctx, ++now, 0) == 0); }
	assert(ar_dev_mcs_preencode(&ctx, ++now, 0) == AR_DEV_MCS_SYNC_ON);
	assert(ctx.advertisements == 2 && ctx.cancellations == 1);
	assert(ar_dev_mcs_observe(&ctx, 1) == (AR_DEV_MCS_APPLY_BUFFER | AR_DEV_MCS_WRITE_FCH | AR_DEV_MCS_SYNC_OFF));
	assert(!ctx.pending && ctx.acknowledgements == 1 && ctx.cancellations == 2);
	assert(ar_dev_mcs_preencode(&ctx, ++now, 0) == 0);

	/* Timeout requests RX reset only when the phase turns on. It neither
	 * invents an ACK nor releases the pending MCS transaction. */
	cfg = config(); cfg.max_wait_ms = 20;
	assert(ar_dev_mcs_init(&ctx, &cfg) == 0); ar_dev_mcs_connect(&ctx, 1); now = 0;
	samples(&ctx, &now, 8, 500, 0);
	for (unsigned i = 0; i < 9; i++) { ar_dev_mcs_preencode(&ctx, now + 30, 0); }
	assert(ctx.timeouts == 0);
	assert(ar_dev_mcs_preencode(&ctx, now + 30, 0) == (AR_DEV_MCS_SYNC_ON | AR_DEV_MCS_RESET_RX));
	assert(ctx.timeouts == 1 && ctx.pending && !ctx.acknowledgements);
	assert(ar_dev_mcs_connect(&ctx, 0) == (AR_DEV_MCS_WRITE_FCH | AR_DEV_MCS_SYNC_OFF));
	assert(!ctx.pending && !ctx.started);
	assert(ar_dev_mcs_connect(&ctx, 0) == 0);
	assert(ar_dev_mcs_interleaver(&ctx) == 0);
	puts("DEV receive MCS policy tests passed");
	return 0;
}
