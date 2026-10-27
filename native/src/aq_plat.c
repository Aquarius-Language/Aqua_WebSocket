/*
 * aq_plat.c - Freestanding platform layer. See aq_plat.h.
 * MIT License, Copyright (c) 2026 Temple Lin.
 */
#include "aq_plat.h"
#if defined(AQ_HOSTED_LIBC)
#include <string.h>
#endif

/* =====================================================================
 *  Operating system bindings
 * ===================================================================== */
#if defined(AQ_WINDOWS)

typedef uintptr_t W_SOCKET;
typedef struct W_WSAPOLLFD { W_SOCKET fd; short events; short revents; } W_WSAPOLLFD;
typedef struct W_ADDRINFO {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    void *ai_addr;
    struct W_ADDRINFO *ai_next;
} W_ADDRINFO;
#define W_IMPORT __declspec(dllimport)
#define W_API __stdcall
W_IMPORT int W_API WSAStartup(unsigned short version, void *data);
W_IMPORT W_SOCKET W_API socket(int af, int type, int protocol);
W_IMPORT int W_API bind(W_SOCKET s, const void *addr, int len);
W_IMPORT int W_API listen(W_SOCKET s, int backlog);
W_IMPORT W_SOCKET W_API accept(W_SOCKET s, void *addr, int *len);
W_IMPORT int W_API connect(W_SOCKET s, const void *addr, int len);
W_IMPORT int W_API send(W_SOCKET s, const char *buf, int len, int flags);
W_IMPORT int W_API recv(W_SOCKET s, char *buf, int len, int flags);
W_IMPORT int W_API sendto(W_SOCKET s, const char *buf, int len, int flags, const void *to, int tolen);
W_IMPORT int W_API recvfrom(W_SOCKET s, char *buf, int len, int flags, void *from, int *fromlen);
W_IMPORT int W_API closesocket(W_SOCKET s);
W_IMPORT int W_API ioctlsocket(W_SOCKET s, long cmd, unsigned long *arg);
W_IMPORT int W_API setsockopt(W_SOCKET s, int level, int name, const char *val, int len);
W_IMPORT int W_API getsockopt(W_SOCKET s, int level, int name, char *val, int *len);
W_IMPORT int W_API getsockname(W_SOCKET s, void *addr, int *len);
W_IMPORT int W_API WSAPoll(W_WSAPOLLFD *fds, unsigned long n, int timeout);
W_IMPORT int W_API WSAGetLastError(void);
W_IMPORT int W_API WSAIoctl(W_SOCKET s, unsigned long code, void *in, unsigned long inlen, void *out, unsigned long outlen, unsigned long *ret, void *ov, void *cb);
W_IMPORT int W_API getaddrinfo(const char *node, const char *service, const W_ADDRINFO *hints, W_ADDRINFO **res);
W_IMPORT void W_API freeaddrinfo(W_ADDRINFO *ai);
W_IMPORT int W_API gethostname(char *name, int len);
W_IMPORT void *W_API GetProcessHeap(void);
W_IMPORT void *W_API HeapAlloc(void *heap, unsigned long flags, size_t bytes);
W_IMPORT void *W_API HeapReAlloc(void *heap, unsigned long flags, void *mem, size_t bytes);
W_IMPORT int W_API HeapFree(void *heap, unsigned long flags, void *mem);
W_IMPORT int W_API QueryPerformanceCounter(int64_t *count);
W_IMPORT int W_API QueryPerformanceFrequency(int64_t *freq);
W_IMPORT void W_API GetSystemTimeAsFileTime(uint64_t *ft);
W_IMPORT int W_API SwitchToThread(void);
W_IMPORT void W_API Sleep(unsigned long ms);
W_IMPORT long W_API BCryptGenRandom(void *alg, unsigned char *buf, unsigned long len, unsigned long flags);

#define W_AF_INET 2
#define W_AF_INET6 23
#define W_SOCK_STREAM 1
#define W_SOCK_DGRAM 2
#define W_IPPROTO_TCP 6
#define W_IPPROTO_UDP 17
#define W_IPPROTO_IPV6 41
#define W_IPV6_V6ONLY 27
#define W_SOL_SOCKET 0xffff
#define W_SO_REUSEADDR 4
#define W_SO_EXCLUSIVEADDRUSE (~4)
#define W_SO_ERROR 0x1007
#define W_TCP_NODELAY 1
#define W_FIONBIO 0x8004667EL
#define W_SIO_UDP_CONNRESET 0x9800000CUL
#define W_WSAEWOULDBLOCK 10035
#define W_WSAEINPROGRESS 10036
#define W_WSAECONNRESET 10054
#define W_WSAEMSGSIZE 10040
#define W_INVALID_SOCKET ((W_SOCKET)~(uintptr_t)0)
#define W_POLLRDNORM 0x0100
#define W_POLLRDBAND 0x0200
#define W_POLLIN (W_POLLRDNORM | W_POLLRDBAND)
#define W_POLLWRNORM 0x0010
#define W_POLLOUT W_POLLWRNORM
#define W_POLLERR 0x0001
#define W_POLLHUP 0x0002
#define W_POLLNVAL 0x0004
#define AF4 W_AF_INET
#define AF6 W_AF_INET6

#else /* Linux and Android: raw system calls */

