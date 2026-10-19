/* SPDX-License-Identifier: MIT */
/* Independently implemented receive policy from Ascent VRX 18.21.10 instruction
 * evidence. Native state/layout and synchronous calls replace vendor queues.
 * See docs/configuration.md for policy parameters and validation limitations. */
#include "ar_dev_mcs.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }

int ar_dev_mcs_decode_config(struct ar_dev_mcs_config *config, const uint8_t *wire, size_t length)
{
	if (!config || !wire || length != AR_DEV_MCS_CONFIG_BYTES || (wire[0] & ~3u) ||
	    !wire[2] || wire[2] > AR_DEV_MCS_ROWS || wire[1] > AR_DEV_MCS_NONE || wire[3] > 1 ||
	    wire[10] > 1 || wire[11] != 2 || wire[12] > 100 || wire[13] || wire[14] || wire[15]) { return -22; }
	struct ar_dev_mcs_config parsed = {.enabled = wire[0] & 1, .automatic = wire[0] >> 1,
		.initial_mcs = wire[1], .count = wire[2], .policy = wire[3],
		.hold_ms = le16(wire + 4), .delay_samples = le16(wire + 6), .max_wait_ms = le16(wire + 8),
		.distance_enabled = wire[10], .distance_offset = wire[12]};
	for (unsigned i = 0; i < AR_DEV_MCS_ROWS; i++) {
		const uint8_t *p = wire + 16 + i * 12;
		if (i >= parsed.count) {
			for (unsigned j = 0; j < 12; j++) { if (p[j]) { return -22; } }
			continue;
		}
		/* Stock truncates the table at internal MCS12 when the link starts
		 * on 5 MHz (0x221b7e: 12); the link widens to 20 MHz after login, so
		 * rows up to internal 15 (MCS 13, 1024-QAM 3/4) are taken. */
		if (p[0] > 15 || (i && p[0] <= parsed.rows[i - 1].mcs)) { return -22; }
		parsed.rows[i] = (struct ar_dev_mcs_row){p[0], p[1], p[2], (int8_t)p[3],
			le16(p + 4), le16(p + 6), le16(p + 8), le16(p + 10)};
	}
	*config = parsed;
	return 0;
}

int ar_dev_mcs_init(struct ar_dev_mcs *ctx, const struct ar_dev_mcs_config *config)
{
	if (!ctx || !config || config->enabled > 1 || config->automatic > 1 ||
	    config->initial_mcs > AR_DEV_MCS_NONE || !config->count ||
	    config->count > AR_DEV_MCS_ROWS || config->policy > 1) { return -22; }
	for (unsigned i = 0; i < config->count; i++) {
		const struct ar_dev_mcs_row *row = &config->rows[i];
		if (row->mcs >= AR_DEV_MCS_NONE ||
		    (i && row->mcs <= config->rows[i - 1].mcs)) { return -22; }
	}
	memset(ctx, 0, sizeof *ctx);
	ctx->config = *config;
	ctx->current = ctx->previous = 255;
	ctx->observed = AR_DEV_MCS_NONE;
	ctx->fch[0] = 0x1e;
	return 0;
}

static unsigned cancel(struct ar_dev_mcs *ctx)
{
	ctx->advertised = 0;
	if (!(ctx->fch[1] & 15)) { return AR_DEV_MCS_NOTHING; }
	ctx->fch[1] &= 0xf0;
	ctx->cancellations++;
	return AR_DEV_MCS_WRITE_FCH | AR_DEV_MCS_SYNC_OFF;
}

unsigned ar_dev_mcs_connect(struct ar_dev_mcs *ctx, unsigned connected)
{
	connected = !!connected;
	if (connected == ctx->connected) { return AR_DEV_MCS_NOTHING; }
	ctx->connected = connected;
	ctx->ready = ctx->index = 0;
	if (connected) { return AR_DEV_MCS_NOTHING; }
	ctx->started = ctx->pending = ctx->direction = 0;
	ctx->delayed = 0;
	return cancel(ctx);
}

void ar_dev_mcs_set_ceiling(struct ar_dev_mcs *ctx, unsigned internal_mcs)
{
	if (ctx) { ctx->ceiling = internal_mcs < AR_DEV_MCS_NONE ? (uint8_t)internal_mcs : 0; }
}

