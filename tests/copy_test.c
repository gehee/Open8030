/* SPDX-License-Identifier: MIT */
#include "ar_copy.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int main(void)
{
	/* Exact allocation ends expose word reads past a short final fragment. */
	for (size_t length = 0; length <= 128; length++) {
		for (size_t from = 0; from < 4; from++) {
			for (size_t to = 0; to < 4; to++) {
				unsigned char *source = malloc(from + length + (length == 0));
				unsigned char *dest = malloc(to + length + 1);
				assert(source && dest);
				for (size_t i = 0; i < from + length; i++) { source[i] = (unsigned char)(i * 37); }
				memset(dest, 0xa5, to + length + 1);
				ar_copy(dest + to, source + from, length);
				assert(!memcmp(dest + to, source + from, length));
				for (size_t i = 0; i < to; i++) { assert(dest[i] == 0xa5); }
				assert(dest[to + length] == 0xa5);
				free(source); free(dest);
			}
		}
	}
	puts("unaligned copy lengths, allocation bounds and destination guards passed");
}
