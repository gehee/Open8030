/* SPDX-License-Identifier: MIT */
#include "ar_json.h"
#include <string.h>

static unsigned space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static size_t ws(struct ar_json s, size_t p) { while (p < s.length && space(s.data[p])) { p++; } return p; }
static int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* Decode a JSON string character. Field names/settings are ASCII; non-ASCII
 * values remain valid JSON but never compare equal to supported ASCII names. */
static int character(struct ar_json s, size_t *p, unsigned *out)
{
	if (*p >= s.length) { return -22; }
	unsigned c = (unsigned char)s.data[(*p)++];
	if (c < 32 || c == '"') { return -22; }
	if (c != '\\') { *out = c; return 0; }
	if (*p >= s.length) { return -22; }
	c = (unsigned char)s.data[(*p)++];
	const char *esc = "\"\\/bfnrt", *decoded = "\"\\/\b\f\n\r\t";
	for (unsigned i = 0; esc[i]; i++) { if (c == (unsigned)esc[i]) { *out = decoded[i]; return 0; } }
	if (c != 'u' || s.length - *p < 4) { return -22; }
	unsigned value = 0;
	for (unsigned i = 0; i < 4; i++) { int digit = hex(s.data[(*p)++]); if (digit < 0) { return -22; } value = value * 16 + digit; }
	*out = value; return 0;
}
static int string_end(struct ar_json s, size_t *p)
{
	if (*p >= s.length || s.data[(*p)++] != '"') { return -22; }
	while (*p < s.length && s.data[*p] != '"') { unsigned c; if (character(s, p, &c)) { return -22; } }
	if (*p == s.length) { return -22; }
	(*p)++; return 0;
}
static int skip(struct ar_json s, size_t *p, unsigned depth)
{
	*p = ws(s, *p);
	if (*p >= s.length || depth > 16) { return -22; }
	char c = s.data[*p];
	if (c == '"') { return string_end(s, p); }
	if (c == '{' || c == '[') {
		char end = c == '{' ? '}' : ']'; (*p)++; *p = ws(s, *p);
		if (*p < s.length && s.data[*p] == end) { (*p)++; return 0; }
		for (;;) {
			if (c == '{') {
				if (string_end(s, p)) { return -22; } *p = ws(s, *p);
				if (*p == s.length || s.data[(*p)++] != ':') { return -22; }
			}
			if (skip(s, p, depth + 1)) { return -22; } *p = ws(s, *p);
			if (*p == s.length) { return -22; }
			if (s.data[*p] == end) { (*p)++; return 0; }
			if (s.data[(*p)++] != ',') { return -22; } *p = ws(s, *p);
		}
	}
	for (unsigned i = 0; i < 3; i++) {
		const char *word = i == 0 ? "true" : i == 1 ? "false" : "null"; size_t n = strlen(word);
		if (s.length - *p >= n && !memcmp(s.data + *p, word, n)) { *p += n; return 0; }
	}
	if (c == '-') { (*p)++; }
	if (*p == s.length || s.data[*p] < '0' || s.data[*p] > '9') { return -22; }
	if (s.data[(*p)++] != '0') { while (*p < s.length && s.data[*p] >= '0' && s.data[*p] <= '9') { (*p)++; } }
	if (*p < s.length && s.data[*p] == '.') {
		size_t start = ++*p; while (*p < s.length && s.data[*p] >= '0' && s.data[*p] <= '9') { (*p)++; }
		if (*p == start) { return -22; }
	}
	if (*p < s.length && (s.data[*p] == 'e' || s.data[*p] == 'E')) {
		(*p)++; if (*p < s.length && (s.data[*p] == '+' || s.data[*p] == '-')) { (*p)++; }
		size_t start = *p; while (*p < s.length && s.data[*p] >= '0' && s.data[*p] <= '9') { (*p)++; }
		if (*p == start) { return -22; }
	}
	return 0;
}
int ar_json_parse(struct ar_json *out, const char *data, size_t length)
{
	if (!out || !data || !length || length > 16384) { return -22; }
	struct ar_json s = {data, length}; size_t start = ws(s, 0), end = start;
	if (skip(s, &end, 0) || ws(s, end) != length) { return -22; }
	*out = (struct ar_json){data + start, end - start}; return 0;
}
int ar_json_object_prefix(struct ar_json *out, const char *data, size_t capacity, size_t *consumed)
{
	if (!out || !data || !consumed || !capacity || capacity > 16384) { return -22; }
	struct ar_json s = {data, capacity};
	size_t start = capacity >= 3 && !memcmp(data, "\xef\xbb\xbf", 3) ? 3 : 0;
	start = ws(s, start); size_t end = start;
	if (start == capacity || data[start] != '{' || skip(s, &end, 0)) { return -22; }
	*out = (struct ar_json){data + start, end - start}; *consumed = end; return 0;
}
int ar_json_string(struct ar_json s, char *out, size_t capacity)
{
	if (!s.length || s.data[0] != '"' || !out || !capacity) { return -22; }
	size_t p = 1, n = 0;
	while (p < s.length && s.data[p] != '"') {
		unsigned c; if (character(s, &p, &c) || !c || c > 127 || n + 1 >= capacity) { return -22; }
		out[n++] = c;
	}
	if (p + 1 != s.length) { return -22; }
	out[n] = 0; return 0;
}
int ar_json_equal(struct ar_json s, const char *text)
{
	if (!s.length || s.data[0] != '"' || !text) { return 0; }
	size_t p = 1, n = 0;
	while (p < s.length && s.data[p] != '"') {
		unsigned c; if (character(s, &p, &c) || !text[n] || c != (unsigned char)text[n++]) { return 0; }
	}
	return p + 1 == s.length && !text[n];
}
int ar_json_member(struct ar_json s, const char *key, struct ar_json *out)
{
	if (!out || !key || !s.length || s.data[0] != '{') { return -22; }
	size_t p = ws(s, 1); unsigned found = 0; struct ar_json value = {0};
	while (p < s.length && s.data[p] != '}') {
		size_t start = p; if (string_end(s, &p)) { return -22; }
		unsigned match = ar_json_equal((struct ar_json){s.data + start, p - start}, key);
		p = ws(s, p); if (p == s.length || s.data[p++] != ':') { return -22; }
		p = ws(s, p); start = p; if (skip(s, &p, 0)) { return -22; }
		if (match) { if (found++) { return -22; } value = (struct ar_json){s.data + start, p - start}; }
		p = ws(s, p); if (p < s.length && s.data[p] == ',') { p = ws(s, p + 1); } else { break; }
	}
	if (!found) { return -2; } *out = value; return 0;
}
int ar_json_path(struct ar_json s, const char *path, struct ar_json *out)
{
	if (!path || !out) { return -22; }
	while (*path) {
		char key[48]; size_t n = 0;
		while (*path && *path != '.') { if (n + 1 == sizeof key) { return -22; } key[n++] = *path++; }
		key[n] = 0; int error = ar_json_member(s, key, &s); if (error) { return error; }
		if (*path) { path++; if (!*path) { return -22; } }
	}
	*out = s; return 0;
}
int ar_json_index(struct ar_json s, unsigned index, struct ar_json *out)
{
	if (!out || !s.length || s.data[0] != '[') { return -22; }
	size_t p = ws(s, 1);
	for (unsigned i = 0; p < s.length && s.data[p] != ']'; i++) {
		size_t start = p; if (skip(s, &p, 0)) { return -22; }
		if (i == index) { *out = (struct ar_json){s.data + start, p - start}; return 0; }
		p = ws(s, p); if (p < s.length && s.data[p] == ',') { p = ws(s, p + 1); } else { break; }
	}
	return -2;
}
int ar_json_integer(struct ar_json s, int32_t *out)
{
	if (!out || !s.length) { return -22; }
	size_t p = s.data[0] == '-'; unsigned negative = p; uint32_t value = 0;
	if (p == s.length) { return -22; }
	for (; p < s.length; p++) {
		unsigned digit = (unsigned char)s.data[p] - '0'; uint32_t limit = negative ? UINT32_C(2147483648) : INT32_MAX;
		if (digit > 9 || value > (limit - digit) / 10) { return -22; } value = value * 10 + digit;
	}
	*out = negative ? (int32_t)(-((int64_t)value)) : (int32_t)value; return 0;
}
int ar_json_boolean(struct ar_json s, unsigned *out)
{
	if (!out) { return -22; }
	if (s.length == 4 && !memcmp(s.data, "true", 4)) { *out = 1; return 0; }
	if (s.length == 5 && !memcmp(s.data, "false", 5)) { *out = 0; return 0; }
	return -22;
}
