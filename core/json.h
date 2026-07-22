/* core/json.h — a small recursive-descent JSON DOM parser, OS-independent.
 *
 * Built to parse Instagram Graph API responses on NT4: no floating point (JSON
 * numbers are kept as their raw text; read them as int on demand), no libc
 * beyond malloc/free/mem*, so it drops into the freestanding NT4 build. Strings
 * are decoded (escapes + \uXXXX -> UTF-8) into owned, NUL-terminated buffers. */
#ifndef CORE_JSON_H
#define CORE_JSON_H

typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ
} JsonType;

typedef struct JsonValue JsonValue;

/* Parse `len` bytes of JSON text. Returns the root value (caller json_free's) or
 * NULL on syntax error / allocation failure / excessive nesting. */
JsonValue *json_parse(const char *text, int len);
void json_free(JsonValue *v);

JsonType    json_type(const JsonValue *v);

/* Object member by key (JSON_OBJ only), or NULL if absent / not an object. */
const JsonValue *json_get(const JsonValue *obj, const char *key);
/* Array element by index (JSON_ARR only), or NULL if out of range. */
const JsonValue *json_at(const JsonValue *arr, int index);
/* Element count for JSON_ARR / JSON_OBJ, else 0. */
int json_count(const JsonValue *v);

/* Decoded string (NUL-terminated) for JSON_STR, else NULL. *out_len optional. */
const char *json_string(const JsonValue *v, int *out_len);
/* Integer value of a JSON_NUM (truncates any fraction/exponent-free form),
 * or `def` if v is not a number. */
long json_int(const JsonValue *v, long def);
/* 1/0 for JSON_BOOL, else `def`. */
int  json_bool(const JsonValue *v, int def);

/* Convenience: obj.key as string / int (NULL/def if missing or wrong type). */
const char *json_get_str(const JsonValue *obj, const char *key);
long        json_get_int(const JsonValue *obj, const char *key, long def);

#endif /* CORE_JSON_H */
