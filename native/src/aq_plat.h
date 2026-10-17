/*
 * aq_plat.h - Freestanding platform layer shared by Aquarius native libraries.
 *
 * The same sources build three ways:
 *   - Linux x86_64 / Android arm64-v8a / Android x86_64: no libc at all; Linux
 *     system calls are issued directly, so one binary works on glibc, musl and
 *     Bionic without NDK or sysroot dependencies.
 *   - Windows x64: Winsock, kernel32 and bcrypt imports only (no C runtime).
 *   - Optionally with a hosted toolchain (define AQ_HOSTED_LIBC) when you want
 *     to link the system C library instead of the freestanding shims.
 *
 * Everything is single threaded by design: a library call takes the global
 * lock, performs non-blocking I/O and returns. Hosts call a poll function from
 * their own loop, so no background threads exist inside the library.
 *
 * MIT License, Copyright (c) 2026 Temple Lin.
 */
#ifndef AQ_PLAT_H
#define AQ_PLAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#if defined(_WIN32)
#  define AQ_WINDOWS 1
#  define AQ_EXPORT __declspec(dllexport)
#else
#  define AQ_LINUX 1
#  define AQ_EXPORT __attribute__((visibility("default")))
#endif

#if defined(__ANDROID__)
#  define AQ_PLATFORM_NAME "android"
#elif defined(AQ_WINDOWS)
#  define AQ_PLATFORM_NAME "windows"
#else
#  define AQ_PLATFORM_NAME "linux"
#endif

typedef intptr_t aq_sock;
#define AQ_BAD_SOCK ((aq_sock)-1)

/* Network address (IPv4 or IPv6), host byte order port. */
typedef struct aq_addr {
    uint8_t family;      /* 4 or 6, 0 = unset */
    uint16_t port;
    uint8_t ip[16];      /* IPv4 uses the first four bytes */
} aq_addr;

/* Result codes for non-blocking I/O helpers. */
#define AQ_IO_AGAIN  (-1000)   /* would block, try later */
#define AQ_IO_CLOSED (-1001)   /* orderly shutdown by peer */
#define AQ_IO_ERROR  (-1002)   /* hard failure */

/* poll() style flags shared by every platform (translated internally). */
#define AQ_POLL_IN   1
#define AQ_POLL_OUT  4
#define AQ_POLL_ERR  8
#define AQ_POLL_HUP  16

typedef struct aq_pollfd { aq_sock fd; short events; short revents; } aq_pollfd;

/* ---- lifecycle / locking ---- */
int  aq_plat_init(void);
void aq_lock(void);
void aq_unlock(void);

/* ---- memory ---- */
void *aq_malloc(size_t n);
void *aq_calloc(size_t count, size_t size);
void *aq_realloc(void *p, size_t n);
void  aq_free(void *p);
char *aq_strdup(const char *s);
char *aq_strndup(const char *s, size_t n);

/* ---- strings (always available, independent of libc) ---- */
size_t aq_strlen(const char *s);
int    aq_strcmp(const char *a, const char *b);
int    aq_strncmp(const char *a, const char *b, size_t n);
int    aq_strcasecmp(const char *a, const char *b);
int    aq_strncasecmp(const char *a, const char *b, size_t n);
const char *aq_strstr(const char *h, const char *n);
const char *aq_strcasestr(const char *h, const char *n);
void  *aq_memcpy(void *d, const void *s, size_t n);
void  *aq_memmove(void *d, const void *s, size_t n);
void  *aq_memset(void *d, int c, size_t n);
int    aq_memcmp(const void *a, const void *b, size_t n);
int    aq_vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int    aq_snprintf(char *buf, size_t cap, const char *fmt, ...);
long long aq_atoll(const char *s, const char **end);

/* ---- time / randomness ---- */
uint64_t aq_now_ms(void);           /* monotonic milliseconds */
uint64_t aq_now_us(void);           /* monotonic microseconds */
uint64_t aq_wall_seconds(void);     /* seconds since the Unix epoch (best effort) */
int      aq_random(void *buf, size_t n);  /* 0 on success */
uint32_t aq_random_u32(void);