#if defined(__x86_64__)
#  define SYS_read 0
#  define SYS_write 1
#  define SYS_close 3
#  define SYS_mmap 9
#  define SYS_munmap 11
#  define SYS_ioctl 16
#  define SYS_sched_yield 24
#  define SYS_socket 41
#  define SYS_connect 42
#  define SYS_sendto 44
#  define SYS_recvfrom 45
#  define SYS_bind 49
#  define SYS_listen 50
#  define SYS_getsockname 51
#  define SYS_setsockopt 54
#  define SYS_getsockopt 55
#  define SYS_clock_gettime 228
#  define SYS_ppoll 271
#  define SYS_accept4 288
#  define SYS_getrandom 318
#  define SYS_openat 257
static inline long aq_sys6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#elif defined(__aarch64__)
#  define SYS_read 63
#  define SYS_write 64
#  define SYS_close 57
#  define SYS_mmap 222
#  define SYS_munmap 215
#  define SYS_ioctl 29
#  define SYS_sched_yield 124
#  define SYS_socket 198
#  define SYS_connect 203
#  define SYS_sendto 206
#  define SYS_recvfrom 207
#  define SYS_bind 200
#  define SYS_listen 201
#  define SYS_getsockname 204
#  define SYS_setsockopt 208
#  define SYS_getsockopt 209
#  define SYS_clock_gettime 113
#  define SYS_ppoll 73
#  define SYS_accept4 242
#  define SYS_getrandom 278
#  define SYS_openat 56
static inline long aq_sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile ("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#else
#  error "Unsupported architecture"
#endif
#define sys0(n) aq_sys6(n,0,0,0,0,0,0)
#define sys1(n,a) aq_sys6(n,(long)(a),0,0,0,0,0)
#define sys2(n,a,b) aq_sys6(n,(long)(a),(long)(b),0,0,0,0)
#define sys3(n,a,b,c) aq_sys6(n,(long)(a),(long)(b),(long)(c),0,0,0)
#define sys4(n,a,b,c,d) aq_sys6(n,(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define sys5(n,a,b,c,d,e) aq_sys6(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
#define sys6(n,a,b,c,d,e,f) aq_sys6(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),(long)(f))

#define L_AF_INET 2
#define L_AF_INET6 10
#define L_SOCK_STREAM 1
#define L_SOCK_DGRAM 2
#define L_SOCK_NONBLOCK 04000
#define L_SOCK_CLOEXEC 02000000
#define L_SOL_SOCKET 1
#define L_SO_REUSEADDR 2
#define L_SO_ERROR 4
#define L_IPPROTO_TCP 6
#define L_IPPROTO_IPV6 41
#define L_IPV6_V6ONLY 26
#define L_TCP_NODELAY 1
#define L_MSG_NOSIGNAL 0x4000
#define L_MSG_DONTWAIT 0x40
#define L_EAGAIN 11
#define L_EINTR 4
#define L_EINPROGRESS 115
#define L_ECONNREFUSED 111
#define L_POLLIN 1
#define L_POLLOUT 4
#define L_POLLERR 8
#define L_POLLHUP 16
#define L_POLLNVAL 32
#define L_SIOCGIFCONF 0x8912
#define AF4 L_AF_INET
#define AF6 L_AF_INET6
struct l_timespec { long sec; long nsec; };
struct l_pollfd { int fd; short events; short revents; };
#endif

/* =====================================================================
 *  Freestanding C runtime shims (the compiler may emit calls to these).
 * ===================================================================== */
#if !defined(AQ_HOSTED_LIBC)
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

__attribute__((used)) void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
    while (n >= 8 && (((uintptr_t)dp | (uintptr_t)sp) & 7) == 0) { *(uint64_t *)(void *)dp = *(const uint64_t *)(const void *)sp; dp += 8; sp += 8; n -= 8; }
    while (n--) *dp++ = *sp++;
    return d;
}
__attribute__((used)) void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
    if (dp == sp || n == 0) return d;
    if (dp < sp || dp >= sp + n) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
__attribute__((used)) void *memset(void *d, int c, size_t n) {
    volatile uint8_t *dp = (volatile uint8_t *)d;   /* volatile: keep the compiler from turning this into a memset call */
    while (n--) *dp++ = (uint8_t)c;
    return d;
}
__attribute__((used)) int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
__attribute__((used)) size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
#if defined(AQ_WINDOWS)
/* clang emits these for floating point use and large stack frames on Windows. */
int _fltused = 0;
/* Stack probe for frames larger than a page (MSVC ABI: size in RAX, RSP unchanged). */
__asm__(
    ".globl __chkstk\n"
    "__chkstk:\n"
    "    pushq %rcx\n"
    "    pushq %rax\n"
    "    cmpq $0x1000, %rax\n"
    "    leaq 24(%rsp), %rcx\n"
    "    jb 1f\n"
    "2:\n"
    "    subq $0x1000, %rcx\n"
    "    testq %rcx, (%rcx)\n"
    "    subq $0x1000, %rax\n"
    "    cmpq $0x1000, %rax\n"
    "    ja 2b\n"
    "1:\n"
    "    subq %rax, %rcx\n"
    "    testq %rcx, (%rcx)\n"
    "    popq %rax\n"
    "    popq %rcx\n"
    "    ret\n");
#endif
#endif

void *aq_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
void *aq_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
void *aq_memset(void *d, int c, size_t n) { return memset(d, c, n); }
int aq_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
size_t aq_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int aq_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int aq_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int aq_strcasecmp(const char *a, const char *b) {
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}
int aq_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int x = lower((unsigned char)a[i]), y = lower((unsigned char)b[i]);
        if (x != y || !x) return x - y;
    }
    return 0;
}
const char *aq_strstr(const char *h, const char *n) {
    size_t ln = aq_strlen(n);
    if (!ln) return h;
    for (; *h; h++) if (*h == *n && aq_strncmp(h, n, ln) == 0) return h;
    return 0;
}
const char *aq_strcasestr(const char *h, const char *n) {
    size_t ln = aq_strlen(n);
    if (!ln) return h;
    for (; *h; h++) if (aq_strncasecmp(h, n, ln) == 0) return h;
    return 0;
}
long long aq_atoll(const char *s, const char **end) {
    long long v = 0; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (end) *end = s;
    return neg ? -v : v;
}

/* Minimal vsnprintf: %d %i %u %x %X %c %s %p %% with flags '-', '0', width,
 * precision (strings and '.*') and length modifiers h, l, ll, z. */
int aq_vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    size_t pos = 0;
#define PUT(ch) do { if (pos + 1 < cap) buf[pos] = (char)(ch); pos++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
        for (;; fmt++) { if (*fmt == '-') left = 1; else if (*fmt == '0') zero = 1; else if (*fmt == '+' || *fmt == ' ' || *fmt == '#') {} else break; }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++; prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') lng++; else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') lng = 2;
            fmt++;
        }
        char tmp[32]; const char *s = tmp; int len = 0;
        switch (*fmt) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
            char rev[24]; int r = 0;
            do { rev[r++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (v < 0) tmp[len++] = '-';
            while (r) tmp[len++] = rev[--r];
            break;
        }
        case 'u': case 'x': case 'X': case 'p': {
            unsigned long long u;
            if (*fmt == 'p') { u = (uintptr_t)va_arg(ap, void *); }
            else u = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            int base = *fmt == 'u' ? 10 : 16;
            const char *digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char rev[24]; int r = 0;
            do { rev[r++] = digits[u % (unsigned)base]; u /= (unsigned)base; } while (u);
            while (r) tmp[len++] = rev[--r];
            break;
        }
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': {
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = 0; while (s[len] && (prec < 0 || len < prec)) len++;
            break;
        }
        case '%': tmp[0] = '%'; len = 1; break;
        default: tmp[0] = '%'; tmp[1] = *fmt; len = 2; if (!*fmt) { fmt--; len = 1; } break;
        }
        int pad = width > len ? width - len : 0;
        if (!left) {
            if (zero && *fmt != 's' && s == tmp && len && tmp[0] == '-') { PUT('-'); s++; len--; while (pad--) PUT('0'); pad = 0; }
            while (pad > 0) { PUT(zero && *fmt != 's' ? '0' : ' '); pad--; }
        }
        for (int i = 0; i < len; i++) PUT(s[i]);
        while (left && pad-- > 0) PUT(' ');
    }
    if (cap) buf[pos < cap ? pos : cap - 1] = 0;
