/* SPDX-License-Identifier: MIT */
/* Independent DEV idle admission, Ascent VRX 18.21.10 0x22d996 and helpers.
 * Wire integrity is handled by mac.c; transition effects belong to the RF/MAC
 * scheduler. Keeping these separate prevents an accepted check from asserting
 * that hardware synchronization or the over-air login has completed. */
#include "ar_dev.h"
#include <string.h>

void ar_dev_stream_init(struct ar_dev_stream *stream)
{
	if (stream) { memset(stream, 0, sizeof *stream); }
}

int ar_dev_stream_feed(struct ar_dev_stream *stream, const uint8_t *data, size_t length,
	ar_dev_command_callback callback, void *context)
{
	if (!stream || (!data && length) || stream->used >= sizeof stream->bytes) { return AR_MAC_FORMAT; }
	for (size_t i = 0; i < length; i++) {
		stream->bytes[stream->used++] = data[i];
		if (stream->used < 4) { continue; }
		/* Original 0x232f9a/0x233302 scans the four-byte header for magic,
		 * retains a partial header and reads exactly the declared body. */
		if (stream->bytes[0] != 0x4a) {
			size_t skip = 1;
			while (skip < 4 && stream->bytes[skip] != 0x4a) { skip++; }
			memmove(stream->bytes, stream->bytes + skip, stream->used - skip);
			stream->used -= skip; stream->discarded += skip;
			continue;
		}
		if (stream->bytes[2] > 128) {
			stream->length_errors++; stream->discarded += stream->used; stream->used = 0;
			continue;
		}
		size_t total = 4u + stream->bytes[2];
		if (stream->used < total) { continue; }
		uint8_t check = 0;
		for (size_t j = 0; j < total; j++) { check ^= stream->bytes[j]; }
		if (check) {
			/* Stock discards the consumed envelope on a failed XOR check. */
			stream->checksum_errors++; stream->discarded += total;
		} else {
			if (stream->bytes[1] < 8) { stream->commands[stream->bytes[1]]++; }
			if (callback) { callback(stream->bytes, total, context); }
		}
		stream->used = 0;
	}
	return AR_MAC_OK;
}

void ar_dev_retx_init(struct ar_dev_retx *context, uint8_t threshold)
{
	if (context) {
		*context = (struct ar_dev_retx){.threshold = threshold < 128 ? threshold : 0, .last_id = 255};
	}
}

enum ar_dev_retx_action ar_dev_retx_feed(struct ar_dev_retx *context, uint8_t first, uint8_t second)
{
	if (!context || (first > 15 && first != 255) || (second > 15 && second != 255) || context->phase > 2) {
		return AR_DEV_RETX_INVALID;
	}
	if (!context->threshold) { return AR_DEV_RETX_NONE; }
	uint8_t newest = second == 255 || first > second ? first : second;
	if (context->last_id == 255) {
		context->repeated = 0; context->last_id = newest;
		return AR_DEV_RETX_NONE;
	}
	if (context->phase) {
		if (--context->phase) { return AR_DEV_RETX_NONE; }
		context->repeated = 0; context->last_id = newest;
		return AR_DEV_RETX_ENABLE;
	}
	/* 0x231676 computes these once before examining both request IDs. The
	 * second comparison still uses the previous ID even if the first advances. */
	unsigned next = (context->last_id + 1) & 15, after = (context->last_id + 2) & 15;
	uint8_t ids[2] = {first, second};
	for (unsigned i = 0; i < 2; i++) {
		if (ids[i] == 255) { continue; }
		if (ids[i] == next || ids[i] == after) {
			context->repeated = 0; context->last_id = ids[i];
		} else { context->repeated++; }
	}
	if (context->repeated < context->threshold) { return AR_DEV_RETX_NONE; }
	context->phase = 2;
	return AR_DEV_RETX_DISABLE;
}

static int pairing_match(const struct ar_dev_idle_config *config,
	const struct ar_dev_control *control, unsigned first, unsigned end)
{
	/* 0x228a30: these entries suppress pairing interpretation, but do not
	 * bypass the ordinary local-position and configured/candidate checks. */
	for (unsigned i = 0; i < 3; i++) {
		if (!memcmp(config->pairing_skip[i], control->peer, 4)) { return 0; }
	}
	for (unsigned i = first; i < end; i++) {
		if ((control->slots[i] & 9) == 9) { return config->pairing ? 1 : -1; }
	}
	return config->pairing ? -1 : 0;
}

static unsigned local_position(const struct ar_dev_idle_config *config,
	const struct ar_dev_control *control)
{
	/* The original message context defaults to 8. Extension 2 replaces this
	 * with the first local-address match; it is not the peer candidate list. */
	if (control->extensions & 4) {
		const uint8_t *list = control->extension[2].data;
		for (unsigned i = 0; i < list[0]; i++) {
			if (!memcmp(list + 1 + 4 * i, config->local, 4)) { return i; }
		}
	}
	return 8;
}

