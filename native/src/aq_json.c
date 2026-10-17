/*
 * aq_json.c - Small JSON reader/writer. See aq_json.h.
 * MIT License, Copyright (c) 2026 Temple Lin.
 */
#include "aq_json.h"

typedef struct { const char *p, *end; int depth; } jp;

static void skip_ws(jp *s) { while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')) s->p++; }
static aj *new_value(aj_type t) { aj *v = (aj *)aq_calloc(1, sizeof(aj)); if (v) v->type = t; return v; }

static int hex4(const char *p, uint32_t *out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return -1;
    }
    *out = v; return 0;
}
static void put_utf8(aq_buf *b, uint32_t cp) {
    char t[4]; int n;
    if (cp < 0x80) { t[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) { t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 63)); t[2] = (char)(0x80 | (cp & 63)); n = 3; }
    else { t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 63)); t[2] = (char)(0x80 | ((cp >> 6) & 63)); t[3] = (char)(0x80 | (cp & 63)); n = 4; }
    aq_buf_append(b, t, (size_t)n);
}
static char *parse_string(jp *s, size_t *len) {
    if (s->p >= s->end || *s->p != '"') return 0;
    s->p++;
    aq_buf b = {0};
    while (s->p < s->end && *s->p != '"') {
        char c = *s->p++;
        if ((unsigned char)c < 0x20) { aq_buf_free(&b); return 0; }
        if (c != '\\') { aq_buf_putc(&b, c); continue; }
        if (s->p >= s->end) { aq_buf_free(&b); return 0; }
        c = *s->p++;
        switch (c) {
        case '"': aq_buf_putc(&b, '"'); break;
        case '\\': aq_buf_putc(&b, '\\'); break;
        case '/': aq_buf_putc(&b, '/'); break;
        case 'b': aq_buf_putc(&b, '\b'); break;
        case 'f': aq_buf_putc(&b, '\f'); break;
        case 'n': aq_buf_putc(&b, '\n'); break;
        case 'r': aq_buf_putc(&b, '\r'); break;
        case 't': aq_buf_putc(&b, '\t'); break;
        case 'u': {
            uint32_t cp;
            if (s->end - s->p < 4 || hex4(s->p, &cp) < 0) { aq_buf_free(&b); return 0; }
            s->p += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                uint32_t lo;
                if (s->end - s->p >= 6 && s->p[0] == '\\' && s->p[1] == 'u' && hex4(s->p + 2, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                    s->p += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
            put_utf8(&b, cp);
            break;
        }
        default: aq_buf_free(&b); return 0;
        }
    }
    if (s->p >= s->end) { aq_buf_free(&b); return 0; }
    s->p++;
    aq_buf_putc(&b, 0);
    if (b.failed) { aq_buf_free(&b); return 0; }
    *len = b.len - 1;
    return (char *)b.data;
}
static aj *parse_value(jp *s);
static aj *parse_number(jp *s) {
    const char *p = s->p; int neg = 0; double v = 0;
    if (p < s->end && *p == '-') { neg = 1; p++; }
    if (p >= s->end || *p < '0' || *p > '9') return 0;
    while (p < s->end && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    if (p < s->end && *p == '.') {
        p++; double scale = 0.1;
        if (p >= s->end || *p < '0' || *p > '9') return 0;
        while (p < s->end && *p >= '0' && *p <= '9') { v += (*p++ - '0') * scale; scale *= 0.1; }
    }
    if (p < s->end && (*p == 'e' || *p == 'E')) {
        p++; int eneg = 0, e = 0;
        if (p < s->end && (*p == '+' || *p == '-')) { eneg = *p == '-'; p++; }
        if (p >= s->end || *p < '0' || *p > '9') return 0;
        while (p < s->end && *p >= '0' && *p <= '9') { e = e * 10 + (*p++ - '0'); if (e > 400) e = 400; }
        while (e--) v = eneg ? v / 10 : v * 10;
    }
    aj *r = new_value(AJ_NUM); if (!r) return 0;
    r->num = neg ? -v : v; s->p = p;
    return r;
}
static aj *parse_value(jp *s) {
    skip_ws(s);
    if (s->p >= s->end || ++s->depth > 64) return 0;
    aj *r = 0;
    char c = *s->p;
    if (c == '{') {
        s->p++; r = new_value(AJ_OBJ); if (!r) return 0;
        aj **tail = &r->child;
        skip_ws(s);
        if (s->p < s->end && *s->p == '}') { s->p++; s->depth--; return r; }
        for (;;) {
            skip_ws(s);
            size_t kl; char *key = parse_string(s, &kl);
            if (!key) goto fail;
            skip_ws(s);
            if (s->p >= s->end || *s->p != ':') { aq_free(key); goto fail; }
            s->p++;
            aj *v = parse_value(s);
            if (!v) { aq_free(key); goto fail; }
            v->key = key; *tail = v; tail = &v->next;
            skip_ws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == '}') { s->p++; break; }
            goto fail;
        }
    } else if (c == '[') {
        s->p++; r = new_value(AJ_ARR); if (!r) return 0;
        aj **tail = &r->child;
        skip_ws(s);
        if (s->p < s->end && *s->p == ']') { s->p++; s->depth--; return r; }
        for (;;) {
            aj *v = parse_value(s);
            if (!v) goto fail;
            *tail = v; tail = &v->next;
            skip_ws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == ']') { s->p++; break; }
            goto fail;
        }
    } else if (c == '"') {
        r = new_value(AJ_STR); if (!r) return 0;
        r->str = parse_string(s, &r->len);
        if (!r->str) goto fail;
    } else if (c == 't' && s->end - s->p >= 4 && aq_memcmp(s->p, "true", 4) == 0) { s->p += 4; r = new_value(AJ_BOOL); if (r) r->boolean = 1; }
    else if (c == 'f' && s->end - s->p >= 5 && aq_memcmp(s->p, "false", 5) == 0) { s->p += 5; r = new_value(AJ_BOOL); }
    else if (c == 'n' && s->end - s->p >= 4 && aq_memcmp(s->p, "null", 4) == 0) { s->p += 4; r = new_value(AJ_NULL); }
    else r = parse_number(s);
    if (r) s->depth--;
    return r;
fail:
    aj_free(r);
    return 0;
}
aj *aj_parse(const char *text, size_t n) {
    jp s = { text, text + n, 0 };
    aj *v = parse_value(&s);
    if (!v) return 0;
    skip_ws(&s);
    if (s.p != s.end) { aj_free(v); return 0; }
    return v;
}
void aj_free(aj *v) {
    while (v) {
        aj *next = v->next;
        aj_free(v->child);
        aq_free(v->str); aq_free(v->key); aq_free(v);
        v = next;
    }
}
aj *aj_get(const aj *obj, const char *key) {
    if (!obj || obj->type != AJ_OBJ) return 0;
    for (aj *c = obj->child; c; c = c->next) if (c->key && aq_strcmp(c->key, key) == 0) return c;
    return 0;
}
const char *aj_get_str(const aj *obj, const char *key, const char *def) {
    aj *v = aj_get(obj, key);
    return v && v->type == AJ_STR ? v->str : def;
}
long long aj_get_int(const aj *obj, const char *key, long long def) {
    aj *v = aj_get(obj, key);
    if (!v) return def;
    if (v->type == AJ_NUM) return (long long)v->num;
    if (v->type == AJ_BOOL) return v->boolean;
    if (v->type == AJ_STR) return aq_atoll(v->str, 0);
    return def;
}
double aj_get_num(const aj *obj, const char *key, double def) {
    aj *v = aj_get(obj, key);
    return v && v->type == AJ_NUM ? v->num : def;
}
int aj_get_bool(const aj *obj, const char *key, int def) {
    aj *v = aj_get(obj, key);
    if (!v) return def;
    if (v->type == AJ_BOOL) return v->boolean;
    if (v->type == AJ_NUM) return v->num != 0;
    return def;
}
int aj_count(const aj *arr) {
    int n = 0;
    if (arr && (arr->type == AJ_ARR || arr->type == AJ_OBJ)) for (aj *c = arr->child; c; c = c->next) n++;
    return n;
}

void aj_w_str(aq_buf *b, const char *s, size_t n) {
    static const char hex[] = "0123456789abcdef";
    aq_buf_reserve(b, n + 2);
    aq_buf_putc(b, '"');
    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = 0; char u[7];
        if (c == '"') esc = "\\\"";
        else if (c == '\\') esc = "\\\\";
        else if (c == '\n') esc = "\\n";
        else if (c == '\r') esc = "\\r";
        else if (c == '\t') esc = "\\t";
        else if (c < 0x20 || c == 0x7f) { u[0] = '\\'; u[1] = 'u'; u[2] = '0'; u[3] = '0'; u[4] = hex[c >> 4]; u[5] = hex[c & 15]; u[6] = 0; esc = u; }
        if (esc) {
            aq_buf_append(b, s + start, i - start);
            aq_buf_puts(b, esc);
            start = i + 1;
        }
    }
    aq_buf_append(b, s + start, n - start);
    aq_buf_putc(b, '"');
}
void aj_w_cstr(aq_buf *b, const char *s) { aj_w_str(b, s ? s : "", s ? aq_strlen(s) : 0); }
void aj_w_key(aq_buf *b, const char *key, int *first) {
    if (first && !*first) aq_buf_putc(b, ',');
    if (first) *first = 0;
    aj_w_cstr(b, key);
    aq_buf_putc(b, ':');
}