unsigned ar_dev_mcs_observe(struct ar_dev_mcs *ctx, uint8_t mcs)
{
	if (mcs >= AR_DEV_MCS_NONE) { return AR_DEV_MCS_NOTHING; }
	ctx->observed = mcs;
	if (!ctx->connected || !ctx->started) { return AR_DEV_MCS_NOTHING; }
	if (ctx->pending) {
		if (mcs != ctx->config.rows[ctx->current].mcs) { return AR_DEV_MCS_NOTHING; }
		ctx->pending = 0;
		ctx->acknowledgements++;
		return AR_DEV_MCS_APPLY_BUFFER | cancel(ctx);
	}
	for (unsigned i = 0; i < ctx->config.count; i++) {
		if (ctx->config.rows[i].mcs != mcs || i == ctx->current) { continue; }
		ctx->previous = ctx->current; ctx->current = i;
		ctx->unsolicited++;
		return AR_DEV_MCS_APPLY_BUFFER;
	}
	return AR_DEV_MCS_NOTHING;
}

static unsigned request(struct ar_dev_mcs *ctx, uint32_t now_ms)
{
	ctx->pending = 1;
	ctx->request_ms = ctx->direction_ms = now_ms;
	ctx->requests++;
	/* LINK105 echoes PHY106 when its current observation already matches. */
	if (ctx->observed == ctx->config.rows[ctx->current].mcs) {
		return ar_dev_mcs_observe(ctx, ctx->observed);
	}
	ctx->phase = 1; ctx->preencodes = 0; ctx->retry_ms = now_ms;
	return AR_DEV_MCS_NOTHING;
}

unsigned ar_dev_mcs_sample(struct ar_dev_mcs *ctx, uint32_t now_ms,
	uint16_t snr, uint16_t errors)
{
	/* PHY103/24b64a: increment before insertion; a >200-ms gap discards
	 * window readiness. Eight new samples are needed even after reconnect. */
	if ((uint32_t)(now_ms - ctx->sample_ms) > 200) { ctx->ready = ctx->index = 0; }
	ctx->sample_ms = now_ms;
	ctx->index = (ctx->index + 1) & 7;
	ctx->snr[ctx->index] = snr; ctx->errors[ctx->index] = errors;
	ctx->samples++;
	if (!ctx->index) { ctx->ready = 1; }
	if (!ctx->connected || !ctx->ready || !ctx->config.enabled) { return AR_DEV_MCS_NOTHING; }
	if (!ctx->started) {
		if (ctx->delayed < ctx->config.delay_samples) { ctx->delayed++; return AR_DEV_MCS_NOTHING; }
		unsigned selected = 0, explicit = 0;
		uint16_t minimum = UINT16_MAX;
		for (unsigned i = 0; i < 8; i++) { if (ctx->snr[i] < minimum) { minimum = ctx->snr[i]; } }
		for (unsigned i = 0; i < ctx->config.count; i++) {
			if (ctx->config.rows[i].mcs == ctx->config.initial_mcs) { selected = i; explicit = 1; break; }
		}
		if (!explicit) {
			while (selected + 1 < ctx->config.count && ctx->config.rows[selected + 1].snr_down <= minimum) { selected++; }
		}
		while (ctx->ceiling && selected && ctx->config.rows[selected].mcs > ctx->ceiling) { selected--; }
		ctx->current = selected; ctx->previous = 255; ctx->started = 1;
		return request(ctx, now_ms);
	}
	if (!ctx->config.automatic || ctx->pending ||
	    (uint32_t)(now_ms - ctx->request_ms) < ctx->config.hold_ms) { return AR_DEV_MCS_NOTHING; }
	const struct ar_dev_mcs_row *current = &ctx->config.rows[ctx->current];
	const struct ar_dev_mcs_row *next = ctx->current + 1 < ctx->config.count ? current + 1 : NULL;
	/* The host's ceiling: no row above it. */
	if (next && ctx->ceiling && next->mcs > ctx->ceiling) { next = NULL; }
	const int over = ctx->ceiling && ctx->current && current->mcs > ctx->ceiling;
	unsigned up_snr = next ? next->snr_up : UINT16_MAX;
	unsigned down_snr = ctx->current ? current->snr_down : 0;
	unsigned up_ms = next ? next->up_ms : UINT16_MAX;
	unsigned down_ms = ctx->current ? current->down_ms : UINT16_MAX;
	unsigned bad = 0, recent = 0, average = 0;
	for (unsigned i = 0; i < 8; i++) {
		average += ctx->snr[i]; bad += !!ctx->errors[i];
	}
	for (unsigned i = 0; i < 8 && ctx->errors[(ctx->index - i) & 7]; i++) { recent++; }
	average >>= 3;
	ctx->average_snr = average; ctx->bad_samples = bad; ctx->recent_bad = recent;
	int direction = 1;
	unsigned forced = 0;
	if (!ctx->config.policy) {
		unsigned up = next ? next->errors_up : 0;
		unsigned down = ctx->current ? current->errors_down : 255;
		direction = bad <= up ? 1 : bad < down ? 0 : -1;
		if (ctx->current && current->consecutive_down > 0 && recent >= (unsigned)current->consecutive_down) {
			direction = -1; forced = 1;
		}
	}
	/* Above the host's ceiling (it was lowered): down at once. */
	if (over) { direction = -1; forced = 1; }
	/* 21fd74..21fd86: an LDPC down decision bypasses SNR. Otherwise
	 * low SNR may force down and the middle region suppresses promotion.
	 * High SNR alone does not override an LDPC neutral decision. */
	if (direction != -1 && average <= up_snr) { direction = average < down_snr ? -1 : 0; }
	/* Preserve the original one-sample neutral transition when direction
	 * changes, except an immediate consecutive-error downgrade. */
	if (ctx->direction == 1) {
		if (direction != 1) { ctx->direction = forced ? 2 : 0; }
	} else if (ctx->direction == 2) {
		if (direction != -1) { ctx->direction = 0; }
	} else if (direction) {
		ctx->direction = direction > 0 ? 1 : 2; ctx->direction_ms = now_ms;
	}
	uint32_t elapsed = now_ms - ctx->direction_ms;
	int step = ctx->direction == 1 && elapsed > up_ms ? 1 :
		ctx->direction == 2 && (elapsed >= down_ms || forced) ? -1 : 0;
	/* No hold on a row left for errors, as stock: up again after the next
	 * row's up time of good samples (0.8-1.5 s), down within its down time.
	 * Open8030 held such a row 30 s, doubling to 8 min, until 2026-10-18: the
	 * errors of a width switch or a link forming again held the MCS at -1 for
	 * 30 s at 19 dB, and at 6 for a minute at 21 dB. */
	if ((step > 0 && !next) || (step < 0 && !ctx->current) || !step) { return AR_DEV_MCS_NOTHING; }
	ctx->previous = ctx->current; ctx->current += step;
	return request(ctx, now_ms);
}