#undef PUT
    return (int)pos;
}
int aq_snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = aq_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* =====================================================================
 *  Locking
 * ===================================================================== */
static volatile int g_lock;
void aq_lock(void) {
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) {
#if defined(AQ_WINDOWS)
        SwitchToThread();
#else
        sys0(SYS_sched_yield);
#endif
    }
}
void aq_unlock(void) { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }

/* =====================================================================
 *  Memory
 * ===================================================================== */
#if defined(AQ_WINDOWS)
void *aq_malloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }
void *aq_calloc(size_t c, size_t s) {
    if (s && c > (size_t)-1 / s) return 0;
    return HeapAlloc(GetProcessHeap(), 8 /* HEAP_ZERO_MEMORY */, c * s ? c * s : 1);
}
void *aq_realloc(void *p, size_t n) {
    if (!p) return aq_malloc(n);
    if (!n) { aq_free(p); return 0; }
    return HeapReAlloc(GetProcessHeap(), 0, p, n);
}
void aq_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
#else
/* Segregated free lists over mmap arenas. Blocks carry a 16 byte header whose
 * first word holds the class index (0..NCLASS-1) or, for large blocks, the
 * mapping size with the top bit set. */
#define NCLASS 13          /* 32 B .. 128 KiB */
#define ARENA_BYTES (1u << 20)
#define LARGE_FLAG ((size_t)1 << (sizeof(size_t) * 8 - 1))
typedef struct free_block { struct free_block *next; } free_block;
static free_block *g_free[NCLASS];
static uint8_t *g_arena_cur, *g_arena_end;
static void *os_map(size_t n) {
    long r = sys6(SYS_mmap, 0, n, 3 /*RW*/, 0x22 /*PRIVATE|ANON*/, -1, 0);
    if (r < 0 && r > -4096) return 0;
    return (void *)r;
}
static void os_unmap(void *p, size_t n) { sys2(SYS_munmap, p, n); }
static int size_class(size_t n) {
    size_t need = n + 16; int c = 0; size_t sz = 32;
    while (sz < need) { sz <<= 1; c++; }
    return c;
}
void *aq_malloc(size_t n) {
    if (n == 0) n = 1;
    if (n + 16 > ((size_t)32 << (NCLASS - 1))) {
        size_t total = (n + 16 + 4095) & ~(size_t)4095;
        uint8_t *m = (uint8_t *)os_map(total);
        if (!m) return 0;
        *(size_t *)(void *)m = total | LARGE_FLAG;
        return m + 16;
    }
    int c = size_class(n);
    size_t sz = (size_t)32 << c;
    if (g_free[c]) {
        free_block *b = g_free[c]; g_free[c] = b->next;
        uint8_t *m = (uint8_t *)b - 16;
        *(size_t *)(void *)m = (size_t)c;
        return m + 16;
    }
    if (!g_arena_cur || (size_t)(g_arena_end - g_arena_cur) < sz) {
        size_t abytes = sz > ARENA_BYTES ? sz : ARENA_BYTES;
        uint8_t *a = (uint8_t *)os_map(abytes);
        if (!a) return 0;
        /* Return the unused tail of the previous arena to the free lists. */
        while (g_arena_cur && g_arena_end - g_arena_cur >= 32) {
            size_t rest = (size_t)(g_arena_end - g_arena_cur); int rc = 0; size_t rs = 32;
            while ((rs << 1) <= rest && rc + 1 < NCLASS) { rs <<= 1; rc++; }
            free_block *fb = (free_block *)(void *)(g_arena_cur + 16);
            fb->next = g_free[rc]; g_free[rc] = fb; g_arena_cur += rs;
        }
        g_arena_cur = a; g_arena_end = a + abytes;
    }
    uint8_t *m = g_arena_cur; g_arena_cur += sz;
    *(size_t *)(void *)m = (size_t)c;
    return m + 16;
}
static size_t block_capacity(void *p) {
    size_t h = *(size_t *)(void *)((uint8_t *)p - 16);
    if (h & LARGE_FLAG) return (h & ~LARGE_FLAG) - 16;
    return ((size_t)32 << h) - 16;
}
void aq_free(void *p) {
    if (!p) return;
    uint8_t *m = (uint8_t *)p - 16;
    size_t h = *(size_t *)(void *)m;
    if (h & LARGE_FLAG) { os_unmap(m, h & ~LARGE_FLAG); return; }
    if (h >= NCLASS) return; /* corrupted header: leak rather than crash */
    free_block *b = (free_block *)p; b->next = g_free[h]; g_free[h] = b;
}
void *aq_calloc(size_t c, size_t s) {
    if (s && c > (size_t)-1 / s) return 0;
    void *p = aq_malloc(c * s);
    if (p) aq_memset(p, 0, c * s ? c * s : 1);
    return p;
}
void *aq_realloc(void *p, size_t n) {
    if (!p) return aq_malloc(n);
    if (!n) { aq_free(p); return 0; }
    size_t cap = block_capacity(p);
    if (n <= cap) return p;
    void *q = aq_malloc(n);
    if (!q) return 0;
    aq_memcpy(q, p, cap);
    aq_free(p);
    return q;
}
#endif
char *aq_strndup(const char *s, size_t n) {
    char *d = (char *)aq_malloc(n + 1);
    if (!d) return 0;
    aq_memcpy(d, s, n); d[n] = 0;
    return d;
}
char *aq_strdup(const char *s) { return s ? aq_strndup(s, aq_strlen(s)) : 0; }

/* =====================================================================
 *  Time and randomness
 * ===================================================================== */
