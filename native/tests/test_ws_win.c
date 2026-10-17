/* Freestanding Windows test: loads aquaws.dll and runs a loopback session. */
#include "aq_plat.h"
#define W_IMPORT __declspec(dllimport)
W_IMPORT void *__stdcall LoadLibraryA(const char *);
W_IMPORT void *__stdcall GetProcAddress(void *, const char *);
W_IMPORT void *__stdcall GetStdHandle(unsigned long);
W_IMPORT int __stdcall WriteFile(void *, const void *, unsigned long, unsigned long *, void *);
W_IMPORT void __stdcall ExitProcess(unsigned int);
typedef int32_t (*aq_fn)(const uint8_t *, int32_t, uint8_t *, int32_t);
static void *lib; static uint8_t *out;
static void say(const char *s) { unsigned long w; WriteFile(GetStdHandle((unsigned long)-11), s, (unsigned long)aq_strlen(s), &w, 0); }
static const char *call(const char *name, const char *json) {
    aq_fn f = (aq_fn)GetProcAddress(lib, name);
    if (!f) { say("missing export\n"); ExitProcess(2); }
    int n = f((const uint8_t *)json, (int32_t)aq_strlen(json), out, 1 << 20);
    out[n < 0 ? 0 : n] = 0;
    return (const char *)out;
}
static int jint(const char *s, const char *key) {
    char pat[64]; aq_snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = aq_strstr(s, pat); if (!p) return -1;
    return (int)aq_atoll(p + aq_strlen(pat), 0);
}
int __stdcall mainCRTStartup(void) {
    lib = LoadLibraryA("aquaws.dll");
    if (!lib) { say("LoadLibrary failed\n"); ExitProcess(3); }
    out = (uint8_t *)aq_malloc(1 << 20);
    static char req[400000], line[600];
    int sctx = jint(call("aqws_open", "{}"), "ctx"), cctx = jint(call("aqws_open", "{}"), "ctx");
    aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"host\":\"127.0.0.1\",\"port\":0}", sctx);
    const char *r = call("aqws_listen", req); say(r); say("\n");
    int port = jint(r, "port");
    aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"url\":\"ws://localhost:%d/w\"}", cctx, port);
    r = call("aqws_connect", req); say(r); say("\n");
    int conn = jint(r, "conn"), got = 0, big = 0;
    for (int i = 0; i < 300 && !(got && big); i++) {
        aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":5}", sctx);
        r = call("aqws_poll", req);
        if (aq_strstr(r, "\"open\"")) { int sc = jint(aq_strstr(r, "\"open\""), "conn"); aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":\"hi from windows server\"}", sctx, sc); call("aqws_send", req); say("server open\n"); }
        if (aq_strstr(r, "\"message\"")) { aq_snprintf(line, sizeof line, "server got message len=%d\n", (int)aq_strlen(r)); say(line); big = aq_strlen(r) > 100000; }
        aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":5}", cctx);
        r = call("aqws_poll", req);
        if (aq_strstr(r, "hi from windows server")) {
            got = 1; say("client got greeting\n");
            int n = aq_snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":\"", cctx, conn);
            for (int k = 0; k < 200000; k++) req[n++] = 'x';
            req[n++] = '"'; req[n++] = '}'; req[n] = 0;
            call("aqws_send", req);
        }
    }
    say(call("aqws_random", "{\"bytes\":4}")); say("\n");
    say(got && big ? "WINDOWS_OK\n" : "WINDOWS_FAIL\n");
    ExitProcess(got && big ? 0 : 1);
    return 0;
}
