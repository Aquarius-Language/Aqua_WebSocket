#include "aq_json.h"
#include <aqua/aqua.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/api.h>

typedef void (*web_event)(void *, int32_t, int32_t, int32_t);
AQUA_WEB_IMPORT(network_socket_open_protocols)
int32_t web_open(const char *, const char *, uint32_t, web_event, void *);
AQUA_WEB_IMPORT(network_socket_send)
double web_send(int32_t, const uint8_t *, uint32_t, int32_t);
AQUA_WEB_IMPORT(network_socket_buffered) double web_buffered(int32_t);
AQUA_WEB_IMPORT(network_socket_close)
void web_close(int32_t, int32_t, const char *);
AQUA_WEB_IMPORT(network_socket_protocol) char *web_protocol(int32_t);
AQUA_WEB_IMPORT(network_release) void web_release(int32_t);
AQUA_WEB_IMPORT(network_page_host) char *web_page_host(void);
AQUA_WEB_IMPORT(network_page_port) char *web_page_port(void);
AQUA_WEB_IMPORT(network_page_secure) int32_t web_page_secure(void);

typedef struct context context;
typedef struct connection {
  struct connection *next;
  context *owner;
  int id, handle, state;
  char *url;
  double deadline;
} connection;
typedef struct event {
  struct event *next;
  aq_buf json;
} event;
typedef struct service {
  context *contexts;
  int next_id;
} service;
struct context {
  context *next;
  connection *connections;
  event *head, *tail;
  size_t queued_bytes;
  int id;
  bool closed;
};
typedef struct poll_job {
  service *service;
  aqua_pending *pending;
  int id, max;
  double deadline;
} poll_job;

