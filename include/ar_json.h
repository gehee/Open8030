/* SPDX-License-Identifier: MIT */
#ifndef AR_JSON_H
#define AR_JSON_H
#include <stddef.h>
#include <stdint.h>
/* Bounded immutable spans; no heap allocation or retained parser tree. */
struct ar_json { const char *data; size_t length; };
int ar_json_parse(struct ar_json *value, const char *data, size_t length);
/* Loader overlays can leave stale bytes after the first complete object.
 * Validate that object within capacity; accept optional UTF-8 BOM at byte0.
 * consumed ends at its closing brace, excluding trailing whitespace/old data. */
int ar_json_object_prefix(struct ar_json *value, const char *data, size_t capacity, size_t *consumed);
int ar_json_member(struct ar_json value, const char *key, struct ar_json *out);
int ar_json_path(struct ar_json value, const char *path, struct ar_json *out);
int ar_json_index(struct ar_json value, unsigned index, struct ar_json *out);
int ar_json_integer(struct ar_json value, int32_t *out);
int ar_json_boolean(struct ar_json value, unsigned *out);
int ar_json_string(struct ar_json value, char *out, size_t capacity);
int ar_json_equal(struct ar_json value, const char *text);
#endif