/* ---- addresses ---- */
int  aq_addr_parse(const char *text, uint16_t port, aq_addr *out);   /* numeric IPv4/IPv6 or "localhost" */
int  aq_addr_format(const aq_addr *a, char *buf, size_t cap, int with_port);
int  aq_addr_equal(const aq_addr *a, const aq_addr *b);
int  aq_addr_is_loopback(const aq_addr *a);
int  aq_addr_is_any(const aq_addr *a);
/* Resolve a host name. Linux/Android builds only accept numeric addresses and
 * "localhost"; callers resolve names through the Aquarius Network library.
 * Windows additionally uses getaddrinfo. */
int  aq_resolve(const char *host, uint16_t port, aq_addr *out);

/* ---- TCP ---- */
aq_sock aq_tcp_listen(const aq_addr *bind_addr, int backlog, int *err);
aq_sock aq_tcp_accept(aq_sock listener, aq_addr *peer);   /* AQ_BAD_SOCK when none */
aq_sock aq_tcp_connect(const aq_addr *to, int *err);       /* non-blocking connect started */
int     aq_tcp_connect_result(aq_sock s);                   /* 0 ok, >0 pending, <0 failed */
int     aq_sock_send(aq_sock s, const void *buf, int len);  /* bytes, or AQ_IO_* */
int     aq_sock_recv(aq_sock s, void *buf, int cap);        /* bytes, or AQ_IO_* */
void    aq_sock_close(aq_sock s);
int     aq_sock_local(aq_sock s, aq_addr *out);

/* ---- UDP ---- */
aq_sock aq_udp_open(const aq_addr *bind_addr, int *err);
int     aq_udp_send(aq_sock s, const void *buf, int len, const aq_addr *to);
int     aq_udp_recv(aq_sock s, void *buf, int cap, aq_addr *from);  /* bytes or AQ_IO_* */
/* Best effort: primary outbound IPv4 address without sending a packet. */
int     aq_primary_ipv4(aq_addr *out);
/* Enumerate local IPv4 interface addresses (non-loopback first). Returns count. */
int     aq_local_ipv4s(aq_addr *out, int max);

/* ---- readiness ---- */
int aq_poll(aq_pollfd *fds, int n, int timeout_ms);

/* ---- small helpers ---- */
static inline uint16_t aq_rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t aq_rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline void aq_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void aq_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* Growable byte buffer. */
typedef struct aq_buf { uint8_t *data; size_t len, cap; int failed; } aq_buf;
void aq_buf_reserve(aq_buf *b, size_t extra);
void aq_buf_append(aq_buf *b, const void *data, size_t n);
void aq_buf_putc(aq_buf *b, char c);
void aq_buf_puts(aq_buf *b, const char *s);
void aq_buf_printf(aq_buf *b, const char *fmt, ...);
void aq_buf_consume(aq_buf *b, size_t n);
void aq_buf_free(aq_buf *b);

/* Base64 (standard alphabet, padded). */
size_t aq_base64_encode(const uint8_t *in, size_t n, char *out);      /* out needs 4*((n+2)/3)+1 */
int    aq_base64_decode(const char *in, size_t n, uint8_t *out, size_t cap); /* bytes or -1 */

/* SHA-1 (used by the WebSocket handshake and STUN HMAC). */
typedef struct aq_sha1 { uint32_t h[5]; uint64_t len; uint8_t block[64]; size_t used; } aq_sha1;
void aq_sha1_init(aq_sha1 *c);
void aq_sha1_update(aq_sha1 *c, const void *data, size_t n);
void aq_sha1_final(aq_sha1 *c, uint8_t out[20]);
void aq_hmac_sha1(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[20]);

uint32_t aq_crc32(const uint8_t *data, size_t n);    /* ISO-HDLC (STUN fingerprint) */
uint32_t aq_crc32c(const uint8_t *data, size_t n);   /* Castagnoli (SCTP) */

#endif
