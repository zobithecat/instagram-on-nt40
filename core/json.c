/* core/json.c — see json.h. Recursive-descent JSON DOM, no floating point. */
#include "json.h"
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 128

struct JsonValue {
    JsonType     type;
    char        *str;   /* JSON_STR decoded text / JSON_NUM raw text (NUL-term) */
    int          slen;
    int          boolean;
    int          count;      /* ARR/OBJ element count */
    char       **keys;       /* OBJ keys (decoded, NUL-terminated) */
    JsonValue  **vals;       /* ARR/OBJ values */
};

typedef struct { const char *p, *end; int depth; } P;

static JsonValue *parse_value(P *x);

static void skip_ws(P *x) {
    while (x->p < x->end) {
        char c = *x->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') x->p++;
        else break;
    }
}

static JsonValue *node(JsonType t) {
    JsonValue *v = (JsonValue *)malloc(sizeof(JsonValue));
    if (v) { memset(v, 0, sizeof(*v)); v->type = t; }
    return v;
}

/* Decode a JSON string starting at the opening quote. On success advances x->p
 * past the closing quote and returns a malloc'd NUL-terminated buffer (*len set
 * to the decoded byte length); NULL on error. */
static char *parse_string_raw(P *x, int *len) {
    if (x->p >= x->end || *x->p != '"') return NULL;
    const char *s = ++x->p;                 /* first char after opening quote */
    /* decoded length <= raw span length, so size the buffer to the raw span */
    const char *scan = s;
    while (scan < x->end && *scan != '"') {
        if (*scan == '\\') scan++;           /* skip escaped char in the scan */
        scan++;
    }
    if (scan >= x->end) return NULL;         /* unterminated */
    char *out = (char *)malloc((size_t)(scan - s) + 1);
    if (!out) return NULL;
    char *o = out;

    while (x->p < x->end && *x->p != '"') {
        unsigned char c = (unsigned char)*x->p;
        if (c != '\\') { *o++ = (char)c; x->p++; continue; }
        x->p++;
        if (x->p >= x->end) { free(out); return NULL; }
        char e = *x->p++;
        switch (e) {
        case '"': *o++ = '"'; break;
        case '\\': *o++ = '\\'; break;
        case '/': *o++ = '/'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'n': *o++ = '\n'; break;
        case 'r': *o++ = '\r'; break;
        case 't': *o++ = '\t'; break;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4; i++) {
                if (x->p >= x->end) { free(out); return NULL; }
                char h = *x->p++;
                cp <<= 4;
                if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                else { free(out); return NULL; }
            }
            /* surrogate pair -> astral codepoint */
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (x->end - x->p >= 2 && x->p[0] == '\\' && x->p[1] == 'u') {
                    x->p += 2;
                    unsigned lo = 0;
                    for (int i = 0; i < 4; i++) {
                        if (x->p >= x->end) { free(out); return NULL; }
                        char h = *x->p++;
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                        else { free(out); return NULL; }
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
            }
            /* encode cp as UTF-8 */
            if (cp < 0x80) {
                *o++ = (char)cp;
            } else if (cp < 0x800) {
                *o++ = (char)(0xC0 | (cp >> 6));
                *o++ = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                *o++ = (char)(0xE0 | (cp >> 12));
                *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                *o++ = (char)(0x80 | (cp & 0x3F));
            } else {
                *o++ = (char)(0xF0 | (cp >> 18));
                *o++ = (char)(0x80 | ((cp >> 12) & 0x3F));
                *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                *o++ = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: free(out); return NULL;
        }
    }
    if (x->p >= x->end) { free(out); return NULL; }
    x->p++; /* closing quote */
    *o = 0;
    if (len) *len = (int)(o - out);
    return out;
}

static JsonValue *parse_string(P *x) {
    int len = 0;
    char *s = parse_string_raw(x, &len);
    if (!s) return NULL;
    JsonValue *v = node(JSON_STR);
    if (!v) { free(s); return NULL; }
    v->str = s;
    v->slen = len;
    return v;
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static JsonValue *parse_number(P *x) {
    const char *s = x->p;
    if (x->p < x->end && *x->p == '-') x->p++;
    while (x->p < x->end && is_digit(*x->p)) x->p++;
    if (x->p < x->end && *x->p == '.') { x->p++; while (x->p < x->end && is_digit(*x->p)) x->p++; }
    if (x->p < x->end && (*x->p == 'e' || *x->p == 'E')) {
        x->p++;
        if (x->p < x->end && (*x->p == '+' || *x->p == '-')) x->p++;
        while (x->p < x->end && is_digit(*x->p)) x->p++;
    }
    int n = (int)(x->p - s);
    if (n == 0 || (n == 1 && s[0] == '-')) return NULL;
    JsonValue *v = node(JSON_NUM);
    if (!v) return NULL;
    v->str = (char *)malloc((size_t)n + 1);
    if (!v->str) { free(v); return NULL; }
    memcpy(v->str, s, (size_t)n);
    v->str[n] = 0;
    v->slen = n;
    return v;
}

static JsonValue *parse_lit(P *x, const char *lit, JsonType t, int boolean) {
    int n = (int)strlen(lit);
    if (x->end - x->p < n || memcmp(x->p, lit, (size_t)n) != 0) return NULL;
    x->p += n;
    JsonValue *v = node(t);
    if (v) v->boolean = boolean;
    return v;
}

/* grow the vals[] (and keys[] for objects) arrays by appending one slot */
static int push_child(JsonValue *parent, char *key, JsonValue *child) {
    int n = parent->count;
    JsonValue **nv = (JsonValue **)realloc(parent->vals, sizeof(JsonValue *) * (size_t)(n + 1));
    if (!nv) return -1;
    parent->vals = nv;
    parent->vals[n] = child;
    if (key) {
        char **nk = (char **)realloc(parent->keys, sizeof(char *) * (size_t)(n + 1));
        if (!nk) return -1;
        parent->keys = nk;
        parent->keys[n] = key;
    }
    parent->count = n + 1;
    return 0;
}

static JsonValue *parse_array(P *x) {
    x->p++; /* '[' */
    if (++x->depth > JSON_MAX_DEPTH) return NULL;
    JsonValue *arr = node(JSON_ARR);
    if (!arr) return NULL;
    skip_ws(x);
    if (x->p < x->end && *x->p == ']') { x->p++; x->depth--; return arr; }
    for (;;) {
        JsonValue *item = parse_value(x);
        if (!item || push_child(arr, NULL, item) != 0) { if (item) json_free(item); json_free(arr); return NULL; }
        skip_ws(x);
        if (x->p >= x->end) { json_free(arr); return NULL; }
        if (*x->p == ',') { x->p++; continue; }
        if (*x->p == ']') { x->p++; break; }
        json_free(arr); return NULL;
    }
    x->depth--;
    return arr;
}

static JsonValue *parse_object(P *x) {
    x->p++; /* '{' */
    if (++x->depth > JSON_MAX_DEPTH) return NULL;
    JsonValue *obj = node(JSON_OBJ);
    if (!obj) return NULL;
    skip_ws(x);
    if (x->p < x->end && *x->p == '}') { x->p++; x->depth--; return obj; }
    for (;;) {
        skip_ws(x);
        int klen = 0;
        char *key = parse_string_raw(x, &klen);
        if (!key) { json_free(obj); return NULL; }
        skip_ws(x);
        if (x->p >= x->end || *x->p != ':') { free(key); json_free(obj); return NULL; }
        x->p++;
        JsonValue *val = parse_value(x);
        if (!val || push_child(obj, key, val) != 0) {
            free(key); if (val) json_free(val); json_free(obj); return NULL;
        }
        skip_ws(x);
        if (x->p >= x->end) { json_free(obj); return NULL; }
        if (*x->p == ',') { x->p++; continue; }
        if (*x->p == '}') { x->p++; break; }
        json_free(obj); return NULL;
    }
    x->depth--;
    return obj;
}

static JsonValue *parse_value(P *x) {
    skip_ws(x);
    if (x->p >= x->end) return NULL;
    char c = *x->p;
    switch (c) {
    case '{': return parse_object(x);
    case '[': return parse_array(x);
    case '"': return parse_string(x);
    case 't': return parse_lit(x, "true", JSON_BOOL, 1);
    case 'f': return parse_lit(x, "false", JSON_BOOL, 0);
    case 'n': return parse_lit(x, "null", JSON_NULL, 0);
    default:  return (c == '-' || is_digit(c)) ? parse_number(x) : NULL;
    }
}

JsonValue *json_parse(const char *text, int len) {
    if (!text || len <= 0) return NULL;
    P x = { text, text + len, 0 };
    JsonValue *v = parse_value(&x);
    if (!v) return NULL;
    skip_ws(&x);
    if (x.p != x.end) { json_free(v); return NULL; } /* trailing garbage */
    return v;
}

void json_free(JsonValue *v) {
    if (!v) return;
    if (v->str) free(v->str);
    for (int i = 0; i < v->count; i++) {
        if (v->keys && v->keys[i]) free(v->keys[i]);
        json_free(v->vals[i]);
    }
    free(v->keys);
    free(v->vals);
    free(v);
}

JsonType json_type(const JsonValue *v) { return v ? v->type : JSON_NULL; }

const JsonValue *json_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJ || !key) return NULL;
    for (int i = 0; i < obj->count; i++)
        if (obj->keys[i] && strcmp(obj->keys[i], key) == 0) return obj->vals[i];
    return NULL;
}

const JsonValue *json_at(const JsonValue *arr, int index) {
    if (!arr || arr->type != JSON_ARR || index < 0 || index >= arr->count) return NULL;
    return arr->vals[index];
}

int json_count(const JsonValue *v) {
    return (v && (v->type == JSON_ARR || v->type == JSON_OBJ)) ? v->count : 0;
}

const char *json_string(const JsonValue *v, int *out_len) {
    if (!v || v->type != JSON_STR) return NULL;
    if (out_len) *out_len = v->slen;
    return v->str;
}

long json_int(const JsonValue *v, long def) {
    if (!v || v->type != JSON_NUM || !v->str) return def;
    const char *s = v->str;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    long n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return neg ? -n : n;
}

int json_bool(const JsonValue *v, int def) {
    return (v && v->type == JSON_BOOL) ? v->boolean : def;
}

const char *json_get_str(const JsonValue *obj, const char *key) {
    return json_string(json_get(obj, key), NULL);
}

long json_get_int(const JsonValue *obj, const char *key, long def) {
    return json_int(json_get(obj, key), def);
}