static context *find_context(service *s, int id) {
  for (context *c = s->contexts; c; c = c->next)
    if (c->id == id && !c->closed)
      return c;
  return NULL;
}
static connection *find_connection(context *c, int id) {
  for (connection *x = c->connections; x; x = x->next)
    if (x->id == id)
      return x;
  return NULL;
}
static void append_event(context *c, aq_buf *json) {
  if (c->closed || json->failed || json->len > 1024 * 1024 ||
      c->queued_bytes + json->len > 16 * 1024 * 1024) {
    aq_buf_free(json);
    return;
  }
  event *e = calloc(1, sizeof *e);
  if (!e) {
    aq_buf_free(json);
    return;
  }
  e->json = *json;
  memset(json, 0, sizeof *json);
  if (c->tail)
    c->tail->next = e;
  else
    c->head = e;
  c->tail = e;
  c->queued_bytes += e->json.len;
}
static void socket_event(void *data, int32_t kind, int32_t a, int32_t b) {
  connection *x = data;
  context *c = x->owner;
  aq_buf out = {0};
  if (c->closed) {
    if (kind == 2 || kind == 3)
      free((void *)(uintptr_t)a);
    if (kind == 4)
      free((void *)(uintptr_t)b);
    return;
  }
  aq_buf_printf(&out, "{\"conn\":%d,\"server\":0,\"type\":", x->id);
  if (kind == 1) {
    x->state = 1;
    aq_buf_puts(&out, "\"open\",\"remote\":");
    aj_w_cstr(&out, x->url);
    aq_buf_puts(&out, ",\"protocol\":");
    char *p = web_protocol(x->handle);
    aj_w_cstr(&out, p ? p : "");
    free(p);
  } else if (kind == 2) {
    aq_buf_puts(&out, "\"message\",\"text\":");
    aj_w_str(&out, (const char *)(uintptr_t)a, (size_t)b);
    free((void *)(uintptr_t)a);
  } else if (kind == 3) {
    aq_buf_puts(&out, "\"binary\",\"bytes\":[");
    const uint8_t *p = (const uint8_t *)(uintptr_t)a;
    for (int i = 0; i < b; i++)
      aq_buf_printf(&out, "%s%u", i ? "," : "", p[i]);
    aq_buf_putc(&out, ']');
    free((void *)(uintptr_t)a);
  } else if (kind == 4) {
    x->state = 2;
    aq_buf_printf(&out, "\"close\",\"code\":%d,\"reason\":", a);
    aj_w_cstr(&out, b ? (const char *)(uintptr_t)b : "");
    free((void *)(uintptr_t)b);
  } else
    aq_buf_puts(&out, "\"error\",\"message\":\"WebSocket connection failed\"");
  aq_buf_putc(&out, '}');
  append_event(c, &out);
}
static void close_context(context *c) {
  c->closed = true;
  for (connection *x = c->connections; x; x = x->next) {
    web_close(x->handle, 1001, "going away");
    web_release(x->handle);
    x->handle = 0;
  }
  while (c->head) {
    event *e = c->head;
    c->head = e->next;
    aq_buf_free(&e->json);
    free(e);
  }
  c->tail = NULL;
  c->queued_bytes = 0;
}
static void *create(aqua_runtime *rt) {
  (void)rt;
  service *s = calloc(1, sizeof *s);
  if (s)
    s->next_id = 1;
  return s;
}
static void destroy(aqua_runtime *rt, void *data) {
  (void)rt;
  service *s = data;
  if (!s)
    return;
  while (s->contexts) {
    context *c = s->contexts;
    s->contexts = c->next;
    close_context(c);
    while (c->connections) {
      connection *x = c->connections;
      c->connections = x->next;
      free(x->url);
      free(x);
    }
    free(c);
  }
  free(s);
}
static void poll_result(context *c, int max, aq_buf *out) {
  aq_buf_puts(out, "{\"events\":[");
  int count = 0;
  while (c->head && count < max) {
    event *e = c->head;
    if (out->len + e->json.len > 1024 * 1024 - 64)
      break;
    c->head = e->next;
    if (!c->head)
      c->tail = NULL;
    if (count++)
      aq_buf_putc(out, ',');
    aq_buf_append(out, e->json.data, e->json.len);
    c->queued_bytes -= e->json.len;
    aq_buf_free(&e->json);
    free(e);
  }
  aq_buf_printf(out, "],\"pending\":%s}", c->head ? "true" : "false");
}
static double poll_tick(aqua_runtime *rt, double now, void *data) {
  poll_job *j = data;
  context *c = find_context(j->service, j->id);
  if (c) {
    for (connection *x = c->connections; x; x = x->next)
      if (x->state == 0 && now >= x->deadline) {
        socket_event(x, 5, 0, 0);
        web_close(x->handle, 1000, "");
        x->state = 2;
      }
  }
  if (c && !c->head && now < j->deadline)
    return 5;
  aq_buf out = {0};
  if (c)
    poll_result(c, j->max, &out);
  else
    aq_buf_puts(&out, "{\"error\":\"unknown context\"}");
  aqua_resolve(j->pending, aqua_text(rt, (const char *)out.data, out.len));
  aq_buf_free(&out);
  free(j);
  return -1;
}
static void command(aqua_call *call) {
  service *s = aqua_state(call);
  aqua_str name = aqua_arg_str(call, 0), text = aqua_arg_str(call, 1);
  aj *req = aj_parse(text.data, text.length);
  aq_buf out = {0};
  if (!s || !req || req->type != AJ_OBJ) {
    aj_free(req);
    aqua_fail(call, "Invalid WebSocket command");
    return;
  }
  context *c = find_context(s, (int)aj_get_int(req, "ctx", 0));
  if (strcmp(name.data, "aqws_open") == 0) {
    c = calloc(1, sizeof *c);
    if (!c) {
      aj_free(req);
      aqua_fail(call, "Out of memory");
      return;
    }
    c->id = s->next_id++;
    c->next = s->contexts;
    s->contexts = c;
    char *host = web_page_host(), *port = web_page_port();
    aq_buf_printf(&out,
                  "{\"ctx\":%d,\"version\":\"2.0.0\",\"platform\":\"web\","
                  "\"server\":false,\"tls\":true,\"page\":{\"host\":",
                  c->id);
    aj_w_cstr(&out, host ? host : "");
    aq_buf_puts(&out, ",\"port\":");
    aj_w_cstr(&out, port ? port : "");
    aq_buf_printf(&out, ",\"secure\":%s}}",
                  web_page_secure() ? "true" : "false");
    free(host);
    free(port);
  } else if (strcmp(name.data, "aqws_random") == 0) {
    int n = (int)aj_get_int(req, "bytes", 16);
    if (n < 1)
      n = 1;
    if (n > 256)
      n = 256;
    uint8_t bytes[256];
    uint32_t word;
    if (__wasi_random_get(bytes, (size_t)n) ||
        __wasi_random_get((uint8_t *)&word, sizeof word)) {
      aj_free(req);
      aqua_fail(call, "Secure random generation failed");
      return;
    }
    aq_buf_puts(&out, "{\"hex\":\"");
    for (int i = 0; i < n; i++)
      aq_buf_printf(&out, "%02x", bytes[i]);
    aq_buf_printf(&out, "\",\"int\":%u}", word & 0x3fffffffu);
  } else if (!c)
    aq_buf_puts(&out, "{\"error\":\"unknown context\"}");
  else if (strcmp(name.data, "aqws_close_ctx") == 0) {
    close_context(c);
    aq_buf_puts(&out, "{\"ok\":true}");
  } else if (strcmp(name.data, "aqws_poll") == 0) {
    poll_job *j = calloc(1, sizeof *j);
    if (!j) {
      aj_free(req);
      aqua_fail(call, "Out of memory");
      return;
    }
    j->service = s;
    j->id = c->id;
    j->max = (int)aj_get_int(req, "max", 10000);
    if (j->max < 1)
      j->max = 1;
    int timeout = (int)aj_get_int(req, "timeout", 0);
    if (timeout < 0)
      timeout = 0;
    if (timeout > 1000)
      timeout = 1000;
    j->deadline = aqua_now_ms(aqua_call_runtime(call)) + timeout;
    j->pending = aqua_defer(call);
    aqua_schedule(aqua_call_runtime(call), 0, poll_tick, j);
    aj_free(req);
    return;
  } else if (strcmp(name.data, "aqws_connect") == 0) {
    aj *url = aj_get(req, "url");
    if (!url || url->type != AJ_STR || strlen(url->str) != url->len ||
        (strncmp(url->str, "ws://", 5) != 0 && strncmp(url->str, "wss://", 6) != 0)) {
      aq_buf_puts(&out, "{\"error\":\"invalid WebSocket URL\"}");
      goto finish;
    }
    connection *x = calloc(1, sizeof *x);
    if (!x) {
      aj_free(req);
      aqua_fail(call, "Out of memory");
      return;
    }
    x->owner = c;
    x->id = s->next_id++;
    x->url = strdup(url->str);
    if (!x->url) { free(x); aj_free(req); aqua_fail(call, "Out of memory"); return; }
    aq_buf protocols = {0};
    aj *list = aj_get(req, "protocols");
    for (aj *p = list ? list->child : NULL; p; p = p->next)
      if (p->type == AJ_STR) {
        aq_buf_append(&protocols, p->str, p->len);
        aq_buf_putc(&protocols, 0);
      }
    x->deadline = aqua_now_ms(aqua_call_runtime(call)) +
                  aj_get_int(req, "timeout", 10000);
    x->handle = web_open(x->url, (const char *)protocols.data,
                         (uint32_t)protocols.len, socket_event, x);
    aq_buf_free(&protocols);
    if (!x->handle) {
      free(x->url); free(x);
      aq_buf_puts(&out, "{\"error\":\"invalid WebSocket URL or subprotocol\"}");
      goto finish;
    }
    x->next = c->connections;
    c->connections = x;
    aq_buf_printf(&out, "{\"conn\":%d}", x->id);
  } else if (strcmp(name.data, "aqws_send") == 0) {
    aj *value = aj_get(req, "text");
    bool is_text = value && value->type == AJ_STR;
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (is_text) {
      bytes = (uint8_t *)value->str;
      length = value->len;
    } else {
      value = aj_get(req, "bytes");
      if (!value || value->type != AJ_ARR) {
        aq_buf_puts(&out, "{\"error\":\"send needs text or bytes\"}");
        goto finish;
      }
      length = (size_t)aj_count(value);
      bytes = malloc(length ? length : 1);
      if (!bytes) {
        aj_free(req);
        aqua_fail(call, "Out of memory");
        return;
      }
      size_t i = 0;
      for (aj *p = value->child; p; p = p->next) {
        if (p->type != AJ_NUM || p->num < 0 || p->num > 255 ||
            p->num != (int)p->num) {
          free(bytes);
          aq_buf_puts(&out, "{\"error\":\"invalid byte\"}");
          goto finish;
        }
        bytes[i++] = (uint8_t)p->num;
      }
    }
    aj *ids = aj_get(req, "conns");
    int sent = 0;
    for (connection *x = c->connections; x; x = x->next) {
      bool selected = x->id == aj_get_int(req, "conn", 0);
      for (aj *p = ids ? ids->child : NULL; p; p = p->next)
        if (p->type == AJ_NUM && p->num == x->id)
          selected = true;
      if (selected && x->state == 1) {
        web_send(x->handle, bytes, (uint32_t)length, is_text);
        sent++;
      }
    }
    if (!is_text)
      free(bytes);
    aq_buf_printf(&out, "{\"sent\":%d}", sent);
  } else if (strcmp(name.data, "aqws_close") == 0) {
    connection *x = find_connection(c, (int)aj_get_int(req, "conn", 0));
    if (x) {
      int code = (int)aj_get_int(req, "code", 1000);
      if (code != 1000 && (code < 3000 || code > 4999))
        code = 1000;
      web_close(x->handle, code, aj_get_str(req, "reason", ""));
    }
    aq_buf_printf(&out, "{\"ok\":%s}", x ? "true" : "false");
  } else if (strcmp(name.data, "aqws_info") == 0) {
    aq_buf_puts(&out, "{\"servers\":[],\"connections\":[");
    int count = 0;
    for (connection *x = c->connections; x; x = x->next) {
      if (x->state == 2)
        continue;
      aq_buf_printf(&out,
                    "%s{\"conn\":%d,\"server\":0,\"state\":\"%s\",\"buffered\":"
                    "%.0f,\"remote\":",
                    count++ ? "," : "", x->id,
                    x->state == 1 ? "open" : "connecting",
                    web_buffered(x->handle));
      aj_w_cstr(&out, x->url);
      aq_buf_putc(&out, '}');
    }
    aq_buf_puts(&out, "]}");
  } else if (strcmp(name.data, "aqws_close_server") == 0)
    aq_buf_puts(&out, "{\"ok\":false}");
  else if (strcmp(name.data, "aqws_broadcast") == 0)
    aq_buf_puts(&out, "{\"sent\":0}");
  else
    aq_buf_puts(
        &out,
        "{\"error\":\"Unsupported WebSocket server operation in a browser\"}");
finish:
  aj_free(req);
  if (out.failed)
    aqua_fail(call, "Out of memory");
  else
    aqua_return_text(call, (const char *)out.data, out.len);
  aq_buf_free(&out);
}
AQUA_LIBRARY(aquaws) {
  aqua_library_state(library, create, destroy);
  aqua_class *m =
      aqua_define_module(library, "AquaWebSocketNative|網頁通訊端原生");
  aqua_define_function(m, "Command|命令", command, "ss");
}
