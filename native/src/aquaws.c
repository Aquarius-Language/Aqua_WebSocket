/*
 * aquaws.c - Aqua_WebSocket native engine (RFC 6455 client and server).
 *
 * Registered through the Aquarius native SDK. Internal engine commands
 * take JSON requests and responses; their buffers are private. The
 * engine has no threads: aqws_poll() performs all non-blocking socket work
 * for one context and returns queued events.
 *
 * MIT License, Copyright (c) 2026 Temple Lin.
 */
#include "aq_plat.h"
#include "aq_json.h"
#include <aqua/aqua.h>

#define AQWS_VERSION "2.0.0"
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define MAX_HEADER_BYTES 16384
#define DEFAULT_MAX_MESSAGE (512 * 1024)
#define MAX_BUFFERED_OUT (16 * 1024 * 1024)
#define READ_CHUNK 65536

enum { ST_CONNECTING = 1, ST_HANDSHAKE, ST_OPEN, ST_CLOSING, ST_CLOSED };

typedef struct ws_event { struct ws_event *next; char *json; size_t len; } ws_event;

typedef struct ws_ctx {
    int id;
    ws_event *head, *tail;
    int queued;
} ws_ctx;

typedef struct ws_server {
    int id, ctx;
    aq_sock sock;
    aq_addr addr;
    char *path;            /* required request path, "" = any */
    char **origins; int norigins;
    char **protocols; int nprotocols;
    int max_message, max_connections, ping_interval;
    int connections;
} ws_server;

typedef struct ws_conn {
    int id, ctx, server;   /* server == 0 for client connections */
    aq_sock sock;
    int state;
    int is_client;
    aq_buf in, out, msg;
    int msg_opcode;        /* 0 = no fragmented message in progress */
    uint64_t created, last_rx, last_ping, close_deadline, handshake_deadline;
    int ping_outstanding;
    int max_message, ping_interval;
    int close_sent, close_received, close_code, close_emitted;
    char *close_reason;
    char remote[64];
    char *path, *query, *origin, *protocol;
    /* client only */
    char *host_header, *request_protocols, *extra_headers;
    char accept_expected[32];
} ws_conn;

static ws_ctx **g_ctx; static int g_nctx;
static ws_server **g_srv; static int g_nsrv;
static ws_conn **g_conn; static int g_nconn;
static int g_next_id = 1;

/* ------------------------------------------------------------------ */
static int new_id(void) { int id = g_next_id++; if (g_next_id > 0x3fffffff) g_next_id = 1; return id; }
static void *grow_push(void *arr, int *n, void *item) {
    void **a = (void **)aq_realloc(arr, sizeof(void *) * (size_t)(*n + 1));
    if (!a) return arr;
    a[(*n)++] = item;
    return a;
}
static ws_ctx *find_ctx(int id) { for (int i = 0; i < g_nctx; i++) if (g_ctx[i]->id == id) return g_ctx[i]; return 0; }
static ws_server *find_srv(int ctx, int id) { for (int i = 0; i < g_nsrv; i++) if (g_srv[i]->id == id && g_srv[i]->ctx == ctx) return g_srv[i]; return 0; }
static ws_conn *find_conn(int ctx, int id) { for (int i = 0; i < g_nconn; i++) if (g_conn[i]->id == id && g_conn[i]->ctx == ctx) return g_conn[i]; return 0; }

static void push_event(int ctxid, aq_buf *b) {
    ws_ctx *c = find_ctx(ctxid);
    if (!c || b->failed) { aq_buf_free(b); return; }
    ws_event *e = (ws_event *)aq_calloc(1, sizeof *e);
    if (!e) { aq_buf_free(b); return; }
    e->json = (char *)b->data; e->len = b->len;
    b->data = 0; b->len = b->cap = 0;
    if (c->tail) c->tail->next = e; else c->head = e;
    c->tail = e; c->queued++;
}

static void emit_error(int ctx, int conn, int server, const char *msg) {
    aq_buf b = {0};
    aq_buf_printf(&b, "{\"type\":\"error\",\"conn\":%d,\"server\":%d,\"message\":", conn, server);
    aj_w_cstr(&b, msg);
    aq_buf_putc(&b, '}');
    push_event(ctx, &b);
}

static void free_strlist(char **l, int n) { for (int i = 0; i < n; i++) aq_free(l[i]); aq_free(l); }
static char **read_strlist(const aj *arr, int *count) {
    *count = 0;
    if (!arr || arr->type != AJ_ARR) return 0;
    int n = aj_count(arr);
    char **l = (char **)aq_calloc((size_t)(n ? n : 1), sizeof(char *));
    if (!l) return 0;
    for (aj *v = arr->child; v; v = v->next) if (v->type == AJ_STR) l[(*count)++] = aq_strdup(v->str);
    return l;
}

/* ------------------------------------------------------------------ */
static int valid_utf8(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (c < 0x80) { i++; continue; }
        int len; uint32_t cp;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return 0;
        if (i + (size_t)len > n) return 0;
        for (int k = 1; k < len; k++) { if ((s[i + k] & 0xC0) != 0x80) return 0; cp = (cp << 6) | (s[i + k] & 0x3F); }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        i += (size_t)len;
    }
    return 1;
}

static void flush_out(ws_conn *c) {
    while (c->out.len && c->sock != AQ_BAD_SOCK) {
        int n = aq_sock_send(c->sock, c->out.data, c->out.len > 1 << 20 ? 1 << 20 : (int)c->out.len);
        if (n > 0) { aq_buf_consume(&c->out, (size_t)n); continue; }
        if (n == AQ_IO_AGAIN) return;
        /* hard error: mark closed */
        aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK;
        if (!c->close_code) c->close_code = 1006;
        c->state = ST_CLOSED;
        return;
    }
}

static void send_frame(ws_conn *c, int opcode, const uint8_t *data, size_t n) {
    if (c->sock == AQ_BAD_SOCK || c->state == ST_CLOSED) return;
    uint8_t hdr[14]; size_t h = 0;
    hdr[h++] = (uint8_t)(0x80 | opcode);
    uint8_t maskbit = c->is_client ? 0x80 : 0;
    if (n < 126) hdr[h++] = (uint8_t)(maskbit | n);
    else if (n < 65536) { hdr[h++] = (uint8_t)(maskbit | 126); hdr[h++] = (uint8_t)(n >> 8); hdr[h++] = (uint8_t)n; }
    else { hdr[h++] = (uint8_t)(maskbit | 127); for (int i = 7; i >= 0; i--) hdr[h++] = (uint8_t)((uint64_t)n >> (8 * i)); }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (c->is_client) { aq_random(mask, 4); aq_memcpy(hdr + h, mask, 4); h += 4; }
    aq_buf_reserve(&c->out, h + n);
    if (c->out.failed) return;
    aq_buf_append(&c->out, hdr, h);
    if (c->is_client) {
        uint8_t *dst = c->out.data + c->out.len;
        for (size_t i = 0; i < n; i++) dst[i] = data[i] ^ mask[i & 3];
        c->out.len += n;
    } else aq_buf_append(&c->out, data, n);
    flush_out(c);
    if (c->out.len > MAX_BUFFERED_OUT) {
        /* slow consumer: drop the connection instead of growing without bound */
        aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED;
        if (!c->close_code) { c->close_code = 1008; aq_free(c->close_reason); c->close_reason = aq_strdup("send buffer overflow"); }
    }
}

