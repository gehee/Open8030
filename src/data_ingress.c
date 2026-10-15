/* SPDX-License-Identifier: MIT */
#include "ar_data_ingress.h"

static unsigned le16(const uint8_t *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

int ar_data_ingress(unsigned port, const uint8_t *request, size_t length,
	const struct ar_data_ingress_ops *ops, void *context)
{
	if (port > 2 || !request || !length || !ops || !ops->find ||
	    !ops->feed || !ops->control) { return -22; }
	if (request[0] != 6) { return ops->control(request, length, context); }
	if (length < 4) { return -22; }
	unsigned count = port ? 0 : le16(request + 2);
	void *parser = ops->find(3, port, context);
	if (!parser) { return 0; }
	const uint8_t *data = request + 4;
	size_t available = length - 4;
	if (port) {
		if (available < 5) { return -22; }
		if (data[0] != 0xff || data[1] != 0xa5 || data[2] != 0xaa ||
		    data[3] != 0x5a || data[4] != 0xff) { return 0; }
		if (available < 7) { return -22; }
		count = le16(data + 5) + 15;
		if (count > UINT16_MAX) { return -22; }
		if (count > 4129) { return 0; }
	}
	if (count > available) { return -22; }
	(void)ops->feed(parser, data, count, context);
	return 0;
}