uint64_t aq_now_us(void) {
#if defined(AQ_WINDOWS)
    static int64_t freq;
    int64_t c;
    if (!freq) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (uint64_t)((c / freq) * 1000000 + (c % freq) * 1000000 / freq);
#else
    struct l_timespec ts = {0, 0};
    sys2(SYS_clock_gettime, 1 /*MONOTONIC*/, &ts);
    return (uint64_t)ts.sec * 1000000u + (uint64_t)ts.nsec / 1000u;
#endif
}
uint64_t aq_now_ms(void) { return aq_now_us() / 1000u; }
uint64_t aq_wall_seconds(void) {
#if defined(AQ_WINDOWS)
    uint64_t ft = 0; GetSystemTimeAsFileTime(&ft);
    return ft / 10000000u - 11644473600u;
#else
    struct l_timespec ts = {0, 0};
    sys2(SYS_clock_gettime, 0 /*REALTIME*/, &ts);
    return (uint64_t)ts.sec;
#endif
}
static uint64_t g_rng_state[2];
static uint64_t xorshift(void) {
    uint64_t s1 = g_rng_state[0], s0 = g_rng_state[1];
    g_rng_state[0] = s0; s1 ^= s1 << 23;
    g_rng_state[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
    return g_rng_state[1] + s0;
}
int aq_random(void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
#if defined(AQ_WINDOWS)
    if (BCryptGenRandom(0, p, (unsigned long)n, 2 /* BCRYPT_USE_SYSTEM_PREFERRED_RNG */) == 0) return 0;
#else
    size_t got = 0;
    while (got < n) {
        long r = sys3(SYS_getrandom, p + got, n - got, 0);
        if (r == -L_EINTR) continue;
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (got == n) return 0;
    /* Fallback: /dev/urandom. */
    long fd = sys4(SYS_openat, -100 /*AT_FDCWD*/, "/dev/urandom", 0, 0);
    if (fd >= 0) {
        while (got < n) {
            long r = sys3(SYS_read, fd, p + got, n - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        sys1(SYS_close, fd);
        if (got == n) return 0;
    }
#endif
    /* Last resort, not cryptographically strong. */
    if (!g_rng_state[0]) { g_rng_state[0] = aq_now_us() | 1; g_rng_state[1] = (uintptr_t)buf ^ 0x9E3779B97F4A7C15ull; }
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(xorshift() >> 24);
    return -1;
}
uint32_t aq_random_u32(void) { uint32_t v = 0; aq_random(&v, sizeof v); return v; }

/* =====================================================================
 *  Addresses
 * ===================================================================== */
static int parse_ipv4(const char *s, uint8_t out[4], const char **end) {
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return -1;
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); if (++digits > 3 || v > 255) return -1; }
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return -1; s++; }
    }
    if (end) *end = s;
    return 0;
}
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int parse_ipv6(const char *s, uint8_t out[16]) {
    uint16_t words[8]; int n = 0, gap = -1;
    if (s[0] == ':' && s[1] == ':') { gap = 0; s += 2; if (!*s) goto done; }
    while (*s && *s != '%') {
        if (n >= 8) return -1;
        /* embedded IPv4 */
        const char *q = s; int dots = 0;
        while (*q && *q != ':' && *q != '%') { if (*q == '.') dots++; q++; }
        if (dots == 3) {
            uint8_t v4[4]; const char *e;
            if (n > 6 || parse_ipv4(s, v4, &e) < 0) return -1;
            words[n++] = (uint16_t)((v4[0] << 8) | v4[1]); words[n++] = (uint16_t)((v4[2] << 8) | v4[3]);
            s = e; break;
        }
        int v = 0, digits = 0;
        while (hexval(*s) >= 0) { v = (v << 4) | hexval(*s++); if (++digits > 4) return -1; }
        if (!digits) return -1;
        words[n++] = (uint16_t)v;
        if (*s == ':') {
            s++;
            if (*s == ':') { if (gap >= 0) return -1; gap = n; s++; if (!*s || *s == '%') break; }
            else if (!*s) return -1;
        } else if (*s && *s != '%') return -1;
    }
done:
    if (gap < 0 && n != 8) return -1;
    if (gap >= 0 && n > 7) return -1;
    {
        uint16_t full[8] = {0};
        if (gap >= 0) {
            for (int i = 0; i < gap; i++) full[i] = words[i];
            int tail = n - gap;
            for (int i = 0; i < tail; i++) full[8 - tail + i] = words[gap + i];
        } else for (int i = 0; i < 8; i++) full[i] = words[i];
        for (int i = 0; i < 8; i++) { out[i * 2] = (uint8_t)(full[i] >> 8); out[i * 2 + 1] = (uint8_t)full[i]; }
    }
    return 0;
}
int aq_addr_parse(const char *text, uint16_t port, aq_addr *out) {
    aq_memset(out, 0, sizeof *out);
    out->port = port;
    if (!text || !*text) return -1;
    if (aq_strcasecmp(text, "localhost") == 0) { out->family = 4; out->ip[0] = 127; out->ip[3] = 1; return 0; }
    const char *e;
    if (parse_ipv4(text, out->ip, &e) == 0 && *e == 0) { out->family = 4; return 0; }
    char tmp[64]; size_t n = aq_strlen(text);
    if (text[0] == '[' && n >= 2 && n < sizeof tmp && text[n - 1] == ']') { aq_memcpy(tmp, text + 1, n - 2); tmp[n - 2] = 0; text = tmp; }
    if (parse_ipv6(text, out->ip) == 0) { out->family = 6; return 0; }
    return -1;
}
int aq_addr_format(const aq_addr *a, char *buf, size_t cap, int with_port) {
    if (a->family == 4) {
        if (with_port) return aq_snprintf(buf, cap, "%u.%u.%u.%u:%u", a->ip[0], a->ip[1], a->ip[2], a->ip[3], a->port);
        return aq_snprintf(buf, cap, "%u.%u.%u.%u", a->ip[0], a->ip[1], a->ip[2], a->ip[3]);
    }
    char t[48]; size_t p = 0;
    /* find longest zero run for :: compression */
    int best = -1, bestlen = 0;
    for (int i = 0; i < 8;) {
        if (a->ip[i * 2] == 0 && a->ip[i * 2 + 1] == 0) {
            int j = i; while (j < 8 && a->ip[j * 2] == 0 && a->ip[j * 2 + 1] == 0) j++;
            if (j - i > bestlen && j - i > 1) { best = i; bestlen = j - i; }
            i = j;
        } else i++;
    }
    for (int i = 0; i < 8; i++) {
        if (i == best) { t[p++] = ':'; if (i == 0) t[p++] = ':'; i += bestlen - 1; continue; }
        p += (size_t)aq_snprintf(t + p, sizeof t - p, "%x", (a->ip[i * 2] << 8) | a->ip[i * 2 + 1]);
        if (i < 7) t[p++] = ':';
    }
    t[p] = 0;
    if (with_port) return aq_snprintf(buf, cap, "[%s]:%u", t, a->port);
    return aq_snprintf(buf, cap, "%s", t);
}
int aq_addr_equal(const aq_addr *a, const aq_addr *b) {
    if (a->family != b->family || a->port != b->port) return 0;
    return aq_memcmp(a->ip, b->ip, a->family == 4 ? 4 : 16) == 0;
}
int aq_addr_is_loopback(const aq_addr *a) {
    if (a->family == 4) return a->ip[0] == 127;
    for (int i = 0; i < 15; i++) if (a->ip[i]) return 0;
    return a->ip[15] == 1;
}
int aq_addr_is_any(const aq_addr *a) {
    int n = a->family == 4 ? 4 : 16;
    for (int i = 0; i < n; i++) if (a->ip[i]) return 0;
    return 1;
}

