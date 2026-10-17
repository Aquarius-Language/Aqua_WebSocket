/* Shared test helpers: load a library built for the Aquarius buffer ABI.
 * On Windows the tests are freestanding (no CRT): libc names map onto aq_plat. */
#if defined(AQ_STATIC_TEST)
/* Freestanding static Linux/Android test (runs under qemu for arm64): the library is linked in. */
#include "aq_plat.h"
typedef int32_t (*aq_fn)(const uint8_t *, int32_t, uint8_t *, int32_t);
typedef struct { const char *name; aq_fn fn; } aq_export;
extern const aq_export g_exports[];
#if defined(__aarch64__)
static long sc3(long n, long a, long b, long c) { register long x8 __asm__("x8") = n; register long x0 __asm__("x0") = a; register long x1 __asm__("x1") = b; register long x2 __asm__("x2") = c; __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory"); return x0; }
#define NR_WRITE 64
#define NR_EXIT 93
#else
static long sc3(long n, long a, long b, long c) { long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory"); return r; }
#define NR_WRITE 1
#define NR_EXIT 60
#endif
#define snprintf aq_snprintf
#define strcmp aq_strcmp
#define strncmp aq_strncmp
#define strstr aq_strstr
#define strlen aq_strlen
#define memset aq_memset
#define malloc aq_malloc
#define free aq_free
#define exit(c) sc3(NR_EXIT, (c), 0, 0)
#define atoi(s) ((int)aq_atoll((s), 0))
static char *strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char *)s; return 0; }
static char *strrchr(const char *s, int c) { const char *r = 0; for (; *s; s++) if (*s == (char)c) r = s; return (char *)r; }
static char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
static int printf(const char *fmt, ...) {
    static char b[8192]; __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int n = aq_vsnprintf(b, sizeof b, fmt, ap); __builtin_va_end(ap);
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    sc3(NR_WRITE, 1, (long)b, n); return n;
}
#define fprintf(f, ...) printf(__VA_ARGS__)
#define stderr 0
#define RTLD_NOW 0
static void *dlopen(const char *p, int f) { (void)p; (void)f; return (void *)g_exports; }
static void *dlsym(void *lib, const char *name) { (void)lib; for (int i = 0; g_exports[i].name; i++) if (!aq_strcmp(g_exports[i].name, name)) return (void *)g_exports[i].fn; return 0; }
static const char *dlerror(void) { return "static"; }
int main(int argc, char **argv);
__attribute__((noreturn, used)) void _start_c(void) { static char *argv[] = { "test", 0 }; sc3(NR_EXIT, main(1, argv), 0, 0); for (;;) {} }
#if defined(__aarch64__)
__asm__(".global _start\n_start:\n mov x29, #0\n mov x30, #0\n bl _start_c\n");
#else
__asm__(".global _start\n_start:\n xor %rbp,%rbp\n and $-16,%rsp\n call _start_c\n");
#endif
#elif defined(_WIN32)
#include "aq_plat.h"
#define W_IMPORT __declspec(dllimport)
W_IMPORT void *__stdcall LoadLibraryA(const char *);
W_IMPORT void *__stdcall GetProcAddress(void *, const char *);
W_IMPORT void *__stdcall GetStdHandle(unsigned long);
W_IMPORT int __stdcall WriteFile(void *, const void *, unsigned long, unsigned long *, void *);
W_IMPORT void __stdcall ExitProcess(unsigned int);
#define snprintf aq_snprintf
#define strcmp aq_strcmp
#define strncmp aq_strncmp
#define strstr aq_strstr
#define strlen aq_strlen
#define memset aq_memset
#define malloc aq_malloc
#define free aq_free
#define exit(c) ExitProcess(c)
#define atoi(s) ((int)aq_atoll((s), 0))
static char *strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char *)s; return 0; }
static char *strrchr(const char *s, int c) { const char *r = 0; for (; *s; s++) if (*s == (char)c) r = s; return (char *)r; }
static char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
static int printf(const char *fmt, ...) {
    static char b[8192]; __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int n = aq_vsnprintf(b, sizeof b, fmt, ap); __builtin_va_end(ap);
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    unsigned long w; WriteFile(GetStdHandle((unsigned long)-11), b, (unsigned long)n, &w, 0); return n;
}
#define fprintf(f, ...) printf(__VA_ARGS__)
#define dlsym GetProcAddress
static const char *dlerror(void) { return "LoadLibrary failed"; }
#define dlopen(p, f) LoadLibraryA(p)
#define RTLD_NOW 0
#define stderr 0
int main(int argc, char **argv);
int __stdcall mainCRTStartup(void);
int __stdcall mainCRTStartup(void) { static char *argv[] = { "test", 0 }; ExitProcess((unsigned)main(1, argv)); return 0; }
#else
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#endif
typedef int32_t (*aq_fn)(const uint8_t *, int32_t, uint8_t *, int32_t);
static void *g_lib;
static uint8_t *g_out;
static void lib_open(const char *path) {
    g_lib = dlopen(path, RTLD_NOW);
    if (!g_lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); exit(1); }
    int32_t (*abi)(void) = (int32_t (*)(void))dlsym(g_lib, "aqua_library_abi");
    if (!abi || abi() != 1) { fprintf(stderr, "bad abi\n"); exit(1); }
    g_out = malloc(1 << 20);
}
static const char *call(const char *name, const char *json) {
    aq_fn f = (aq_fn)dlsym(g_lib, name);
    if (!f) { fprintf(stderr, "missing %s\n", name); exit(1); }
    int n = f((const uint8_t *)json, (int32_t)strlen(json), g_out, 1 << 20);
    if (n < 0) { fprintf(stderr, "%s failed %d\n", name, n); exit(1); }
    g_out[n] = 0;
    return (const char *)g_out;
}
static int json_int(const char *s, const char *key) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(s, pat); if (!p) return -1;
    return atoi(p + strlen(pat));
}
