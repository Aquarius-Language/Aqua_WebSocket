/* Static freestanding test for Linux/Android targets (runs under qemu for arm64). */
#include "aq_plat.h"
int32_t aqws_open(const uint8_t *, int32_t, uint8_t *, int32_t);
int32_t aqws_listen(const uint8_t *, int32_t, uint8_t *, int32_t);
int32_t aqws_connect(const uint8_t *, int32_t, uint8_t *, int32_t);
int32_t aqws_poll(const uint8_t *, int32_t, uint8_t *, int32_t);
int32_t aqws_send(const uint8_t *, int32_t, uint8_t *, int32_t);
int32_t aqws_random(const uint8_t *, int32_t, uint8_t *, int32_t);
typedef int32_t (*aq_fn)(const uint8_t *, int32_t, uint8_t *, int32_t);
#if defined(__aarch64__)
static long sc3(long n, long a, long b, long c) { register long x8 __asm__("x8") = n; register long x0 __asm__("x0") = a; register long x1 __asm__("x1") = b; register long x2 __asm__("x2") = c; __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory"); return x0; }
#define NR_WRITE 64
#define NR_EXIT 93
#else
static long sc3(long n, long a, long b, long c) { long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory"); return r; }
#define NR_WRITE 1
#define NR_EXIT 60
#endif
static uint8_t out[1 << 20];
static void say(const char *s) { sc3(NR_WRITE, 1, (long)s, (long)aq_strlen(s)); }
static const char *call(aq_fn f, const char *json) { int n = f((const uint8_t *)json, (int32_t)aq_strlen(json), out, sizeof out); out[n < 0 ? 0 : n] = 0; return (const char *)out; }
static int jint(const char *s, const char *key) { char pat[64]; aq_snprintf(pat, sizeof pat, "\"%s\":", key); const char *p = aq_strstr(s, pat); return p ? (int)aq_atoll(p + aq_strlen(pat), 0) : -1; }
static char req[300000];
int test_main(void) {
    int sctx = jint(call(aqws_open, "{}"), "ctx"), cctx = jint(call(aqws_open, "{}"), "ctx");
    aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"host\":\"0.0.0.0\",\"port\":0}", sctx);
    const char *r = call(aqws_listen, req); say(r); say("\n");
    int port = jint(r, "port");
    aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"url\":\"ws://127.0.0.1:%d/a\"}", cctx, port);
    r = call(aqws_connect, req); say(r); say("\n");
    int conn = jint(r, "conn"), ok = 0;
    for (int i = 0; i < 300 && !ok; i++) {
        aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":5}", sctx);
        r = call(aqws_poll, req);
        if (aq_strstr(r, "\"open\"")) { say("server open: "); say(r); say("\n"); }
        if (aq_strstr(r, "\"message\"")) { say("server got: "); say(r); say("\n"); ok = aq_strstr(r, "arm64 hello") != 0; }
        aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":5}", cctx);
        r = call(aqws_poll, req);
        if (aq_strstr(r, "\"open\"")) { aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":\"arm64 hello 世界\"}", cctx, conn); call(aqws_send, req); }
    }
    say(call(aqws_random, "{}")); say("\n");
    say(ok ? "STATIC_OK\n" : "STATIC_FAIL\n");
    return ok ? 0 : 1;
}
__attribute__((noreturn)) void _start_c(void) { sc3(NR_EXIT, test_main(), 0, 0); for (;;) {} }
#if defined(__aarch64__)
__asm__(".global _start\n_start:\n mov x29, #0\n mov x30, #0\n bl _start_c\n");
#else
__asm__(".global _start\n_start:\n xor %rbp,%rbp\n and $-16,%rsp\n call _start_c\n");
#endif