/* sockaddr conversion: identical layouts on Windows and Linux except family values */
typedef struct sa_storage { uint8_t bytes[128]; } sa_storage;
static int to_sockaddr(const aq_addr *a, sa_storage *ss) {
    aq_memset(ss, 0, sizeof *ss);
    uint8_t *b = ss->bytes;
    if (a->family == 4) {
        *(uint16_t *)(void *)b = AF4;
        b[2] = (uint8_t)(a->port >> 8); b[3] = (uint8_t)a->port;
        aq_memcpy(b + 4, a->ip, 4);
        return 16;
    }
    *(uint16_t *)(void *)b = AF6;
    b[2] = (uint8_t)(a->port >> 8); b[3] = (uint8_t)a->port;
    aq_memcpy(b + 8, a->ip, 16);
    return 28;
}
static int from_sockaddr(const sa_storage *ss, aq_addr *a) {
    const uint8_t *b = ss->bytes;
    uint16_t fam = *(const uint16_t *)(const void *)b;
    aq_memset(a, 0, sizeof *a);
    a->port = (uint16_t)((b[2] << 8) | b[3]);
    if (fam == AF4) { a->family = 4; aq_memcpy(a->ip, b + 4, 4); return 0; }
    if (fam == AF6) {
        /* map ::ffff:a.b.c.d back to IPv4 for readability */
        static const uint8_t mapped[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if (aq_memcmp(b + 8, mapped, 12) == 0) { a->family = 4; aq_memcpy(a->ip, b + 20, 4); return 0; }
        a->family = 6; aq_memcpy(a->ip, b + 8, 16); return 0;
    }
    return -1;
}

int aq_resolve(const char *host, uint16_t port, aq_addr *out) {
    if (aq_addr_parse(host, port, out) == 0) return 0;
#if defined(AQ_WINDOWS)
    W_ADDRINFO hints, *res = 0;
    aq_memset(&hints, 0, sizeof hints);
    hints.ai_family = 0; hints.ai_socktype = W_SOCK_STREAM;
    if (getaddrinfo(host, 0, &hints, &res) != 0 || !res) return -1;
    int rc = -1;
    for (W_ADDRINFO *p = res; p; p = p->ai_next) {
        sa_storage ss; aq_memset(&ss, 0, sizeof ss);
        size_t n = p->ai_addrlen < sizeof ss ? p->ai_addrlen : sizeof ss;
        aq_memcpy(ss.bytes, p->ai_addr, n);
        if (from_sockaddr(&ss, out) == 0) { out->port = port; rc = 0; if (out->family == 4) break; }
    }
    freeaddrinfo(res);
    return rc;
#else
    return -1;
#endif
}

/* =====================================================================
 *  Sockets
 * ===================================================================== */
int aq_plat_init(void) {
#if defined(AQ_WINDOWS)
    static int done;
    if (!done) { uint8_t data[1024]; if (WSAStartup(0x0202, data) != 0) return -1; done = 1; }
#endif
    return 0;
}

#if defined(AQ_WINDOWS)
static int set_nonblocking(W_SOCKET s) { unsigned long on = 1; return ioctlsocket(s, W_FIONBIO, &on); }
#endif

aq_sock aq_tcp_listen(const aq_addr *bind_addr, int backlog, int *err) {
    sa_storage ss; int len = to_sockaddr(bind_addr, &ss); int one = 1, zero = 0;
#if defined(AQ_WINDOWS)
    W_SOCKET s = socket(bind_addr->family == 4 ? W_AF_INET : W_AF_INET6, W_SOCK_STREAM, W_IPPROTO_TCP);
    if (s == W_INVALID_SOCKET) { if (err) *err = WSAGetLastError(); return AQ_BAD_SOCK; }
    setsockopt(s, W_SOL_SOCKET, W_SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof one);
    if (bind_addr->family == 6) setsockopt(s, W_IPPROTO_IPV6, W_IPV6_V6ONLY, (const char *)&zero, sizeof zero);
    if (bind(s, ss.bytes, len) != 0 || listen(s, backlog) != 0 || set_nonblocking(s) != 0) {
        if (err) *err = WSAGetLastError(); closesocket(s); return AQ_BAD_SOCK;
    }
    return (aq_sock)s;
#else
    long s = sys3(SYS_socket, bind_addr->family == 4 ? L_AF_INET : L_AF_INET6, L_SOCK_STREAM | L_SOCK_NONBLOCK | L_SOCK_CLOEXEC, 0);
    if (s < 0) { if (err) *err = (int)-s; return AQ_BAD_SOCK; }
    sys5(SYS_setsockopt, s, L_SOL_SOCKET, L_SO_REUSEADDR, &one, sizeof one);
    if (bind_addr->family == 6) sys5(SYS_setsockopt, s, L_IPPROTO_IPV6, L_IPV6_V6ONLY, &zero, sizeof zero);
    long r = sys3(SYS_bind, s, ss.bytes, len);
    if (r == 0) r = sys2(SYS_listen, s, backlog);
    if (r != 0) { if (err) *err = (int)-r; sys1(SYS_close, s); return AQ_BAD_SOCK; }
    return (aq_sock)s;
#endif
}

aq_sock aq_tcp_accept(aq_sock listener, aq_addr *peer) {
    sa_storage ss; aq_memset(&ss, 0, sizeof ss);
    int one = 1;
#if defined(AQ_WINDOWS)
    int len = (int)sizeof ss.bytes;
    W_SOCKET s = accept((W_SOCKET)listener, ss.bytes, &len);
    if (s == W_INVALID_SOCKET) return AQ_BAD_SOCK;
    set_nonblocking(s);
    setsockopt(s, W_IPPROTO_TCP, W_TCP_NODELAY, (const char *)&one, sizeof one);
    if (peer) from_sockaddr(&ss, peer);
    return (aq_sock)s;
#else
    int len = (int)sizeof ss.bytes;
    long s = sys4(SYS_accept4, listener, ss.bytes, &len, L_SOCK_NONBLOCK | L_SOCK_CLOEXEC);
    if (s < 0) return AQ_BAD_SOCK;
    sys5(SYS_setsockopt, s, L_IPPROTO_TCP, L_TCP_NODELAY, &one, sizeof one);
    if (peer) from_sockaddr(&ss, peer);
    return (aq_sock)s;
#endif
}

aq_sock aq_tcp_connect(const aq_addr *to, int *err) {
    sa_storage ss; int len = to_sockaddr(to, &ss); int one = 1;
#if defined(AQ_WINDOWS)
    W_SOCKET s = socket(to->family == 4 ? W_AF_INET : W_AF_INET6, W_SOCK_STREAM, W_IPPROTO_TCP);
    if (s == W_INVALID_SOCKET) { if (err) *err = WSAGetLastError(); return AQ_BAD_SOCK; }
    set_nonblocking(s);
    setsockopt(s, W_IPPROTO_TCP, W_TCP_NODELAY, (const char *)&one, sizeof one);
    if (connect(s, ss.bytes, len) != 0) {
        int e = WSAGetLastError();
        if (e != W_WSAEWOULDBLOCK && e != W_WSAEINPROGRESS) { if (err) *err = e; closesocket(s); return AQ_BAD_SOCK; }
    }
    return (aq_sock)s;
#else
    long s = sys3(SYS_socket, to->family == 4 ? L_AF_INET : L_AF_INET6, L_SOCK_STREAM | L_SOCK_NONBLOCK | L_SOCK_CLOEXEC, 0);
    if (s < 0) { if (err) *err = (int)-s; return AQ_BAD_SOCK; }
    sys5(SYS_setsockopt, s, L_IPPROTO_TCP, L_TCP_NODELAY, &one, sizeof one);
    long r = sys3(SYS_connect, s, ss.bytes, len);
    if (r != 0 && r != -L_EINPROGRESS && r != -L_EAGAIN) { if (err) *err = (int)-r; sys1(SYS_close, s); return AQ_BAD_SOCK; }
    return (aq_sock)s;
#endif
}

int aq_tcp_connect_result(aq_sock s) {
    aq_pollfd p = { s, AQ_POLL_OUT, 0 };
    int n = aq_poll(&p, 1, 0);
    if (n <= 0) return 1;
    int e = 0;
#if defined(AQ_WINDOWS)
    int len = sizeof e;
    getsockopt((W_SOCKET)s, W_SOL_SOCKET, W_SO_ERROR, (char *)&e, &len);
#else
    int len = sizeof e;
    sys5(SYS_getsockopt, s, L_SOL_SOCKET, L_SO_ERROR, &e, &len);
#endif
    if (e) return -(e ? e : 1);
    if (p.revents & (AQ_POLL_ERR | AQ_POLL_HUP)) return -1;
    return (p.revents & AQ_POLL_OUT) ? 0 : 1;
}

int aq_sock_send(aq_sock s, const void *buf, int len) {
    if (len <= 0) return 0;
#if defined(AQ_WINDOWS)
    int r = send((W_SOCKET)s, (const char *)buf, len, 0);
    if (r >= 0) return r;
    return WSAGetLastError() == W_WSAEWOULDBLOCK ? AQ_IO_AGAIN : AQ_IO_ERROR;
#else
    for (;;) {
        long r = sys6(SYS_sendto, s, buf, len, L_MSG_NOSIGNAL | L_MSG_DONTWAIT, 0, 0);
        if (r >= 0) return (int)r;
        if (r == -L_EINTR) continue;
        return r == -L_EAGAIN ? AQ_IO_AGAIN : AQ_IO_ERROR;
    }
#endif
}
int aq_sock_recv(aq_sock s, void *buf, int cap) {
#if defined(AQ_WINDOWS)
    int r = recv((W_SOCKET)s, (char *)buf, cap, 0);
    if (r > 0) return r;
    if (r == 0) return AQ_IO_CLOSED;
    return WSAGetLastError() == W_WSAEWOULDBLOCK ? AQ_IO_AGAIN : AQ_IO_ERROR;
#else
    for (;;) {
        long r = sys6(SYS_recvfrom, s, buf, cap, L_MSG_DONTWAIT, 0, 0);
        if (r > 0) return (int)r;
        if (r == 0) return AQ_IO_CLOSED;
        if (r == -L_EINTR) continue;
        return r == -L_EAGAIN ? AQ_IO_AGAIN : AQ_IO_ERROR;
    }
#endif
}
void aq_sock_close(aq_sock s) {
    if (s == AQ_BAD_SOCK) return;
#if defined(AQ_WINDOWS)
    closesocket((W_SOCKET)s);
#else
    sys1(SYS_close, s);
#endif
}
int aq_sock_local(aq_sock s, aq_addr *out) {
    sa_storage ss; aq_memset(&ss, 0, sizeof ss); int len = (int)sizeof ss.bytes;
#if defined(AQ_WINDOWS)
    if (getsockname((W_SOCKET)s, ss.bytes, &len) != 0) return -1;
#else
    if (sys3(SYS_getsockname, s, ss.bytes, &len) != 0) return -1;
#endif
    return from_sockaddr(&ss, out);
}

aq_sock aq_udp_open(const aq_addr *bind_addr, int *err) {
    sa_storage ss; int len = to_sockaddr(bind_addr, &ss); int zero = 0;
#if defined(AQ_WINDOWS)
    W_SOCKET s = socket(bind_addr->family == 4 ? W_AF_INET : W_AF_INET6, W_SOCK_DGRAM, W_IPPROTO_UDP);
    if (s == W_INVALID_SOCKET) { if (err) *err = WSAGetLastError(); return AQ_BAD_SOCK; }
    if (bind_addr->family == 6) setsockopt(s, W_IPPROTO_IPV6, W_IPV6_V6ONLY, (const char *)&zero, sizeof zero);
    if (bind(s, ss.bytes, len) != 0 || set_nonblocking(s) != 0) { if (err) *err = WSAGetLastError(); closesocket(s); return AQ_BAD_SOCK; }
    { /* Ignore ICMP port unreachable resets on UDP sockets. */
        int off = 0; unsigned long ret = 0;
        WSAIoctl(s, W_SIO_UDP_CONNRESET, &off, sizeof off, 0, 0, &ret, 0, 0);
    }
    return (aq_sock)s;
#else
    long s = sys3(SYS_socket, bind_addr->family == 4 ? L_AF_INET : L_AF_INET6, L_SOCK_DGRAM | L_SOCK_NONBLOCK | L_SOCK_CLOEXEC, 0);
    if (s < 0) { if (err) *err = (int)-s; return AQ_BAD_SOCK; }
    if (bind_addr->family == 6) sys5(SYS_setsockopt, s, L_IPPROTO_IPV6, L_IPV6_V6ONLY, &zero, sizeof zero);
    long r = sys3(SYS_bind, s, ss.bytes, len);
    if (r != 0) { if (err) *err = (int)-r; sys1(SYS_close, s); return AQ_BAD_SOCK; }
    return (aq_sock)s;
#endif
}
int aq_udp_send(aq_sock s, const void *buf, int len, const aq_addr *to) {
    sa_storage ss; aq_addr dst = *to;
    aq_addr local;
    /* Dual-stack IPv6 sockets need IPv4-mapped destinations. */
    if (to->family == 4 && aq_sock_local(s, &local) == 0 && local.family == 6) {
        aq_memset(dst.ip, 0, 16); dst.ip[10] = 0xff; dst.ip[11] = 0xff; aq_memcpy(dst.ip + 12, to->ip, 4); dst.family = 6;
    }
    int sl = to_sockaddr(&dst, &ss);
#if defined(AQ_WINDOWS)
    int r = sendto((W_SOCKET)s, (const char *)buf, len, 0, ss.bytes, sl);
    if (r >= 0) return r;
    return WSAGetLastError() == W_WSAEWOULDBLOCK ? AQ_IO_AGAIN : AQ_IO_ERROR;
#else
    long r = sys6(SYS_sendto, s, buf, len, L_MSG_NOSIGNAL | L_MSG_DONTWAIT, ss.bytes, sl);
    if (r >= 0) return (int)r;
    return r == -L_EAGAIN ? AQ_IO_AGAIN : AQ_IO_ERROR;
#endif
}
int aq_udp_recv(aq_sock s, void *buf, int cap, aq_addr *from) {
    sa_storage ss; aq_memset(&ss, 0, sizeof ss); int sl = (int)sizeof ss.bytes;
#if defined(AQ_WINDOWS)
    for (;;) {
        int r = recvfrom((W_SOCKET)s, (char *)buf, cap, 0, ss.bytes, &sl);
        if (r >= 0) { if (from) from_sockaddr(&ss, from); return r; }
        int e = WSAGetLastError();
        if (e == W_WSAECONNRESET || e == W_WSAEMSGSIZE) { sl = (int)sizeof ss.bytes; continue; }
        return e == W_WSAEWOULDBLOCK ? AQ_IO_AGAIN : AQ_IO_ERROR;
    }
#else
    for (;;) {
        long r = sys6(SYS_recvfrom, s, buf, cap, L_MSG_DONTWAIT, ss.bytes, &sl);
        if (r >= 0) { if (from) from_sockaddr(&ss, from); return (int)r; }
        if (r == -L_EINTR) continue;
        if (r == -L_ECONNREFUSED) { sl = (int)sizeof ss.bytes; continue; }
        return r == -L_EAGAIN ? AQ_IO_AGAIN : AQ_IO_ERROR;
    }
#endif
}

int aq_primary_ipv4(aq_addr *out) {
    aq_addr any, probe;
    aq_addr_parse("0.0.0.0", 0, &any);
    aq_addr_parse("192.0.2.1", 9, &probe);   /* TEST-NET-1, no packet is sent */
    aq_sock s = aq_udp_open(&any, 0);
    if (s == AQ_BAD_SOCK) return -1;
    sa_storage ss; int len = to_sockaddr(&probe, &ss); int rc = -1;
#if defined(AQ_WINDOWS)
    if (connect((W_SOCKET)s, ss.bytes, len) == 0 && aq_sock_local(s, out) == 0) rc = 0;
#else
    if (sys3(SYS_connect, s, ss.bytes, len) == 0 && aq_sock_local(s, out) == 0) rc = 0;
#endif
    aq_sock_close(s);
    if (rc == 0 && aq_addr_is_any(out)) rc = -1;
    if (rc == 0) out->port = 0;
    return rc;
}

static void add_unique(aq_addr *out, int *count, int max, const aq_addr *a) {
    if (a->family != 4 || aq_addr_is_any(a)) return;
    if (a->ip[0] == 169 && a->ip[1] == 254) return;   /* link local */
    for (int i = 0; i < *count; i++) if (aq_memcmp(out[i].ip, a->ip, 4) == 0) return;
    if (*count < max) { out[*count] = *a; out[*count].port = 0; (*count)++; }
}
int aq_local_ipv4s(aq_addr *out, int max) {
    int count = 0;
    aq_addr primary;
    if (aq_primary_ipv4(&primary) == 0) add_unique(out, &count, max, &primary);
#if defined(AQ_WINDOWS)
    char name[256];
    if (gethostname(name, sizeof name) == 0) {
        W_ADDRINFO hints, *res = 0;
        aq_memset(&hints, 0, sizeof hints); hints.ai_family = W_AF_INET; hints.ai_socktype = W_SOCK_DGRAM;
        if (getaddrinfo(name, 0, &hints, &res) == 0) {
            for (W_ADDRINFO *p = res; p; p = p->ai_next) {
                sa_storage ss; aq_memset(&ss, 0, sizeof ss);
                aq_memcpy(ss.bytes, p->ai_addr, p->ai_addrlen < sizeof ss ? p->ai_addrlen : sizeof ss);
                aq_addr a; if (from_sockaddr(&ss, &a) == 0 && !aq_addr_is_loopback(&a)) add_unique(out, &count, max, &a);
            }
            freeaddrinfo(res);
        }
    }
#else
    long s = sys3(SYS_socket, L_AF_INET, L_SOCK_DGRAM | L_SOCK_CLOEXEC, 0);
    if (s >= 0) {
        uint8_t reqs[40 * 16];
        struct { int len; int pad; void *buf; } conf = { (int)sizeof reqs, 0, reqs };
        if (sys3(SYS_ioctl, s, L_SIOCGIFCONF, &conf) == 0) {
            for (int off = 0; off + 40 <= conf.len; off += 40) {
                sa_storage ss; aq_memset(&ss, 0, sizeof ss); aq_memcpy(ss.bytes, reqs + off + 16, 16);
                aq_addr a; if (from_sockaddr(&ss, &a) == 0 && !aq_addr_is_loopback(&a)) add_unique(out, &count, max, &a);
            }
        }
        sys1(SYS_close, s);
    }
#endif
    return count;
}

int aq_poll(aq_pollfd *fds, int n, int timeout_ms) {
    if (n <= 0) {
        if (timeout_ms > 0) {
#if defined(AQ_WINDOWS)
            /* WSAPoll rejects empty sets; sleep through a dummy wait instead. */
            Sleep((unsigned long)timeout_ms);
#else
            struct l_timespec ts = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L };
            sys5(SYS_ppoll, 0, 0, &ts, 0, 8);
#endif
        }
        return 0;
    }
#if defined(AQ_WINDOWS)
    W_WSAPOLLFD stackfds[64]; W_WSAPOLLFD *w = n <= 64 ? stackfds : (W_WSAPOLLFD *)aq_malloc(sizeof(W_WSAPOLLFD) * (size_t)n);
    if (!w) return -1;
    for (int i = 0; i < n; i++) {
        w[i].fd = (W_SOCKET)fds[i].fd; w[i].revents = 0;
        w[i].events = (short)(((fds[i].events & AQ_POLL_IN) ? W_POLLIN : 0) | ((fds[i].events & AQ_POLL_OUT) ? W_POLLOUT : 0));
    }
    int r = WSAPoll(w, (unsigned long)n, timeout_ms);
    for (int i = 0; i < n; i++) {
        short re = w[i].revents, o = 0;
        if (re & W_POLLIN) o |= AQ_POLL_IN;
        if (re & W_POLLOUT) o |= AQ_POLL_OUT;
        if (re & (W_POLLERR | W_POLLNVAL)) o |= AQ_POLL_ERR;
        if (re & W_POLLHUP) o |= AQ_POLL_HUP | AQ_POLL_IN;
        fds[i].revents = o;
    }
    if (w != stackfds) aq_free(w);
    return r < 0 ? -1 : r;
#else
    struct l_pollfd stackfds[64]; struct l_pollfd *l = n <= 64 ? stackfds : (struct l_pollfd *)aq_malloc(sizeof(struct l_pollfd) * (size_t)n);
    if (!l) return -1;
    for (int i = 0; i < n; i++) {
        l[i].fd = (int)fds[i].fd; l[i].revents = 0;
        l[i].events = (short)(((fds[i].events & AQ_POLL_IN) ? L_POLLIN : 0) | ((fds[i].events & AQ_POLL_OUT) ? L_POLLOUT : 0));
    }
    struct l_timespec ts = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L };
    long r;
    do { r = sys5(SYS_ppoll, l, n, timeout_ms < 0 ? 0 : &ts, 0, 8); } while (r == -L_EINTR);
    for (int i = 0; i < n; i++) {
        short re = l[i].revents, o = 0;
        if (re & L_POLLIN) o |= AQ_POLL_IN;
        if (re & L_POLLOUT) o |= AQ_POLL_OUT;
        if (re & (L_POLLERR | L_POLLNVAL)) o |= AQ_POLL_ERR;
        if (re & L_POLLHUP) o |= AQ_POLL_HUP;
        fds[i].revents = o;
    }
    if (l != stackfds) aq_free(l);
    return r < 0 ? -1 : (int)r;
