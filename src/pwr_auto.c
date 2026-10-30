/* SPDX-License-Identifier: MIT */
#include "ar_pwr_auto.h"
#include <string.h>

int ar_pwr_auto_db10(uint32_t raw)
{
	/* 100 log10(x) = 30.103 log2(x); log2 from the top bit and a 16-step
	 * table of log2(1 + i/16) x 1024, interpolated: within 0.05 dB. */
	static const uint16_t frac[17] = {0, 90, 174, 254, 330, 402, 470, 536, 599, 659, 717, 773,
		827, 879, 929, 977, 1024};
	if (!raw) { return -1000; }
	unsigned top = 31u - (unsigned)__builtin_clz(raw);
	uint32_t m = top >= 12 ? raw >> (top - 12) : raw << (12 - top);   /* 1.12 fixed point, 4096..8191 */
	unsigned i = (m >> 8) & 15, rest = m & 255;
	uint32_t log2_1024 = top * 1024u + frac[i] + ((uint32_t)(frac[i + 1] - frac[i]) * rest >> 8);
	return (int)((log2_1024 * 30103u / 1024u + 500u) / 1000u) - 156;   /* - 100 log10(36) */
}

void ar_pwr_auto_init(struct ar_pwr_auto *ctx)
{
	if (!ctx) { return; }
	memset(ctx, 0, sizeof *ctx);
	ctx->policy = (struct ar_pwr_auto_policy){
		.target_db10 = AR_PWR_AUTO_TARGET_DB10, .band_db10 = AR_PWR_AUTO_BAND_DB10,
		.overload_gain = AR_PWR_AUTO_OVERLOAD_GAIN, .weak_gain = AR_PWR_AUTO_WEAK_GAIN,
		.burst = AR_PWR_AUTO_BURST, .window = AR_PWR_AUTO_WINDOW,
		.down_ms = AR_PWR_AUTO_DOWN_MS, .overload_ms = AR_PWR_AUTO_OVERLOAD_MS,
		.up_records = AR_PWR_AUTO_UP_RECORDS, .down_records = AR_PWR_AUTO_DOWN_RECORDS,
		.clean_dbm = AR_PWR_AUTO_CLEAN_DBM, .ceiling_db10 = AR_PWR_AUTO_CEILING_DB10,
		.ceiling_slope_db10 = AR_PWR_AUTO_CEILING_SLOPE, .down_step = AR_PWR_AUTO_DOWN_STEP,
		.up_step = AR_PWR_AUTO_UP_STEP, .up_hold_ms = AR_PWR_AUTO_UP_HOLD_MS};
	ctx->max_dbm = ctx->min_dbm = 39;
}

int ar_pwr_auto_set_range(struct ar_pwr_auto *ctx, unsigned on, unsigned max_dbm, unsigned min_dbm)
{
	if (!ctx) { return -22; }
	if (on && (max_dbm > 39 || min_dbm < 9 || min_dbm > max_dbm)) { return -22; }
	ctx->enabled = !!on;
	if (on) { ctx->max_dbm = (uint8_t)max_dbm; ctx->min_dbm = (uint8_t)min_dbm; }
	ctx->verdict = 0;
	return 0;
}

void ar_pwr_auto_reset(struct ar_pwr_auto *ctx)
{
	if (!ctx) { return; }
	ctx->peer_dbm = 0; ctx->window = 0; ctx->run = 0; ctx->filtered = 0; ctx->snr = 0; ctx->snr_db10 = 0; ctx->gain_x16 = 0;
	ctx->verdict = 0; ctx->since_request = 0; ctx->slow_x16 = 0; ctx->best_dbm = 0; ctx->lower_since = 0; ctx->up_votes = 0;
	ctx->up_at = 0;
}

/* 10^(db10 / 100) x 2^16, db10 in -400..400: whole dB from a table, the
 * tenths linearly between (within 0.1 dB). */
static uint64_t ratio_q16(int db10)
{
	static const uint32_t tenth[11] = {10000, 12589, 15849, 19953, 25119, 31623, 39811, 50119,
		63096, 79433, 100000};
	if (db10 < -400) { db10 = -400; }
	if (db10 > 400) { db10 = 400; }
	int whole = db10 >= 0 ? db10 / 10 : -((-db10 + 9) / 10);
	int frac = db10 - whole * 10;   /* 0..9 */
	int decade = whole >= 0 ? whole / 10 : -((-whole + 9) / 10);
	int unit = whole - decade * 10;   /* 0..9 */
	uint64_t a = tenth[unit], b = tenth[unit + 1];
	uint64_t v = (a * (10 - (unsigned)frac) + b * (unsigned)frac) * 65536u / 100000u;   /* x 2^16 / 10 */
	for (; decade > 0; decade--) { v *= 10; }
	for (; decade < 0; decade++) { v /= 10; }
	return v;
}

/* 1/snr in a fixed scale (2^56 / (10^(db10/100) x 2^16)): the noises add. */
static uint64_t inverse(int db10) { uint64_t r = ratio_q16(db10); return r ? ((uint64_t)1 << 56) / r : UINT64_MAX; }