int ar_dev_idle_evaluate(const struct ar_dev_idle_config *config,
	const struct ar_dev_control *control, struct ar_dev_acquisition *next)
{
	if (!next) { return AR_DEV_BAD_CONFIG; }
	memset(next, 0, sizeof *next);
	if (!config || !control || !control->data || control->length < 32 ||
	    !config->channel_count || config->receive_channel >= config->channel_count ||
	    config->mode > 3 || config->relay > 1 || config->pairing > 1 ||
	    config->candidate_count > AR_DEV_MAX_CANDIDATES ||
	    (config->candidate_count && !config->candidates)) { return AR_DEV_BAD_CONFIG; }
	if (control->receive_channel != config->receive_channel) { return AR_DEV_CHANNEL; }
	if (((control->data[3] >> 5) & 1) != config->relay) { return AR_DEV_RELAY; }
	int pairing = pairing_match(config, control, 0, 8);
	if (pairing < 0) { return AR_DEV_PAIRING; }
	unsigned position = local_position(config, control);
	if (!pairing) {
		if (position != 0) { return AR_DEV_LOCAL_POSITION; }
		unsigned found = !memcmp(config->configured_peer, control->peer, 4);
		for (size_t i = 0; !found && i < config->candidate_count; i++) {
			found = !memcmp(config->candidates[i], control->peer, 4);
		}
		if (!found) { return AR_DEV_PEER; }
	}
	if (control->mode != config->mode || control->slot_bitmap != config->slot_bitmap) {
		next->mode = control->mode; next->slot_bitmap = control->slot_bitmap;
		return AR_DEV_RECONFIGURE;
	}
	if (control->transmit_channel >= config->channel_count) { return AR_DEV_CHANNEL; }
	unsigned slot;
	for (slot = 0; slot < 8; slot++) {
		if ((control->slots[slot] & 7) == 1 &&
		    (!pairing || pairing_match(config, control, slot, slot + 1) == 1)) { break; }
	}
	if (slot == 8) { return AR_DEV_NO_SLOT; }
	unsigned selected = config->mode ? 8 : 0;
	if (config->mode == 3) {
		selected = pairing ? slot : position;
		if (selected >= 8 || (control->slots[selected] & 7) != 1) { return AR_DEV_NO_SLOT; }
	}
	struct ar_dev_acquisition plan = {0};
	memcpy(plan.peer, control->peer, 4);
	for (unsigned i = 0; i < 4; i++) { plan.peer_hash ^= plan.peer[i]; }
	plan.selected_slot = selected; plan.available_slot = slot;
	plan.receive_channel = control->receive_channel;
	plan.transmit_channel = control->transmit_channel;
	plan.mode = control->mode; plan.slot_bitmap = control->slot_bitmap;
	plan.pairing = pairing; plan.local_position = position;
	plan.control_field_b00 = !(control->data[3] & 0x40);
	plan.rebuild_frame = config->mode == 1;
	*next = plan;
	return AR_DEV_READY;
}

enum ar_dev_lock_result ar_dev_lock_evaluate(const struct ar_dev_idle_config *config,
	const struct ar_dev_lock_context *lock, const struct ar_dev_control *control,
	struct ar_dev_login *login)
{
	if (!login) { return AR_DEV_LOCK_BAD_CONFIG; }
	memset(login, 0, sizeof *login);
	if (!config || !lock || !control || !control->data || control->length < 32 ||
	    lock->selected_slot >= 8 || lock->assigned_slot > 8 ||
	    lock->transmit_channel >= config->channel_count ||
	    !config->channel_count || config->receive_channel >= config->channel_count) {
		return AR_DEV_LOCK_BAD_CONFIG;
	}
	/* Retuning and other frame modes require additional effects.
	 * Keep them explicit instead of extending this fixed-mode decision. */
	if (config->mode || control->mode ||
	    control->slot_bitmap != config->slot_bitmap ||
	    control->receive_channel != config->receive_channel ||
	    control->transmit_channel != lock->transmit_channel ||
	    ((control->data[3] >> 5) & 1) != config->relay ||
	    pairing_match(config, control, 0, 8) < 0) { return AR_DEV_LOCK_PROFILE; }
	/* 0x228b12 compares against the admitted peer once the node is locked;
	 * another configured candidate cannot replace that identity here. */
	if (memcmp(lock->peer, control->peer, 4)) { return AR_DEV_LOCK_PEER; }
	/* 0x22eba6: an existing assignment's token is checked before searching
	 * the current message's login records. Caller must discard a stale slot. */
	if (lock->assigned_slot < 8 &&
	    (control->slots[lock->assigned_slot] >> 8) != lock->token) {
		return AR_DEV_LOCK_TOKEN_CHANGED;
	}
	if (!(control->extensions & 1)) {
		return (control->slots[lock->selected_slot] & 7) == 1 ?
			AR_DEV_LOCK_WAIT_LOGIN : AR_DEV_LOCK_TEARDOWN;
	}
	/* 0x229dd8 expands packed ten-byte records into twelve-byte scratch
	 * slots. Iterate the wire bitmap directly, preserving sparse slot IDs. */
	const struct ar_mac_extension *extension = &control->extension[0];
	if (!extension->length) { return AR_DEV_LOCK_NO_POSITION; }
	const uint8_t *entry = extension->data + 1;
	for (unsigned slot = 0; slot < 8; slot++) {
		if (!(extension->data[0] & (1u << slot))) { continue; }
		if (!memcmp(entry + 2, config->local, 4) &&
		    entry[0] == (control->slots[slot] >> 8)) {
			/* 0x22ed02..0x22ed82 assigns these before remote sync and
			 * AP-state checks. A record alone does not complete a link. */
			login->slot = slot; login->token = entry[0]; login->value = entry[1];
			login->ap_state = (control->slots[slot] >> 1) & 3;
			memcpy(login->sync, entry + 6, 4);
			return AR_DEV_LOCK_ASSIGNMENT;
		}
		entry += 10;
	}
	return AR_DEV_LOCK_NO_POSITION;
}