#endif
}

/* =====================================================================
 *  Buffers, Base64, SHA-1, CRC
 * ===================================================================== */
void aq_buf_reserve(aq_buf *b, size_t extra) {
    if (b->failed) return;
    if (b->len + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) cap *= 2;
    uint8_t *d = (uint8_t *)aq_realloc(b->data, cap);
    if (!d) { b->failed = 1; return; }
    b->data = d; b->cap = cap;
}
void aq_buf_append(aq_buf *b, const void *data, size_t n) {
    if (!n) return;
    aq_buf_reserve(b, n);
    if (b->failed) return;
    aq_memcpy(b->data + b->len, data, n); b->len += n;
}
void aq_buf_putc(aq_buf *b, char c) { aq_buf_append(b, &c, 1); }
void aq_buf_puts(aq_buf *b, const char *s) { aq_buf_append(b, s, aq_strlen(s)); }
void aq_buf_printf(aq_buf *b, const char *fmt, ...) {
    char tmp[512]; va_list ap; va_start(ap, fmt);
    int n = aq_vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < (int)sizeof tmp) { aq_buf_append(b, tmp, (size_t)n); return; }
    aq_buf_reserve(b, (size_t)n + 1);
    if (b->failed) return;
    va_start(ap, fmt);
    aq_vsnprintf((char *)b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}