static int ceiling_db10(const struct ar_pwr_auto_policy *p, int dbm)
{
	return p->ceiling_db10 - (dbm > p->clean_dbm ? (dbm - p->clean_dbm) * p->ceiling_slope_db10 : 0);
}

/* The distance's part of the SNR at now_dbm, as 1/snr: what is measured
 * less the ceiling's; at least 10 dB above the ceiling (a measurement at or
 * over it says only "much more than the ceiling"). */
static uint64_t distance_inverse(const struct ar_pwr_auto_policy *p, int measured_db10, int now_dbm)
{
	if (measured_db10 < 0) { measured_db10 = 0; }   /* keeps the products in range */
	const uint64_t cap = inverse(ceiling_db10(p, now_dbm)), meas = inverse(measured_db10);
	return meas > cap + cap / 10 ? meas - cap : cap / 10;
}

static uint64_t predicted_inverse(const struct ar_pwr_auto_policy *p, uint64_t distance, int now_dbm, int dbm)
{
	/* The distance's SNR moves with the power, dB for dB. */
	const uint64_t moved = distance * 65536u / ratio_q16((dbm - now_dbm) * 10);
	return moved + inverse(ceiling_db10(p, dbm));
}

int ar_pwr_auto_predict_db10(const struct ar_pwr_auto_policy *p, int measured_db10, int now_dbm, int dbm)
{
	if (!p) { return 0; }
	uint64_t inv = predicted_inverse(p, distance_inverse(p, measured_db10, now_dbm), now_dbm, dbm);
	/* Back to dB x 10: the first ratio at or above 1/inv. */
	int db10 = -400;
	while (db10 < 400 && inverse(db10) > inv) { db10 += 1; }
	return db10;
}

uint8_t ar_pwr_auto_best(const struct ar_pwr_auto_policy *p, int measured_db10, int now_dbm,
	int min_dbm, int max_dbm)
{
	if (!p || min_dbm > max_dbm) { return (uint8_t)now_dbm; }
	const uint64_t distance = distance_inverse(p, measured_db10, now_dbm);
	int best = now_dbm < min_dbm ? min_dbm : now_dbm > max_dbm ? max_dbm : now_dbm;
	uint64_t best_inv = predicted_inverse(p, distance, now_dbm, best);
	const uint64_t stay_inv = best_inv;
	for (int dbm = min_dbm; dbm <= max_dbm; dbm++) {
		uint64_t inv = predicted_inverse(p, distance, now_dbm, dbm);
		/* Lower power on a tie (within 0.1 dB). */
		if (inv * 1023u / 1000u < best_inv || (inv <= best_inv * 1023u / 1000u && dbm < best)) { best = dbm; best_inv = inv; }
	}
	/* Worth a move only for 1 dB or more. */
	if (best != now_dbm && stay_inv * 1000u < best_inv * AR_PWR_AUTO_MOVE_X1000) { return (uint8_t)now_dbm; }
	return (uint8_t)best;
}

