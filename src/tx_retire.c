/* SPDX-License-Identifier: MIT */
#include "ar_tx_retire.h"

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		(uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int ar_tx_retire(struct ar_tx_retire_ring *ring, unsigned retain,
	ar_tx_retire_release release, void *context)
{
	if (!ring)
		return 0;
	if (!release || ring->tail >= 10 || ring->owned > 10)
		return -22;
	unsigned retired = 0;
	while (ring->owned > retain) {
		if (ring->tail >= 10 || ring->owned > 10 || retired == 10)
			return -22;
		struct ar_tx_retire_packet packet = ring->packets[ring->tail];
		if (!packet.data || packet.length < 8)
			return -22;
		for (unsigned i = 0; i < packet.data[7]; ++i) {
			if (packet.length < 8 + 4 * (i + 1))
				return -22;
			uint32_t descriptor = get32(packet.data + 8 + 4 * i);
			release((ring->configuration >> 24) & 15, descriptor & 255,
				0, descriptor >> 12, context);
		}
		if (ring->tail >= 10 || !ring->owned || ring->owned > 10)
			return -22;
		ring->tail = (ring->tail + 1) % 10;
		ring->owned--;
		retired++;
	}
	return 0;
}