static void start_close(ws_conn *c, int code, const char *reason) {
    if (c->state == ST_CLOSED) return;
    if (c->state != ST_OPEN && c->state != ST_CLOSING) {
        aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED;
        if (!c->close_code) c->close_code = code;
        return;
    }
    if (!c->close_sent) {
        uint8_t payload[125]; size_t n = 0;
        if (code > 0) {
            payload[n++] = (uint8_t)(code >> 8); payload[n++] = (uint8_t)code;
            size_t rl = reason ? aq_strlen(reason) : 0; if (rl > 120) rl = 120;
            if (rl) { aq_memcpy(payload + n, reason, rl); n += rl; }
        }
        send_frame(c, 0x8, payload, n);
        c->close_sent = 1;
    }
    if (!c->close_code) { c->close_code = code; aq_free(c->close_reason); c->close_reason = aq_strdup(reason ? reason : ""); }
    if (c->state != ST_CLOSED) c->state = ST_CLOSING;
    c->close_deadline = aq_now_ms() + 2000;
}

static void deliver_message(ws_conn *c, int opcode, const uint8_t *data, size_t n) {
    aq_buf b = {0};
    if (opcode == 1) {
        if (!valid_utf8(data, n)) { start_close(c, 1007, "invalid UTF-8"); return; }
        aq_buf_reserve(&b, n + 64);
        aq_buf_printf(&b, "{\"type\":\"message\",\"conn\":%d,\"server\":%d,\"text\":", c->id, c->server);
        aj_w_str(&b, (const char *)data, n);
        aq_buf_putc(&b, '}');
    } else {
        aq_buf_reserve(&b, n * 4 + 80);
        aq_buf_printf(&b, "{\"type\":\"binary\",\"conn\":%d,\"server\":%d,\"bytes\":[", c->id, c->server);
        for (size_t i = 0; i < n; i++) aq_buf_printf(&b, i ? ",%u" : "%u", data[i]);
        aq_buf_puts(&b, "]}");
    }
    push_event(c->ctx, &b);
}

/* Parse as many complete frames from c->in as possible. */
static void process_frames(ws_conn *c) {
    for (;;) {
        if (c->state != ST_OPEN && c->state != ST_CLOSING) return;
        size_t avail = c->in.len; uint8_t *p = c->in.data;
        if (avail < 2) return;
        int fin = p[0] & 0x80, rsv = p[0] & 0x70, opcode = p[0] & 0x0F;
        int masked = p[1] & 0x80; uint64_t len = p[1] & 0x7F; size_t h = 2;
        if (len == 126) { if (avail < 4) return; len = ((uint64_t)p[2] << 8) | p[3]; h = 4; }
        else if (len == 127) { if (avail < 10) return; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i]; h = 10; }
        if (rsv) { start_close(c, 1002, "reserved bits set"); return; }
        if (masked != (c->is_client ? 0 : 0x80)) { start_close(c, 1002, c->is_client ? "masked server frame" : "unmasked client frame"); return; }
        if (len > (uint64_t)c->max_message) { start_close(c, 1009, "message too big"); return; }
        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked) { if (avail < h + 4) return; aq_memcpy(mask, p + h, 4); h += 4; }
        if (avail < h + len) return;
        uint8_t *payload = p + h;
        if (masked) for (uint64_t i = 0; i < len; i++) payload[i] ^= mask[i & 3];
        c->last_rx = aq_now_ms(); c->ping_outstanding = 0;
        if (opcode >= 8) {
            if (!fin || len > 125) { start_close(c, 1002, "invalid control frame"); return; }
            if (opcode == 0x9) { if (!c->close_sent) send_frame(c, 0xA, payload, (size_t)len); }
            else if (opcode == 0xA) { /* pong */ }
            else if (opcode == 0x8) {
                int code = 1005; char reason[128] = {0};
                if (len >= 2) {
                    code = (payload[0] << 8) | payload[1];
                    size_t rl = (size_t)len - 2; if (rl > 123) rl = 123;
                    aq_memcpy(reason, payload + 2, rl);
                    if (!valid_utf8((const uint8_t *)reason, rl)) reason[0] = 0;
                } else if (len == 1) code = 1002;
                c->close_received = 1;
                if (!c->close_code || !c->close_sent) { c->close_code = code; aq_free(c->close_reason); c->close_reason = aq_strdup(reason); }
                if (!c->close_sent) {
                    if (code == 1005) { send_frame(c, 0x8, NULL, 0); c->close_sent = 1; }
                    else start_close(c, code, reason);
                }
                c->state = ST_CLOSING;
                c->close_deadline = aq_now_ms() + (c->is_client ? 2000 : 200);
            } else { start_close(c, 1002, "unknown opcode"); return; }
        } else if (opcode == 0x1 || opcode == 0x2) {
            if (c->msg_opcode) { start_close(c, 1002, "unexpected data frame"); return; }
            if (fin) { if (c->state == ST_OPEN) deliver_message(c, opcode, payload, (size_t)len); }
            else { c->msg.len = 0; aq_buf_append(&c->msg, payload, (size_t)len); c->msg_opcode = opcode; }
        } else if (opcode == 0x0) {
            if (!c->msg_opcode) { start_close(c, 1002, "unexpected continuation"); return; }
            if (c->msg.len + len > (uint64_t)c->max_message) { start_close(c, 1009, "message too big"); return; }
            aq_buf_append(&c->msg, payload, (size_t)len);
            if (fin) {
                if (c->state == ST_OPEN) deliver_message(c, c->msg_opcode, c->msg.data, c->msg.len);
                c->msg_opcode = 0; c->msg.len = 0;
            }
        } else { start_close(c, 1002, "unknown opcode"); return; }
        aq_buf_consume(&c->in, h + (size_t)len);
    }
}

