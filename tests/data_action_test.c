/* SPDX-License-Identifier: MIT */
#include "ar_data_action.h"
#include "ar_data_route.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
	struct ar_data_action_profile profile;
	unsigned lookups, writes, references;
	int present;
	uint8_t output[128]; uint32_t length;
	struct ar_data_destination destination;
};
static const struct ar_data_action_profile *source(uint16_t id, void *context)
{
	struct fixture *f = context;
	assert(id == 2); f->lookups++;
	return f->present ? &f->profile : NULL;
}
static struct ar_data_destination *find(unsigned id, void *context)
{
	struct fixture *f = context; assert(id == 3); return &f->destination;
}
static void retain(struct ar_data_packet *packet, void *context)
{
	(void)packet; (void)context; assert(0 && "unexpected retain");
}
static void release(struct ar_data_packet *packet, void *context)
{
	struct fixture *f = context;
	assert(packet && f->references > 0); f->references--;
}
static int enqueue(struct ar_data_destination *d, struct ar_data_packet *p,
	struct ar_data_route *r, uint8_t m, uint16_t s, void *context)
{
	(void)d; (void)p; (void)r; (void)m; (void)s; (void)context;
	assert(0 && "unexpected queue"); return -1;
}
static void diagnose(struct ar_data_destination *d, void *context)
{
	(void)d; (void)context; assert(0 && "unexpected failure");
}
static int write_packet(struct ar_data_destination *d, uint8_t metadata,
	const uint8_t *data, uint32_t length, uint16_t source_id, void *context)
{
	struct fixture *f = context;
	assert(d == &f->destination && metadata == 0xa5 && source_id == 2);
	assert(length <= sizeof f->output); memcpy(f->output, data, length);
	f->length = length; f->writes++; return (int)length;
}
int main(void)
{
	const uint8_t prefix[] = {0xc8}, frame[] = {0xc8, 4, 1, 2, 3, 0};
	const uint8_t expected[] = {0xff, 0xa5, 0xaa, 0x5a, 0xff, 3, 0, 0xa5,
		0, 0, 0, 0, 0, 6, 0, 1, 2, 3};
	uint8_t storage[64] = {0}, metadata = 0xa5;
	struct ar_data_action_buffer b = {storage, sizeof storage, 18, 24};
	struct fixture f = {.profile = {1, 2, 1, 1, prefix}, .present = 1, .references = 2};
	const struct ar_data_action_ops ops = {source};
	memcpy(storage + 18, frame, sizeof frame);
	assert(ar_data_action_apply(&b, 12, 2, &metadata, 0, &ops, &f) == 0);
	assert(b.head == 5 && b.tail == 23 && f.lookups == 1);
	assert(!memcmp(storage + b.head, expected, sizeof expected));
	f.destination.enabled = 1; f.destination.write = write_packet;
	struct ar_data_packet packet = {storage + b.head, (uint32_t)(b.tail - b.head)};
	struct ar_data_route route = {.destinations = 1u << 3};
	struct ar_data_route_stats stats = {0};
	const struct ar_data_route_ops delivery = {find, retain, enqueue, release, diagnose};
	assert(ar_data_route_deliver(&route, &packet, metadata, 2, &stats, &delivery, &f) == 0);
	assert(f.writes == 1 && f.length == sizeof expected && !memcmp(f.output, expected, sizeof expected));
	assert(f.references == 1 && route.bytes == sizeof expected && route.packets == 1);
	/* The action helper never owns a reference. Delivery releases only its
	 * supplied reference; fixture-owned allocation lifetime remains separate. */
	release(&packet, &f); assert(f.references == 0);

	/* Default envelope recognition removes exactly 15 bytes. */
	f.profile.custom = 0; f.lookups = 0;
	assert(ar_data_action_apply(&b, 1, 2, &metadata, 0, &ops, &f) == 0);
	assert(b.head == 20 && b.tail == 23 && !memcmp(storage + b.head, "\1\2\3", 3));
	assert(f.lookups == 1);
	/* Fewer than 15 bytes with the full marker: failed trim is a no-op. */
	memcpy(storage + 18, expected, 5); b.head = 18; b.tail = 23;
	assert(ar_data_action_apply(&b, 1, 2, &metadata, 0, &ops, &f) == 0);
	assert(b.head == 18 && b.tail == 23);
	f.present = 0;
	assert(ar_data_action_apply(&b, 5, 2, &metadata, 0, &ops, &f) == 0);
	assert(b.head == 18 && b.tail == 23);

	/* Exact-size prefix storage must not be read past the logical packet. */
	uint8_t small[4] = {0xff, 0xa5, 0xaa, 0x5a};
	b = (struct ar_data_action_buffer){small, sizeof small, 0, sizeof small};
	f.present = 1;
	assert(ar_data_action_apply(&b, 1, 2, &metadata, 0, &ops, &f) == -22);
	assert(ar_data_action_apply(&b, 0x102, 2, &metadata, 0x56, &ops, &f) == -22);
	assert(metadata == 0x56 && b.head == 0 && b.tail == 4);
	/* Earlier transforms persist if a subsequent prepend lacks headroom. */
	b = (struct ar_data_action_buffer){storage, sizeof storage, 18, 24};
	memcpy(storage + 18, frame, sizeof frame); f.profile.custom = 1;
	assert(ar_data_action_apply(&b, 6, 2, &metadata, 0, &ops, &f) == -22);
	assert(b.head == 5 && b.tail == 23);
	/* Head and tail trims are individually bounded, in that order. */
	b.head = 18; b.tail = 24; memcpy(storage + 18, frame, sizeof frame);
	f.profile.head_bytes = 7; f.profile.tail_bytes = 1;
	assert(ar_data_action_apply(&b, 1, 2, &metadata, 0, &ops, &f) == 0);
	assert(b.head == 18 && b.tail == 23);

	const size_t lengths[] = {0, 1, 255, 65535, 65536, 65537};
	for (unsigned i = 0; i < sizeof lengths / sizeof lengths[0]; ++i) {
		size_t length = lengths[i]; uint8_t *data = malloc(length + 16);
		assert(data); memset(data, 0xff, length + 16);
		b = (struct ar_data_action_buffer){data, length + 16, 15, length + 15};
		metadata = 0xa5;
		assert(ar_data_action_apply(&b, 2, 2, &metadata, 0, NULL, NULL) == 0);
		unsigned encoded = (unsigned)(length & 65535), sum = (encoded * 255) & 65535;
		assert(b.head == 0 && b.tail == length + 15 && !memcmp(data, expected, 5));
		assert(data[5] == (uint8_t)encoded && data[6] == encoded >> 8 && data[7] == metadata);
		assert(data[13] == (uint8_t)sum && data[14] == sum >> 8);
		for (unsigned j = 8; j < 13; ++j) { assert(data[j] == 0); }
		for (size_t j = 15; j < length + 16; ++j) { assert(data[j] == 0xff); }
		free(data);
	}
	b = (struct ar_data_action_buffer){storage, sizeof storage, 0, 0};
	assert(ar_data_action_apply(&b, UINT64_C(1) << 63, 2, &metadata, 0, NULL, NULL) == 0);
	assert(ar_data_action_apply(&b, 1, 2, &metadata, 0, NULL, NULL) == -22);
	assert(ar_data_action_apply(NULL, 0, 2, &metadata, 0, NULL, NULL) == -22);
	b.head = 1;
	assert(ar_data_action_apply(&b, 0, 2, &metadata, 0, NULL, NULL) == -22);
	return 0;
}
