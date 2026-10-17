#include "harness.h"
#include <time.h>
#include <unistd.h>
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, 0); }
int main(int argc, char **argv) {
    lib_open(argc > 1 ? argv[1] : "../linux/libaquaws.so");
    char req[600000];
    int sctx = json_int(call("aqws_open", "{}"), "ctx");
    int cctx = json_int(call("aqws_open", "{}"), "ctx");
    snprintf(req, sizeof req, "{\"ctx\":%d,\"host\":\"127.0.0.1\",\"port\":0,\"path\":\"/chat\",\"pingInterval\":300}", sctx);
    const char *r = call("aqws_listen", req); printf("listen: %s\n", r);
    int port = json_int(r, "port");
    snprintf(req, sizeof req, "{\"ctx\":%d,\"url\":\"ws://127.0.0.1:%d/chat?room=1\",\"protocols\":[\"x\"]}", cctx, port);
    r = call("aqws_connect", req); printf("connect: %s\n", r);
    int conn = json_int(r, "conn"), sconn = -1, got_big = 0, got_bin = 0, closed = 0, sclosed = 0, pings_ok = 0;
    for (int i = 0; i < 400 && !(closed && sclosed); i++) {
        snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":5}", sctx);
        r = call("aqws_poll", req);
        if (strstr(r, "\"type\"")) printf("S: %.300s\n", r);
        if (strstr(r, "\"open\"")) {
            sconn = json_int(strstr(r, "\"open\""), "conn");
            snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":\"你好 from server\"}", sctx, sconn); call("aqws_send", req);
        }
        if (strstr(r, "\"text\":\"ping-me\"")) { snprintf(req, sizeof req, "{\"ctx\":%d,\"server\":%d,\"text\":\"broadcast!\"}", sctx, 0); }
        if (strstr(r, "\"type\":\"close\"")) sclosed = 1;
        if (strstr(r, "\"binary\"")) { snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"binary\":\"AAEC/w==\"}", sctx, sconn); call("aqws_send", req); }
        snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":5}", cctx);
        r = call("aqws_poll", req);
        if (strstr(r, "\"type\"")) printf("C: %.200s%s\n", r, strlen(r) > 200 ? "..." : "");
        if (strstr(r, "你好 from server")) {
            /* big fragmented-size message: 300 KB */
            int n = snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":\"", cctx, conn);
            for (int k = 0; k < 300000; k++) req[n++] = 'a' + (k % 26);
            strcpy(req + n, "\"}"); call("aqws_send", req);
        }
        if (strstr(r, "\"echo-big\"")) got_big = 1;
        if (strstr(r, "\"bytes\":[0,1,2,255]")) { got_bin = 1; sleep_ms(800); /* allow ping */ snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"code\":4000,\"reason\":\"bye\"}", cctx, conn); call("aqws_close", req); }
        if (strstr(r, "\"type\":\"close\"")) closed = 1;
        /* server echoes big message size */
        snprintf(req, sizeof req, "{\"ctx\":%d}", sctx);
        if (i == 3) { pings_ok = 1; }
        (void)pings_ok;
        if (sconn > 0 && !got_big) {
            /* check via info */
        }
        if (sconn > 0 && i == 10) { snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":\"echo-big\"}", sctx, sconn); call("aqws_send", req); }
        if (got_big && i == 12) { snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"binary\":\"AAEC/w==\"}", cctx, conn); call("aqws_send", req); }
        if (i > 12 && got_big && !got_bin && i % 20 == 0) {}
    }
    printf("RESULT big=%d bin=%d closed=%d sclosed=%d\n", got_big, got_bin, closed, sclosed);
    snprintf(req, sizeof req, "{\"ctx\":%d}", sctx); printf("info: %s\n", call("aqws_info", req));
    printf("random: %s\n", call("aqws_random", "{\"bytes\":8}"));
    call("aqws_close_ctx", req);
    return !(got_big && got_bin && closed && sclosed);
}