void aq_buf_consume(aq_buf *b, size_t n) {
    if (n >= b->len) { b->len = 0; return; }
    aq_memmove(b->data, b->data + n, b->len - n); b->len -= n;
}
void aq_buf_free(aq_buf *b) { aq_free(b->data); b->data = 0; b->len = b->cap = 0; b->failed = 0; }

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t aq_base64_encode(const uint8_t *in, size_t n, char *out) {
    size_t o = 0, i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64[v >> 18]; out[o++] = B64[(v >> 12) & 63]; out[o++] = B64[(v >> 6) & 63]; out[o++] = B64[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)in[i] << 16; if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        out[o++] = B64[v >> 18]; out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? B64[(v >> 6) & 63] : '='; out[o++] = '=';
    }
    out[o] = 0;
    return o;
}
static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
int aq_base64_decode(const char *in, size_t n, uint8_t *out, size_t cap) {
    uint32_t acc = 0; int bits = 0; size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        int c = (unsigned char)in[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        int v = b64val(c); if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (o >= cap) return -1; out[o++] = (uint8_t)(acc >> bits); }
    }
    return (int)o;
}

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void sha1_block(aq_sha1 *c, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = aq_rd32(p + i * 4);
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
void aq_sha1_init(aq_sha1 *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->used = 0;
}
void aq_sha1_update(aq_sha1 *c, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    c->len += n;
    while (n) {
        size_t take = 64 - c->used; if (take > n) take = n;
        aq_memcpy(c->block + c->used, p, take); c->used += take; p += take; n -= take;
        if (c->used == 64) { sha1_block(c, c->block); c->used = 0; }
    }
}
void aq_sha1_final(aq_sha1 *c, uint8_t out[20]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80; aq_sha1_update(c, &pad, 1);
    uint8_t zero = 0; while (c->used != 56) aq_sha1_update(c, &zero, 1);
    uint8_t lenb[8]; for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    aq_sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++) aq_wr32(out + i * 4, c->h[i]);
}
void aq_hmac_sha1(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[20]) {
    uint8_t k[64], tmp[20]; aq_sha1 c;
    aq_memset(k, 0, 64);
    if (klen > 64) { aq_sha1_init(&c); aq_sha1_update(&c, key, klen); aq_sha1_final(&c, k); }
    else aq_memcpy(k, key, klen);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    aq_sha1_init(&c); aq_sha1_update(&c, ipad, 64); aq_sha1_update(&c, msg, mlen); aq_sha1_final(&c, tmp);
    aq_sha1_init(&c); aq_sha1_update(&c, opad, 64); aq_sha1_update(&c, tmp, 20); aq_sha1_final(&c, out);
}

static uint32_t crc_table[256], crcc_table[256];
static int crc_ready;
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i, d = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            d = (d & 1) ? 0x82F63B78u ^ (d >> 1) : d >> 1;
        }
        crc_table[i] = c; crcc_table[i] = d;
    }
    crc_ready = 1;
}
uint32_t aq_crc32(const uint8_t *data, size_t n) {
    if (!crc_ready) crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
uint32_t aq_crc32c(const uint8_t *data, size_t n) {
    if (!crc_ready) crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crcc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
