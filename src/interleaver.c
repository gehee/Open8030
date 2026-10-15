/* SPDX-License-Identifier: MIT */
#include "ar_interleaver.h"

int ar_interleaver_service(struct ar_interleaver *state,
	const struct ar_interleaver_input *input, unsigned source,
	const struct ar_interleaver_ops *ops, void *context)
{
	if (!state || !input || !ops || input->user > 15 || input->second_user >= 12 ||
	    source >= 32 || input->high_clock > 1 || !ops->field || !ops->exchange || !ops->read || !ops->reset ||
	    !ops->control || !ops->apply || !ops->retry_reset || !ops->post || !ops->notify) { return -22; }
	unsigned user = input->user;
	if (user == 8 && input->distance_enabled && !input->distance_disabled) {
		uint32_t previous = ops->exchange(0x8081b01,1,context);
		ops->field(0x8000501,1,context);
		uint32_t a = input->timestamps[0], b = input->timestamps[1], c = input->timestamps[2];
		ops->field(0x8000501,0,context);
		ops->field(0x8081b01,previous,context);
		uint32_t first = ((a >> 8) & 0xff0000u) | ((b << 8) & 0xffffu) | ((b >> 8) & 255u);
		uint32_t second = (b & 0xff0000u) | ((b >> 16) & 0xff00u) | (c & 255u);
		uint32_t raw = (first - second) & 65535u;
		int delta = raw < 32768 ? (int)raw : (int)raw - 65536;
		if (!state->initialized) {
			state->initialized = 1; state->initial_delta = delta; state->previous_delta = delta;
		}
		int change = delta - state->previous_delta;
		if (change > 2000 || change < -2000) {
			ops->reset(context);
			state->initialized = 0; state->correction_pending = 0;
			state->initial_delta = 0; state->previous_delta = 0;
		} else {
			state->previous_delta = delta;
			if (state->correction_pending) {
				state->correction_pending = 0;
				ops->control(11,1,100,context);
			} else {
				int scaled = delta * (input->high_clock ? 5 : 10);
				if (scaled > 2000 || scaled < -2000) {
					state->correction_pending = 1;
					ops->control(11,1,scaled > 0 ? 98 : 102,context);
				}
			}
		}
	}
	ops->field(0xb4c0305,(user << 1) | 1,context);
	uint8_t payload[8] = {(uint8_t)user,0,0,0,(input->request >> 20) & 15,255,0,0};
	if (ops->read(((user + 0x2c0u) << 18) | 0x401u,context)) {
		payload[5] = (input->request >> 16) & 15;
	}
	struct ar_interleaver_user *u = &state->users[input->second_user];
	unsigned suppress = 0;
	if (u->active == 1) {
		if (u->drain) {
			suppress = u->drain == 6 && !u->retries;
			u->drain--;
			if (suppress) { u->retries = 1; }
		} else {
			ops->apply(input->second_user,u->mcs,context);
		}
	}
	if (u->active && !suppress && u->retries && u->limit) {
		if ((uint8_t)(u->retries - 1) >= u->limit) { ops->retry_reset(input->second_user,context); }
		else { u->retries++; }
	}
	ops->post(payload,context); ops->notify(source,payload,context);
	return 0;
}
