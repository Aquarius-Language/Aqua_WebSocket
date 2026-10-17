#include "harness.h"
#include <time.h>
int main(int argc, char **argv) {
    lib_open(argv[1]);
    int port = atoi(argv[2]); int seconds = atoi(argv[3]);
    static char req[1100000];
    int ctx = json_int(call("aqws_open", "{}"), "ctx");
    snprintf(req, sizeof req, "{\"ctx\":%d,\"host\":\"0.0.0.0\",\"port\":%d,\"pingInterval\":1000}", ctx, port);
    printf("%s\n", call("aqws_listen", req)); fflush(stdout);
    time_t end = time(0) + seconds;
    while (time(0) < end) {
        snprintf(req, sizeof req, "{\"ctx\":%d,\"timeout\":20}", ctx);
        const char *r = call("aqws_poll", req);
        if (!strstr(r, "\"type\"")) continue;
        static char copy[1100000]; strcpy(copy, r);
        printf("EV %.160s\n", copy); fflush(stdout);
        /* echo every message event (text and binary) */
        char *p = copy;
        while ((p = strstr(p, "{\"type\":\"")) ) {
            char *t = p + 9;
            int conn = json_int(p, "conn");
            if (!strncmp(t, "message", 7)) {
                char *txt = strstr(p, "\"text\":") + 7;
                /* find end of JSON string */
                char *e = txt + 1; while (*e && !(*e == '"' && e[-1] != '\\')) e++;
                int n = snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"text\":", ctx, conn);
                memcpy(req + n, txt, e - txt + 1); n += e - txt + 1; strcpy(req + n, "}");
                call("aqws_send", req);
            } else if (!strncmp(t, "binary", 6)) {
                char *d = strstr(p, "\"bytes\":") + 8; char *e = strchr(d, ']');
                int n = snprintf(req, sizeof req, "{\"ctx\":%d,\"conn\":%d,\"bytes\":", ctx, conn);
                memcpy(req + n, d, e - d + 1); n += e - d + 1; strcpy(req + n, "}");
                call("aqws_send", req);
            }
            p = t;
        }
    }
    return 0;
}