unsigned ar_dev_mcs_preencode(struct ar_dev_mcs *ctx, uint32_t now_ms, unsigned slot)
{
	if (!ctx->connected || !ctx->pending || slot >= 8) { return AR_DEV_MCS_NOTHING; }
	unsigned action = 0;
	ctx->preencodes++;
	if (ctx->preencodes > 4) {
		ctx->phase ^= 1; ctx->preencodes = 0;
		if (ctx->phase && ctx->config.max_wait_ms != UINT16_MAX &&
		    (uint32_t)(now_ms - ctx->retry_ms) >= ctx->config.max_wait_ms) {
			/* 23015e requests RX-engine reset and schedules the next timeout
			 * about ten milliseconds later, without completing the request. */
			ctx->retry_ms = now_ms + 10 - ctx->config.max_wait_ms;
			ctx->timeouts++; action |= AR_DEV_MCS_RESET_RX;
		}
	}
	if (!ctx->phase) { return action | (ctx->advertised ? cancel(ctx) : 0); }
	if (!ctx->advertised && !(ctx->fch[1] & 15)) {
		/* 22ba46(command3, TTL31, [slot, mcs | BW-sentinel6<<5], 2).
		 * Source18 writes this later; enqueue itself does not write B8C. */
		ctx->fch[1] = 0xf3; ctx->fch[2] = (ctx->fch[2] & 0xf0) | 5;
		ctx->fch[3] = slot; ctx->fch[4] = ctx->config.rows[ctx->current].mcs | 0xc0;
		ctx->advertised = 1; ctx->advertisements++;
		action |= AR_DEV_MCS_SYNC_ON;
	}
	return action;
}

unsigned ar_dev_mcs_interleaver(const struct ar_dev_mcs *ctx)
{
	return ctx->connected && (ctx->fch[1] & 15) ? AR_DEV_MCS_WRITE_FCH : AR_DEV_MCS_NOTHING;
}