/* ---------------- HTTP handshake helpers ---------------- */
static const char *find_header_end(const aq_buf *b) {
    if (b->len < 4) return 0;
    for (size_t i = 0; i + 3 < b->len; i++)
        if (b->data[i] == '\r' && b->data[i + 1] == '\n' && b->data[i + 2] == '\r' && b->data[i + 3] == '\n') return (const char *)b->data + i;
    return 0;
}
/* Copy header value (trimmed) for name into out. Returns 1 if found. */
static int header_value(const char *head, size_t len, const char *name, char *out, size_t cap) {
    size_t nl = aq_strlen(name);
    const char *p = head, *end = head + len;
    /* skip request/status line */
    while (p < end && *p != '\n') p++;
    if (p < end) p++;
    while (p < end) {
        const char *line = p;
        while (p < end && *p != '\n') p++;
        const char *le = p; if (le > line && le[-1] == '\r') le--;
        if (p < end) p++;
        if ((size_t)(le - line) > nl && line[nl] == ':' && aq_strncasecmp(line, name, nl) == 0) {
            const char *v = line + nl + 1;
            while (v < le && (*v == ' ' || *v == '\t')) v++;
            const char *ve = le; while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
            size_t vl = (size_t)(ve - v); if (vl >= cap) vl = cap - 1;
            aq_memcpy(out, v, vl); out[vl] = 0;
            return 1;
        }
    }
    return 0;
}
static int token_list_contains(const char *list, const char *token) {
    size_t tl = aq_strlen(token);
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        const char *s = p; while (*p && *p != ',') p++;
        const char *e = p; while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        if ((size_t)(e - s) == tl && aq_strncasecmp(s, token, tl) == 0) return 1;
    }
    return 0;
}
static void compute_accept(const char *key, char out[32]) {
    aq_sha1 s; uint8_t d[20];
    aq_sha1_init(&s); aq_sha1_update(&s, key, aq_strlen(key)); aq_sha1_update(&s, WS_GUID, sizeof WS_GUID - 1); aq_sha1_final(&s, d);
    aq_base64_encode(d, 20, out);
}
static void http_reject(ws_conn *c, int status, const char *text) {
    const char *reason = status == 400 ? "Bad Request" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 426 ? "Upgrade Required" : status == 503 ? "Service Unavailable" : "Error";
    aq_buf_printf(&c->out, "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n%s\r\n%s",
                  status, reason, (int)aq_strlen(text), status == 426 ? "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n" : "", text);
    flush_out(c);
    c->state = ST_CLOSING; c->close_sent = 1; c->close_code = 1002;
    c->close_deadline = aq_now_ms() + 500;
    c->close_emitted = 1;   /* never opened: no close event */
}