unsigned ar_pwr_auto_record(struct ar_pwr_auto *ctx, uint32_t now_ms, unsigned errored,
	uint16_t snr, uint8_t gain_a, uint8_t gain_b, uint8_t *target_dbm, uint8_t *direction)
{
	if (!ctx || !target_dbm || !direction) { return 0; }
	ctx->records++;
	ctx->window = (uint16_t)(ctx->window << 1 | !!errored);
	ctx->run = errored ? (uint8_t)(ctx->run < 16 ? ctx->run + 1 : 16) : 0;
	const unsigned raw_gain = gain_a < gain_b ? gain_a : gain_b;
	ctx->gain_x16 = ctx->filtered ? (uint16_t)((int)ctx->gain_x16 + ((int)(raw_gain << 4) - (int)ctx->gain_x16) / 16)
	                              : (uint16_t)(raw_gain << 4);
	ctx->snr = ctx->filtered ? (uint16_t)((ctx->snr + 3u * snr) / 4) : snr;
	ctx->filtered = 1;
	ctx->snr_db10 = (int16_t)ar_pwr_auto_db10(ctx->snr);
	/* The model's SNR: ~64 records (~300 ms), raw scale x 16. */
	if (!ctx->slow_x16) { ctx->slow_x16 = (uint32_t)snr << 4; }
	else { ctx->slow_x16 = (uint32_t)((int32_t)ctx->slow_x16 + ((int32_t)((uint32_t)snr << 4) - (int32_t)ctx->slow_x16) / 64); }
	if (ctx->since_request < UINT32_MAX) { ctx->since_request++; }
	if (!ctx->enabled) { return 0; }

	const struct ar_pwr_auto_policy *p = &ctx->policy;
	const unsigned gain = (ctx->gain_x16 + 8u) >> 4;
	const int current = ctx->peer_dbm ? ctx->peer_dbm : ctx->max_dbm;
	const int short_snr = ctx->snr_db10 < p->target_db10;
	/* The receiver at its gain floor with the SNR short: overloaded, its
	 * SNR falls with more power - down quickly, before the model. */
	if (gain <= p->overload_gain && short_snr && current > ctx->min_dbm) {
		if (ctx->verdict != -1) { ctx->verdict = -1; ctx->verdict_ms = now_ms; }
		if (now_ms - ctx->verdict_ms < p->overload_ms || ctx->since_request <= p->down_records) { return 0; }
		*target_dbm = (uint8_t)(current - 1); *direction = 2;
		return 1;
	}
	ctx->verdict = 0;
	/* A fade: up from the fast SNR, once it has said so for up_records in a
	 * row (~40 ms) - the slow one would take a few hundred ms to see it. */
	if (ctx->since_request > p->up_records && gain > p->overload_gain) {   /* never up overloaded */
		const uint8_t up = ar_pwr_auto_best(p, ctx->snr_db10, current, ctx->min_dbm, ctx->max_dbm);
		ctx->up_votes = up > current ? (uint8_t)(ctx->up_votes < 255 ? ctx->up_votes + 1 : 255) : 0;
		if (ctx->up_votes >= p->up_records) {
			ctx->up_votes = 0; ctx->lower_since = 0; ctx->best_dbm = up;
			ctx->up_at = now_ms ? now_ms : 1;
			*target_dbm = (uint8_t)(up - current > p->up_step ? current + p->up_step : up); *direction = 1;
			return 1;
		}
	}
	/* Down on the SNR over ~300 ms (a fresh one after each request). */
	if (ctx->since_request < p->up_records * 4u) { return 0; }
	const int measured = ar_pwr_auto_db10((ctx->slow_x16 + 8u) >> 4);
	const uint8_t best = ar_pwr_auto_best(p, measured, current, ctx->min_dbm, ctx->max_dbm);
	ctx->best_dbm = best;
	if (best > current) { ctx->lower_since = 0; return 0; }   /* up is the fast SNR's */
	if (best == current) { ctx->lower_since = 0; return 0; }
	if (ctx->up_at && now_ms - ctx->up_at < p->up_hold_ms) { ctx->lower_since = 0; return 0; }   /* held after an up */
	if (!ctx->lower_since) { ctx->lower_since = now_ms ? now_ms : 1; }
	if (now_ms - ctx->lower_since < p->down_ms) { return 0; }
	ctx->lower_since = 0;
	*target_dbm = (uint8_t)(current - best > p->down_step ? current - p->down_step : best); *direction = 2;
	return 1;
}
void ar_pwr_auto_sent(struct ar_pwr_auto *ctx, uint8_t target_dbm, uint8_t direction)
{
	if (!ctx) { return; }
	ctx->peer_dbm = target_dbm;
	ctx->since_request = 0;
	ctx->verdict = 0;
	ctx->slow_x16 = 0;   /* the model judges the new power on its own readings */
	ctx->up_votes = 0;
	ctx->lower_since = 0;
	if (direction == 1) { ctx->requests_up++; } else { ctx->requests_down++; }
}

void ar_pwr_auto_peer(struct ar_pwr_auto *ctx, uint8_t dbm)
{
	if (ctx && dbm >= 9 && dbm <= 39) { ctx->peer_dbm = dbm; }
}

uint32_t ar_pwr_auto_announce_word(uint8_t dbm)
{
	return AR_PWR_AUTO_ANNOUNCE | (uint32_t)dbm << 4;
}

int ar_pwr_auto_parse_announce(uint32_t word, uint8_t *dbm)
{
	if ((word & 15) != AR_PWR_AUTO_ANNOUNCE || (word >> 12) || !dbm) { return -22; }
	*dbm = (uint8_t)(word >> 4);
	return *dbm >= 9 && *dbm <= 39 ? 0 : -22;
}

uint32_t ar_pwr_auto_word(uint8_t target_dbm, uint8_t direction, uint8_t slot)
{
	return AR_PWR_AUTO_SUBTYPE | (uint32_t)target_dbm << 4 | (uint32_t)(direction & 15) << 12 |
		(uint32_t)(slot & 15) << 16;
}

int ar_pwr_auto_parse(uint32_t word, uint8_t *target_dbm, uint8_t *direction, uint8_t *slot)
{
	if ((word & 15) != AR_PWR_AUTO_SUBTYPE || (word >> 21) || !target_dbm || !direction || !slot) { return -22; }
	*target_dbm = (uint8_t)(word >> 4);
	*direction = (uint8_t)((word >> 12) & 15);
	*slot = (uint8_t)((word >> 16) & 15);
	return 0;
}

uint8_t ar_pwr_auto_apply(uint8_t current_dbm, uint8_t target_dbm, uint8_t direction,
	uint8_t min_dbm, uint8_t max_dbm)
{
	if (direction != 1 && direction != 2) { return 0; }
	if (min_dbm < 9 || max_dbm > 39 || min_dbm >= max_dbm) { return 0; }
	unsigned next = target_dbm;
	if (direction == 1 && next <= current_dbm) { next = current_dbm + 1u; }
	if (next > max_dbm) { next = max_dbm; }
	if (next < min_dbm) { next = min_dbm; }
	return next == current_dbm ? 0 : (uint8_t)next;
}
