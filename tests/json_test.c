/* SPDX-License-Identifier: MIT */
#include "ar_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
	const char *valid = " {\"a\":{\"b\":[-2147483648,true,\"a\\\"b\\u0041\"]},\"f\":1.25e-2} \n";
	struct ar_json root, value, array; int32_t n; unsigned b; char text[16];
	assert(!ar_json_parse(&root, valid, strlen(valid)));
	assert(!ar_json_path(root, "a.b", &array));
	assert(!ar_json_index(array, 0, &value) && !ar_json_integer(value, &n) && n == INT32_MIN);
	assert(!ar_json_index(array, 1, &value) && !ar_json_boolean(value, &b) && b == 1);
	assert(!ar_json_index(array, 2, &value) && !ar_json_string(value, text, sizeof text) && !strcmp(text, "a\"bA"));
	assert(ar_json_equal(value, "a\"bA") && !ar_json_equal(value, "a"));
	assert(ar_json_index(array, 3, &value) == -2 && ar_json_path(root, "a.c", &value) == -2);
	assert(!ar_json_path(root, "f", &value) && ar_json_integer(value, &n) == -22);
	const char *bad[] = {"", "{", "[1,]", "{\"a\":}", "{\"a\" 1}", "{\"a\":1,}",
		"true x", "01", "-", "1.", "1e+", "\"\\x\"", "\"\\u12zz\"", "\"line\nfeed\"", "[1 2]"};
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) { assert(ar_json_parse(&root, bad[i], strlen(bad[i])) == -22); }
	const char *overflow[] = {"2147483648", "-2147483649", "99999999999999999"};
	for (unsigned i = 0; i < 3; i++) { assert(!ar_json_parse(&root, overflow[i], strlen(overflow[i])) && ar_json_integer(root, &n) == -22); }
	const char duplicate[] = "{\"a\":1,\"\\u0061\":2}";
	assert(!ar_json_parse(&root, duplicate, sizeof duplicate - 1) && ar_json_member(root, "a", &value) == -22);
	char deep[41]; memset(deep, '[', 20); memset(deep + 20, ']', 20); deep[40] = 0;
	assert(ar_json_parse(&root, deep, 40) == -22);
	for (size_t i = 1; i < strlen(valid) - 3; i++) { assert(ar_json_parse(&root, valid, i) == -22); }
	const char overlay[] = "\xef\xbb\xbf {\"text\":\"}\\\"{\",\"nested\":{}}obsolete bytes";
	size_t consumed = 0;
	assert(!ar_json_object_prefix(&root,overlay,sizeof overlay-1,&consumed));
	assert(!memcmp(overlay+consumed,"obsolete",8) && root.data == overlay+4);
	assert(!ar_json_parse(&value,root.data,root.length));
	assert(ar_json_parse(&value,overlay,sizeof overlay-1)==-22);
	assert(ar_json_object_prefix(&root,overlay,consumed-1,&consumed)==-22);
	assert(ar_json_object_prefix(&root,"{\"x\":\0}",8,&consumed)==-22);
	assert(ar_json_object_prefix(&root,"[]{}",4,&consumed)==-22);
	puts("bounded JSON spans, escapes, nesting, numeric limits and duplicate field rejection passed");
}