static void server_handshake(ws_conn *c) {
    const char *end = find_header_end(&c->in);
    if (!end) { if (c->in.len > MAX_HEADER_BYTES) http_reject(c, 400, "Header too large"); return; }
    size_t hlen = (size_t)(end - (const char *)c->in.data) + 4;
    const char *head = (const char *)c->in.data;
    ws_server *s = find_srv(c->ctx, c->server);
    /* request line */
    const char *sp1 = 0, *sp2 = 0;
    for (size_t i = 0; i < hlen; i++) { if (head[i] == ' ') { if (!sp1) sp1 = head + i; else { sp2 = head + i; break; } } if (head[i] == '\r') break; }
    if (!sp1 || !sp2 || (size_t)(sp1 - head) != 3 || aq_strncmp(head, "GET", 3) != 0) { http_reject(c, 400, "Expected a GET WebSocket upgrade request"); return; }
    char target[2048]; size_t tl = (size_t)(sp2 - sp1 - 1); if (tl >= sizeof target) tl = sizeof target - 1;
    aq_memcpy(target, sp1 + 1, tl); target[tl] = 0;
    char *q = target; while (*q && *q != '?') q++;
    aq_free(c->query); c->query = aq_strdup(*q == '?' ? q + 1 : "");
    *q = 0;
    aq_free(c->path); c->path = aq_strdup(target);
    char upgrade[128] = {0}, connection[256] = {0}, version[16] = {0}, key[128] = {0}, origin[512] = {0}, protos[512] = {0};
    header_value(head, hlen, "Upgrade", upgrade, sizeof upgrade);
    header_value(head, hlen, "Connection", connection, sizeof connection);
    header_value(head, hlen, "Sec-WebSocket-Version", version, sizeof version);
    header_value(head, hlen, "Sec-WebSocket-Key", key, sizeof key);
    header_value(head, hlen, "Origin", origin, sizeof origin);
    header_value(head, hlen, "Sec-WebSocket-Protocol", protos, sizeof protos);
    if (s && s->path && s->path[0] && aq_strcmp(s->path, c->path) != 0) { http_reject(c, 404, "Unknown WebSocket path"); return; }
    if (!token_list_contains(upgrade, "websocket") || !token_list_contains(connection, "upgrade")) { http_reject(c, 426, "Aquarius WebSocket server: please connect with a WebSocket client."); return; }
    if (aq_strcmp(version, "13") != 0 || aq_strlen(key) < 16) { http_reject(c, 400, "Unsupported WebSocket version or key"); return; }
    if (s && s->norigins && origin[0]) {
        int ok = 0;
        for (int i = 0; i < s->norigins; i++) if (aq_strcmp(s->origins[i], "*") == 0 || aq_strcasecmp(s->origins[i], origin) == 0) ok = 1;
        if (!ok) { http_reject(c, 403, "Origin not allowed"); return; }
    }
    aq_free(c->origin); c->origin = aq_strdup(origin);
    const char *chosen = 0;
    if (s && s->nprotocols && protos[0]) for (int i = 0; i < s->nprotocols && !chosen; i++) if (token_list_contains(protos, s->protocols[i])) chosen = s->protocols[i];
    aq_free(c->protocol); c->protocol = aq_strdup(chosen ? chosen : "");
    char accept[32]; compute_accept(key, accept);
    aq_buf_printf(&c->out, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n", accept);
    if (chosen) aq_buf_printf(&c->out, "Sec-WebSocket-Protocol: %s\r\n", chosen);
    aq_buf_puts(&c->out, "\r\n");
    aq_buf_consume(&c->in, hlen);
    c->state = ST_OPEN; c->last_rx = c->last_ping = aq_now_ms();
    flush_out(c);
    aq_buf b = {0};
    aq_buf_printf(&b, "{\"type\":\"open\",\"conn\":%d,\"server\":%d,\"remote\":", c->id, c->server);
    aj_w_cstr(&b, c->remote);
    aq_buf_puts(&b, ",\"path\":"); aj_w_cstr(&b, c->path);
    aq_buf_puts(&b, ",\"query\":"); aj_w_cstr(&b, c->query);
    aq_buf_puts(&b, ",\"origin\":"); aj_w_cstr(&b, c->origin);
    aq_buf_puts(&b, ",\"protocol\":"); aj_w_cstr(&b, c->protocol);
    aq_buf_putc(&b, '}');
    push_event(c->ctx, &b);
}

static void client_send_handshake(ws_conn *c) {
    uint8_t raw[16]; char key[32];
    aq_random(raw, 16); aq_base64_encode(raw, 16, key);
    compute_accept(key, c->accept_expected);
    aq_buf_printf(&c->out, "GET %s%s%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: AquariusWebSocket/" AQWS_VERSION "\r\n",
                  c->path && c->path[0] ? c->path : "/", c->query && c->query[0] ? "?" : "", c->query ? c->query : "", c->host_header, key);
    if (c->request_protocols && c->request_protocols[0]) aq_buf_printf(&c->out, "Sec-WebSocket-Protocol: %s\r\n", c->request_protocols);
    if (c->origin && c->origin[0]) aq_buf_printf(&c->out, "Origin: %s\r\n", c->origin);
    if (c->extra_headers) aq_buf_puts(&c->out, c->extra_headers);
    aq_buf_puts(&c->out, "\r\n");
    c->state = ST_HANDSHAKE;
    flush_out(c);
}
static void client_read_handshake(ws_conn *c) {
    const char *end = find_header_end(&c->in);
    if (!end) { if (c->in.len > MAX_HEADER_BYTES) { emit_error(c->ctx, c->id, 0, "handshake response too large"); start_close(c, 1002, ""); } return; }
    size_t hlen = (size_t)(end - (const char *)c->in.data) + 4;
    const char *head = (const char *)c->in.data;
    if (hlen < 12 || aq_strncmp(head, "HTTP/1.1 101", 12) != 0) {
        char line[96]; size_t l = 0; while (l < hlen && l < sizeof line - 1 && head[l] != '\r') { line[l] = head[l]; l++; } line[l] = 0;
        char msg[160]; aq_snprintf(msg, sizeof msg, "server rejected the WebSocket upgrade: %s", line);
        emit_error(c->ctx, c->id, 0, msg);
        aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED; c->close_code = 1002;
        return;
    }
    char accept[64] = {0}, proto[256] = {0};
    header_value(head, hlen, "Sec-WebSocket-Accept", accept, sizeof accept);
    header_value(head, hlen, "Sec-WebSocket-Protocol", proto, sizeof proto);
    if (aq_strcmp(accept, c->accept_expected) != 0) {
        emit_error(c->ctx, c->id, 0, "invalid Sec-WebSocket-Accept from server");
        aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED; c->close_code = 1002;
        return;
    }
    aq_free(c->protocol); c->protocol = aq_strdup(proto);
    aq_buf_consume(&c->in, hlen);
    c->state = ST_OPEN; c->last_rx = c->last_ping = aq_now_ms();
    aq_buf b = {0};
    aq_buf_printf(&b, "{\"type\":\"open\",\"conn\":%d,\"server\":0,\"remote\":", c->id);
    aj_w_cstr(&b, c->remote);
    aq_buf_puts(&b, ",\"protocol\":"); aj_w_cstr(&b, c->protocol);
    aq_buf_putc(&b, '}');
    push_event(c->ctx, &b);
    process_frames(c);
}

/* ---------------- object lifecycle ---------------- */
static void conn_free(ws_conn *c) {
    if (c->sock != AQ_BAD_SOCK) aq_sock_close(c->sock);
    aq_buf_free(&c->in); aq_buf_free(&c->out); aq_buf_free(&c->msg);
    aq_free(c->close_reason); aq_free(c->path); aq_free(c->query); aq_free(c->origin); aq_free(c->protocol);
    aq_free(c->host_header); aq_free(c->request_protocols); aq_free(c->extra_headers);
    aq_free(c);
}
static void server_free(ws_server *s) {
    if (s->sock != AQ_BAD_SOCK) aq_sock_close(s->sock);
    aq_free(s->path); free_strlist(s->origins, s->norigins); free_strlist(s->protocols, s->nprotocols);
    aq_free(s);
}
static void emit_close(ws_conn *c) {
    if (c->close_emitted) return;
    c->close_emitted = 1;
    aq_buf b = {0};
    aq_buf_printf(&b, "{\"type\":\"close\",\"conn\":%d,\"server\":%d,\"code\":%d,\"reason\":", c->id, c->server, c->close_code ? c->close_code : 1006);
    aj_w_cstr(&b, c->close_reason ? c->close_reason : "");
    aq_buf_puts(&b, ",\"clean\":");
    aq_buf_puts(&b, c->close_received && c->close_sent ? "true" : "false");
    aq_buf_putc(&b, '}');
    push_event(c->ctx, &b);
}
static void reap_closed(int ctx) {
    for (int i = 0; i < g_nconn;) {
        ws_conn *c = g_conn[i];
        if (c->ctx == ctx && c->state == ST_CLOSED) {
            emit_close(c);
            if (c->server) { ws_server *s = find_srv(ctx, c->server); if (s && s->connections > 0) s->connections--; }
            conn_free(c);
            g_conn[i] = g_conn[--g_nconn];
            continue;
        }
        i++;
    }
}

/* ---------------- poll ---------------- */
static void service_conn(ws_conn *c, short revents) {
    uint64_t now = aq_now_ms();
    if (c->state == ST_CONNECTING) {
        int r = aq_tcp_connect_result(c->sock);
        if (r < 0) {
            emit_error(c->ctx, c->id, 0, "connection refused or unreachable");
            aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED; c->close_code = 1006;
            return;
        }
        if (r == 0) client_send_handshake(c);
        else if (now > c->handshake_deadline) {
            emit_error(c->ctx, c->id, 0, "connection timed out");
            aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED; c->close_code = 1006;
            return;
        }
        if (c->state == ST_CONNECTING) return;
    }
    if (revents & AQ_POLL_OUT) flush_out(c);
    if ((revents & (AQ_POLL_IN | AQ_POLL_HUP | AQ_POLL_ERR)) && c->sock != AQ_BAD_SOCK) {
        static uint8_t tmp[READ_CHUNK];   /* static: keeps Windows stack frames small (no stack probes) */
        int total = 0;
        for (;;) {
            int n = aq_sock_recv(c->sock, tmp, sizeof tmp);
            if (n > 0) {
                aq_buf_append(&c->in, tmp, (size_t)n); total += n;
                if (c->in.failed) { start_close(c, 1009, "out of memory"); break; }
                if (total > 8 * READ_CHUNK) break;
                continue;
            }
            if (n == AQ_IO_AGAIN) break;
            /* EOF or error */
            aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK;
            if (!c->close_code) c->close_code = 1006;
            break;
        }
        if (c->state == ST_HANDSHAKE) { if (c->is_client) client_read_handshake(c); else server_handshake(c); }
        if (c->state == ST_OPEN || c->state == ST_CLOSING) process_frames(c);
        if (c->sock == AQ_BAD_SOCK) c->state = ST_CLOSED;
    }
    if (c->state == ST_CLOSED) return;
    /* timers */
    if (c->state == ST_HANDSHAKE && now > c->handshake_deadline) {
        if (c->is_client) emit_error(c->ctx, c->id, 0, "WebSocket handshake timed out");
        else c->close_emitted = 1;
        aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED; c->close_code = 1006;
        return;
    }
    if (c->state == ST_CLOSING) {
        if ((c->close_received && c->close_sent && !c->out.len && !c->is_client) || now > c->close_deadline) {
            aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED;
        }
        return;
    }
    if (c->state == ST_OPEN && c->ping_interval > 0) {
        if (c->ping_outstanding && now - c->last_ping > (uint64_t)c->ping_interval * 2) {
            c->close_code = 1006; aq_free(c->close_reason); c->close_reason = aq_strdup("ping timeout");
            aq_sock_close(c->sock); c->sock = AQ_BAD_SOCK; c->state = ST_CLOSED;
            return;
        }
        if (!c->ping_outstanding && now - c->last_rx > (uint64_t)c->ping_interval && now - c->last_ping > (uint64_t)c->ping_interval) {
            send_frame(c, 0x9, (const uint8_t *)"aqua", 4);
            c->last_ping = now; c->ping_outstanding = 1;
        }
    }
}

static void accept_all(ws_server *s) {
    for (int guard = 0; guard < 256; guard++) {
        aq_addr peer;
        aq_sock ns = aq_tcp_accept(s->sock, &peer);
        if (ns == AQ_BAD_SOCK) return;
        if (s->connections >= s->max_connections) { aq_sock_close(ns); continue; }
        ws_conn *c = (ws_conn *)aq_calloc(1, sizeof *c);
        if (!c) { aq_sock_close(ns); return; }
        c->id = new_id(); c->ctx = s->ctx; c->server = s->id; c->sock = ns; c->state = ST_HANDSHAKE;
        c->max_message = s->max_message; c->ping_interval = s->ping_interval;
        c->created = aq_now_ms(); c->handshake_deadline = c->created + 10000;
        aq_addr_format(&peer, c->remote, sizeof c->remote, 1);
        g_conn = (ws_conn **)grow_push(g_conn, &g_nconn, c);
        s->connections++;
    }
}

static int do_poll(int ctx, int timeout) {
    ws_ctx *cx = find_ctx(ctx);
    if (!cx) return -1;
    int n = 0;
    for (int i = 0; i < g_nsrv; i++) if (g_srv[i]->ctx == ctx && g_srv[i]->sock != AQ_BAD_SOCK) n++;
    for (int i = 0; i < g_nconn; i++) if (g_conn[i]->ctx == ctx && g_conn[i]->sock != AQ_BAD_SOCK) n++;
    aq_pollfd *fds = (aq_pollfd *)aq_calloc((size_t)(n ? n : 1), sizeof(aq_pollfd));
    void **owners = (void **)aq_calloc((size_t)(n ? n : 1), sizeof(void *));
    int *kinds = (int *)aq_calloc((size_t)(n ? n : 1), sizeof(int));
    if (!fds || !owners || !kinds) { aq_free(fds); aq_free(owners); aq_free(kinds); return -1; }
    int k = 0;
    for (int i = 0; i < g_nsrv; i++) if (g_srv[i]->ctx == ctx && g_srv[i]->sock != AQ_BAD_SOCK) {
        fds[k].fd = g_srv[i]->sock; fds[k].events = AQ_POLL_IN; owners[k] = g_srv[i]; kinds[k] = 1; k++;
    }
    for (int i = 0; i < g_nconn; i++) if (g_conn[i]->ctx == ctx && g_conn[i]->sock != AQ_BAD_SOCK) {
        ws_conn *c = g_conn[i];
        fds[k].fd = c->sock; fds[k].events = AQ_POLL_IN;
        if (c->out.len || c->state == ST_CONNECTING) fds[k].events |= AQ_POLL_OUT;
        owners[k] = c; kinds[k] = 2; k++;
    }
    if (cx->queued) timeout = 0;
    if (timeout > 1000) timeout = 1000;
    if (timeout < 0) timeout = 0;
    aq_poll(fds, k, timeout);
    for (int i = 0; i < k; i++) {
        if (kinds[i] == 1) { if (fds[i].revents & AQ_POLL_IN) accept_all((ws_server *)owners[i]); }
    }
    /* service every connection (timers run even without readiness) */
    for (int i = 0; i < k; i++) if (kinds[i] == 2) service_conn((ws_conn *)owners[i], fds[i].revents);
    /* new connections accepted above may already have data */
    for (int i = 0; i < g_nconn; i++) {
        ws_conn *c = g_conn[i];
        int seen = 0;
        for (int j = 0; j < k; j++) if (owners[j] == c) { seen = 1; break; }
        if (!seen && c->ctx == ctx && c->sock != AQ_BAD_SOCK) service_conn(c, AQ_POLL_IN);
    }
    /* connections without sockets still need timers / reaping */
    for (int i = 0; i < g_nconn; i++) if (g_conn[i]->ctx == ctx && g_conn[i]->sock == AQ_BAD_SOCK && g_conn[i]->state != ST_CLOSED) g_conn[i]->state = ST_CLOSED;
    aq_free(fds); aq_free(owners); aq_free(kinds);
    reap_closed(ctx);
    return 0;
}

/* ---------------- request helpers ---------------- */
static int write_out(const aq_buf *b, uint8_t *out, int cap) {
    if (b->failed) { const char *e = "{\"error\":\"out of memory\"}"; int n = (int)aq_strlen(e); aq_memcpy(out, e, (size_t)n); return n; }
    if ((int)b->len > cap) { const char *e = "{\"error\":\"response too large\"}"; int n = (int)aq_strlen(e); aq_memcpy(out, e, (size_t)n); return n; }
    aq_memcpy(out, b->data, b->len);
    return (int)b->len;
}
static int reply_error(uint8_t *out, int cap, const char *msg) {
    aq_buf b = {0};
    aq_buf_puts(&b, "{\"error\":"); aj_w_cstr(&b, msg); aq_buf_putc(&b, '}');
    int n = write_out(&b, out, cap); aq_buf_free(&b); return n;
}

typedef int (*handler_fn)(aj *req, aq_buf *res, uint8_t *out, int cap);
static int run(const uint8_t *in, int inlen, uint8_t *out, int cap, handler_fn fn) {
    if (!in || !out || inlen < 0 || cap < 64) return -1;
    aq_lock();
    aq_plat_init();
    aj *req = aj_parse((const char *)in, (size_t)inlen);
    int n;
    if (!req) n = reply_error(out, cap, "invalid JSON request");
    else {
        aq_buf res = {0};
        n = fn(req, &res, out, cap);
        if (n == 0) n = write_out(&res, out, cap);
        aq_buf_free(&res);
        aj_free(req);
    }
    aq_unlock();
    return n;
}

/* ---------------- handlers ---------------- */
static int h_open(aj *req, aq_buf *res, uint8_t *out, int cap) {
    (void)req; (void)out; (void)cap;
    ws_ctx *c = (ws_ctx *)aq_calloc(1, sizeof *c);
    if (!c) return reply_error(out, cap, "out of memory");
    c->id = new_id();
    g_ctx = (ws_ctx **)grow_push(g_ctx, &g_nctx, c);
    aq_buf_printf(res, "{\"ctx\":%d,\"version\":\"" AQWS_VERSION "\",\"platform\":\"" AQ_PLATFORM_NAME "\",\"server\":true,\"tls\":false}", c->id);
    return 0;
}
static void close_ctx_objects(int ctx) {
    for (int i = 0; i < g_nconn;) { if (g_conn[i]->ctx == ctx) { conn_free(g_conn[i]); g_conn[i] = g_conn[--g_nconn]; continue; } i++; }
    for (int i = 0; i < g_nsrv;) { if (g_srv[i]->ctx == ctx) { server_free(g_srv[i]); g_srv[i] = g_srv[--g_nsrv]; continue; } i++; }
}
static int h_close_ctx(aj *req, aq_buf *res, uint8_t *out, int cap) {
    int id = (int)aj_get_int(req, "ctx", 0);
    for (int i = 0; i < g_nctx; i++) if (g_ctx[i]->id == id) {
        ws_ctx *c = g_ctx[i];
        /* send close frames best effort */
        for (int j = 0; j < g_nconn; j++) if (g_conn[j]->ctx == id && g_conn[j]->state == ST_OPEN) start_close(g_conn[j], 1001, "going away");
        close_ctx_objects(id);
        for (ws_event *e = c->head; e;) { ws_event *n = e->next; aq_free(e->json); aq_free(e); e = n; }
        aq_free(c);
        g_ctx[i] = g_ctx[--g_nctx];
        aq_buf_puts(res, "{\"ok\":true}");
        return 0;
    }
    (void)out; (void)cap;
    aq_buf_puts(res, "{\"ok\":false}");
    return 0;
}
static int h_listen(aj *req, aq_buf *res, uint8_t *out, int cap) {
    int ctx = (int)aj_get_int(req, "ctx", 0);
    if (!find_ctx(ctx)) return reply_error(out, cap, "unknown context");
    const char *host = aj_get_str(req, "host", "0.0.0.0");
    long long port = aj_get_int(req, "port", 0);
    if (port < 0 || port > 65535) return reply_error(out, cap, "port must be 0..65535");
    aq_addr a;
    if (aq_resolve(host, (uint16_t)port, &a) != 0) return reply_error(out, cap, "listen host must be a numeric IP address");
    int err = 0;
    aq_sock s = aq_tcp_listen(&a, 128, &err);
    if (s == AQ_BAD_SOCK) { char m[96]; aq_snprintf(m, sizeof m, "cannot listen on %s:%d (os error %d)", host, (int)port, err); return reply_error(out, cap, m); }
    ws_server *sv = (ws_server *)aq_calloc(1, sizeof *sv);
    if (!sv) { aq_sock_close(s); return reply_error(out, cap, "out of memory"); }
    sv->id = new_id(); sv->ctx = ctx; sv->sock = s;
    aq_sock_local(s, &sv->addr);
    sv->path = aq_strdup(aj_get_str(req, "path", ""));
    sv->origins = read_strlist(aj_get(req, "origins"), &sv->norigins);
    sv->protocols = read_strlist(aj_get(req, "protocols"), &sv->nprotocols);
    sv->max_message = (int)aj_get_int(req, "maxMessage", DEFAULT_MAX_MESSAGE);
    if (sv->max_message < 125 || sv->max_message > 900 * 1024) sv->max_message = DEFAULT_MAX_MESSAGE;
    sv->max_connections = (int)aj_get_int(req, "maxConnections", 256);
    if (sv->max_connections < 1) sv->max_connections = 1;
    if (sv->max_connections > 4096) sv->max_connections = 4096;
    sv->ping_interval = (int)aj_get_int(req, "pingInterval", 25000);
    if (sv->ping_interval < 0) sv->ping_interval = 0;
    g_srv = (ws_server **)grow_push(g_srv, &g_nsrv, sv);
    char addr[64]; aq_addr_format(&sv->addr, addr, sizeof addr, 1);
    aq_buf_printf(res, "{\"server\":%d,\"port\":%d,\"address\":", sv->id, sv->addr.port);
    aj_w_cstr(res, addr);
    aq_buf_putc(res, '}');
    return 0;
}
static int parse_ws_url(const char *url, int *tls, char *host, size_t hcap, int *port, char **path, char **query) {
    const char *p = url;
    if (aq_strncasecmp(p, "ws://", 5) == 0) { *tls = 0; p += 5; *port = 80; }
    else if (aq_strncasecmp(p, "wss://", 6) == 0) { *tls = 1; p += 6; *port = 443; }
    else return -1;
    const char *hs = p, *he;
    if (*p == '[') { while (*p && *p != ']') p++; if (*p != ']') return -1; p++; he = p; }
    else { while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++; he = p; }
    size_t hl = (size_t)(he - hs); if (!hl || hl >= hcap) return -1;
    aq_memcpy(host, hs, hl); host[hl] = 0;
    if (*p == ':') { p++; const char *e; long long v = aq_atoll(p, &e); if (e == p || v < 1 || v > 65535) return -1; *port = (int)v; p = e; }
    const char *ps = p; while (*p && *p != '?' && *p != '#') p++;
    *path = aq_strndup(ps, (size_t)(p - ps));
    if (*p == '?') { p++; const char *qs = p; while (*p && *p != '#') p++; *query = aq_strndup(qs, (size_t)(p - qs)); }
    else *query = aq_strdup("");
    return 0;
}
static int h_connect(aj *req, aq_buf *res, uint8_t *out, int cap) {
    int ctx = (int)aj_get_int(req, "ctx", 0);
    if (!find_ctx(ctx)) return reply_error(out, cap, "unknown context");
    const char *url = aj_get_str(req, "url", "");
    int tls = 0, port = 0; char host[256]; char *path = 0, *query = 0;
    if (parse_ws_url(url, &tls, host, sizeof host, &port, &path, &query) != 0) return reply_error(out, cap, "invalid WebSocket URL (expected ws://host:port/path)");
    if (tls) { aq_free(path); aq_free(query); return reply_error(out, cap, "wss:// needs TLS, which the native engine does not include; use ws:// on desktop/Android or a TLS-terminating proxy"); }
    const char *address = aj_get_str(req, "address", host);
    aq_addr a;
    if (aq_resolve(address, (uint16_t)port, &a) != 0) {
        aq_free(path); aq_free(query);
        aq_buf_puts(res, "{\"error\":\"cannot resolve host name natively; resolve it first and pass address\",\"needsResolve\":true,\"host\":");
        aj_w_cstr(res, host[0] == '[' ? "" : host);
        aq_buf_putc(res, '}');
        return 0;
    }
    int err = 0;
    aq_sock s = aq_tcp_connect(&a, &err);
    if (s == AQ_BAD_SOCK) { aq_free(path); aq_free(query); char m[96]; aq_snprintf(m, sizeof m, "connect failed (os error %d)", err); return reply_error(out, cap, m); }
    ws_conn *c = (ws_conn *)aq_calloc(1, sizeof *c);
    if (!c) { aq_sock_close(s); aq_free(path); aq_free(query); return reply_error(out, cap, "out of memory"); }
    c->id = new_id(); c->ctx = ctx; c->sock = s; c->state = ST_CONNECTING; c->is_client = 1;
    c->path = path; c->query = query;
    c->max_message = (int)aj_get_int(req, "maxMessage", DEFAULT_MAX_MESSAGE);
    if (c->max_message < 125 || c->max_message > 900 * 1024) c->max_message = DEFAULT_MAX_MESSAGE;
    c->ping_interval = (int)aj_get_int(req, "pingInterval", 25000);
    if (c->ping_interval < 0) c->ping_interval = 0;
    c->created = aq_now_ms();
    c->handshake_deadline = c->created + (uint64_t)aj_get_int(req, "timeout", 10000);
    aq_addr_format(&a, c->remote, sizeof c->remote, 1);
    {
        char hh[300];
        int default_port = port == 80;
        if (host[0] == '[' || aq_strstr(host, ":") == 0) aq_snprintf(hh, sizeof hh, default_port ? "%s" : "%s:%d", host, port);
        else aq_snprintf(hh, sizeof hh, default_port ? "[%s]" : "[%s]:%d", host, port);
        c->host_header = aq_strdup(aj_get_str(req, "hostHeader", hh));
    }
    c->origin = aq_strdup(aj_get_str(req, "origin", ""));
    aj *protos = aj_get(req, "protocols");
    if (protos && protos->type == AJ_ARR) {
        aq_buf pb = {0}; int first = 1;
        for (aj *v = protos->child; v; v = v->next) if (v->type == AJ_STR) { if (!first) aq_buf_puts(&pb, ", "); aq_buf_puts(&pb, v->str); first = 0; }
        aq_buf_putc(&pb, 0); c->request_protocols = (char *)pb.data;
    }
    aj *headers = aj_get(req, "headers");
    if (headers && headers->type == AJ_OBJ) {
        aq_buf hb = {0};
        for (aj *v = headers->child; v; v = v->next) {
            if (v->type != AJ_STR || !v->key) continue;
            int ok = 1;
            for (const char *q = v->key; *q; q++) if (*q <= ' ' || *q == ':') ok = 0;
            for (size_t q = 0; q < v->len; q++) if (v->str[q] == '\r' || v->str[q] == '\n') ok = 0;
            if (ok) aq_buf_printf(&hb, "%s: %s\r\n", v->key, v->str);
        }
        aq_buf_putc(&hb, 0); c->extra_headers = (char *)hb.data;
    }
    g_conn = (ws_conn **)grow_push(g_conn, &g_nconn, c);
    aq_buf_printf(res, "{\"conn\":%d}", c->id);
    return 0;
}
static int h_send(aj *req, aq_buf *res, uint8_t *out, int cap) {
    int ctx = (int)aj_get_int(req, "ctx", 0);
    aj *text = aj_get(req, "text"), *bin = aj_get(req, "binary");
    const uint8_t *data; size_t n; int opcode; uint8_t *decoded = 0;
    aj *arr = aj_get(req, "bytes");
    if (text && text->type == AJ_STR) { data = (const uint8_t *)text->str; n = text->len; opcode = 1; }
    else if (arr && arr->type == AJ_ARR) {
        int count = aj_count(arr), k = 0;
        decoded = (uint8_t *)aq_malloc((size_t)(count ? count : 1));
        if (!decoded) return reply_error(out, cap, "out of memory");
        for (aj *v = arr->child; v; v = v->next) decoded[k++] = (uint8_t)(v->type == AJ_NUM ? (int)v->num : 0);
        data = decoded; n = (size_t)count; opcode = 2;
    } else if (bin && bin->type == AJ_STR) {
        decoded = (uint8_t *)aq_malloc(bin->len * 3 / 4 + 4);
        if (!decoded) return reply_error(out, cap, "out of memory");
        int dn = aq_base64_decode(bin->str, bin->len, decoded, bin->len * 3 / 4 + 4);
        if (dn < 0) { aq_free(decoded); return reply_error(out, cap, "binary must be base64"); }
        data = decoded; n = (size_t)dn; opcode = 2;
    } else return reply_error(out, cap, "send needs text, bytes or binary");
    int sent = 0;
    aj *list = aj_get(req, "conns");
    if (list && list->type == AJ_ARR) {
        for (aj *v = list->child; v; v = v->next) {
            ws_conn *c = v->type == AJ_NUM ? find_conn(ctx, (int)v->num) : 0;
            if (c && c->state == ST_OPEN) { send_frame(c, opcode, data, n); sent++; }
        }
    } else {
        ws_conn *c = find_conn(ctx, (int)aj_get_int(req, "conn", 0));
        if (c && c->state == ST_OPEN) { send_frame(c, opcode, data, n); sent++; }
    }
    aq_free(decoded);
    aq_buf_printf(res, "{\"sent\":%d}", sent);
    return 0;
}
static int h_broadcast(aj *req, aq_buf *res, uint8_t *out, int cap) {
    int ctx = (int)aj_get_int(req, "ctx", 0);
    int server = (int)aj_get_int(req, "server", 0);
    int except = (int)aj_get_int(req, "except", 0);
    aj *text = aj_get(req, "text");
    if (!text || text->type != AJ_STR) return reply_error(out, cap, "broadcast needs text");
    int sent = 0;
    for (int i = 0; i < g_nconn; i++) {
        ws_conn *c = g_conn[i];
        if (c->ctx == ctx && c->server == server && server && c->id != except && c->state == ST_OPEN) { send_frame(c, 1, (const uint8_t *)text->str, text->len); sent++; }
    }
    aq_buf_printf(res, "{\"sent\":%d}", sent);
    return 0;
}
static int h_close(aj *req, aq_buf *res, uint8_t *out, int cap) {
    (void)out; (void)cap;
    int ctx = (int)aj_get_int(req, "ctx", 0);
    ws_conn *c = find_conn(ctx, (int)aj_get_int(req, "conn", 0));
    if (c) start_close(c, (int)aj_get_int(req, "code", 1000), aj_get_str(req, "reason", ""));
    aq_buf_printf(res, "{\"ok\":%s}", c ? "true" : "false");
    return 0;
}
static int h_close_server(aj *req, aq_buf *res, uint8_t *out, int cap) {
    (void)out; (void)cap;
    int ctx = (int)aj_get_int(req, "ctx", 0), id = (int)aj_get_int(req, "server", 0);
    int ok = 0;
    for (int i = 0; i < g_nsrv; i++) if (g_srv[i]->ctx == ctx && g_srv[i]->id == id) {
        if (aj_get_bool(req, "closeConnections", 1))
            for (int j = 0; j < g_nconn; j++) if (g_conn[j]->ctx == ctx && g_conn[j]->server == id) start_close(g_conn[j], 1001, "server closing");
        aq_sock_close(g_srv[i]->sock); g_srv[i]->sock = AQ_BAD_SOCK;
        ok = 1;
    }
    aq_buf_printf(res, "{\"ok\":%s}", ok ? "true" : "false");
    return 0;
}
static int h_poll(aj *req, aq_buf *res, uint8_t *out, int cap) {
    int ctx = (int)aj_get_int(req, "ctx", 0);
    ws_ctx *cx = find_ctx(ctx);
    if (!cx) return reply_error(out, cap, "unknown context");
    int timeout = (int)aj_get_int(req, "timeout", 0);
    int max = (int)aj_get_int(req, "max", 10000);
    do_poll(ctx, timeout);
    aq_buf_puts(res, "{\"events\":[");
    int count = 0;
    while (cx->head && count < max) {
        ws_event *e = cx->head;
        if (res->len + e->len + 64 > (size_t)cap) {
            if (count == 0) { /* single oversized event: drop it */
                cx->head = e->next; if (!cx->head) cx->tail = 0; cx->queued--;
                aq_buf_puts(res, "{\"type\":\"error\",\"conn\":0,\"server\":0,\"message\":\"event exceeded the 1 MiB call limit and was dropped\"}");
                aq_free(e->json); aq_free(e); count++;
            }
            break;
        }
        if (count) aq_buf_putc(res, ',');
        aq_buf_append(res, e->json, e->len);
        cx->head = e->next; if (!cx->head) cx->tail = 0; cx->queued--;
        aq_free(e->json); aq_free(e); count++;
    }
    aq_buf_printf(res, "],\"pending\":%d}", cx->queued);
    return 0;
}
static int h_info(aj *req, aq_buf *res, uint8_t *out, int cap) {
    (void)out; (void)cap;
    int ctx = (int)aj_get_int(req, "ctx", 0);
    aq_buf_puts(res, "{\"servers\":[");
    int first = 1;
    for (int i = 0; i < g_nsrv; i++) if (g_srv[i]->ctx == ctx) {
        char addr[64]; aq_addr_format(&g_srv[i]->addr, addr, sizeof addr, 1);
        aq_buf_printf(res, "%s{\"server\":%d,\"port\":%d,\"listening\":%s,\"connections\":%d,\"address\":", first ? "" : ",", g_srv[i]->id, g_srv[i]->addr.port, g_srv[i]->sock != AQ_BAD_SOCK ? "true" : "false", g_srv[i]->connections);
        aj_w_cstr(res, addr); aq_buf_putc(res, '}');
        first = 0;
    }
    aq_buf_puts(res, "],\"connections\":[");
    first = 1;
    for (int i = 0; i < g_nconn; i++) if (g_conn[i]->ctx == ctx) {
        ws_conn *c = g_conn[i];
        static const char *names[] = {"", "connecting", "handshake", "open", "closing", "closed"};
        aq_buf_printf(res, "%s{\"conn\":%d,\"server\":%d,\"state\":\"%s\",\"buffered\":%d,\"remote\":", first ? "" : ",", c->id, c->server, names[c->state], (int)c->out.len);
        aj_w_cstr(res, c->remote); aq_buf_putc(res, '}');
        first = 0;
    }
    aq_buf_puts(res, "]}");
    return 0;
}
static int h_random(aj *req, aq_buf *res, uint8_t *out, int cap) {
    (void)out; (void)cap;
    int n = (int)aj_get_int(req, "bytes", 16);
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    uint8_t buf[256]; aq_random(buf, (size_t)n);
    uint32_t v = 0; aq_random(&v, 4);
    aq_buf_puts(res, "{\"hex\":\"");
    for (int i = 0; i < n; i++) aq_buf_printf(res, "%02x", buf[i]);
    aq_buf_printf(res, "\",\"int\":%u}", v & 0x3fffffffu);
    return 0;
}

/* The SDK owns all public values; engine buffers remain private. */
static void command(aqua_call *call) {
    aqua_str name = aqua_arg_str(call, 0), input = aqua_arg_str(call, 1);
    handler_fn handler = NULL;
#define WS_COMMAND(name_, handler_) if (aq_strcmp(name.data, #name_) == 0) handler = handler_;
#include "commands.def"
#undef WS_COMMAND
    if (!handler || input.length > INT32_MAX) { aqua_fail(call, "Invalid WebSocket command"); return; }
    uint8_t *output = aqua_alloc(1024 * 1024);
    if (!output) { aqua_fail(call, "Out of memory"); return; }
    int length = run((const uint8_t *)input.data, (int)input.length, output, 1024 * 1024, handler);
    if (length < 0) aqua_fail(call, "WebSocket command failed (%d)", length);
    else aqua_return_text(call, (const char *)output, (size_t)length);
    aqua_free(output);
}
AQUA_LIBRARY(aquaws) {
    aqua_class *module = aqua_define_module(library, "AquaWebSocketNative|網頁通訊端原生");
    aqua_define_function(module, "Command|命令", command, "ss");
}

#if defined(AQ_WINDOWS) && !defined(AQ_HOSTED_LIBC)
int __stdcall _DllMainCRTStartup(void *inst, unsigned long reason, void *reserved);
int __stdcall _DllMainCRTStartup(void *inst, unsigned long reason, void *reserved) { (void)inst; (void)reason; (void)reserved; return 1; }
#endif
