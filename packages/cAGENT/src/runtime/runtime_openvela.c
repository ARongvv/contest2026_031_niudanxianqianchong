/* SPDX-License-Identifier: Apache-2.0 */
/**
 * openvela/NuttX runtime adapter 实现。
 *
 * 当构建启用 CAGENT_RUNTIME_OPENVELA 时编译。
 * 所有 openvela/NuttX 特定的 include 隔离在此文件中。
 *
 * 可移植核心在未编译此文件时仍可正常工作（使用 POSIX fallback）。
 *
 * HTTPS 实现参考 ai_agent/src/infra/vela_tls.c，精简了连接池和代理逻辑，
 * 保留 mbedTLS 核心流程和 NuttX 平台适配（SO_RCVTIMEO、ALPN、时钟校准）。
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "runtime_openvela.h"
#include "../types_internal.h"

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include <stdarg.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>
#include <syslog.h>

#include <sys/time.h>

#if defined(CONFIG_ARCH_CHIP_ESP32S3)
#include <arch/esp32s3/chip.h>
#endif

void ov_mem_region_log(const char *label, const void *pointer)
{
    const char *region;

    if (!pointer) {
        region = "null";
    }
#if defined(CONFIG_ARCH_CHIP_ESP32S3) && \
    defined(CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE) && \
    CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE > 0
    else if (esp32s3_ptr_is_extram(pointer)) {
        region = "PSRAM";
    } else {
        region = "SRAM";
    }
#else
    else {
        region = "unknown";
    }
#endif

    printf("[cagent_mem] %s ptr=%p region=%s\n",
           label ? label : "pointer", pointer, region);
}

void *ov_mem_bulk_alloc(size_t size)
{
#if defined(CONFIG_ARCH_CHIP_ESP32S3) && \
    defined(CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE) && \
    CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE > 0
    return esp32s3_psram_bulk_alloc(size);
#else
    return malloc(size);
#endif
}

void ov_mem_bulk_free(void *pointer)
{
#if defined(CONFIG_ARCH_CHIP_ESP32S3) && \
    defined(CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE) && \
    CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE > 0
    esp32s3_psram_bulk_free(pointer);
#else
    free(pointer);
#endif
}

void ov_mem_bulk_diag(const char *point)
{
#if defined(CONFIG_ARCH_CHIP_ESP32S3) && \
    defined(CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE) && \
    CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE > 0
    struct esp32s3_psram_bulk_info_s info;

    if (esp32s3_psram_bulk_info(&info) == 0) {
        printf("[cagent_mem] %s psram_bulk total=%zu free=%zu largest=%zu\n",
               point ? point : "bulk", info.total_bytes, info.free_bytes,
               info.largest_free_bytes);
    }
#else
    (void)point;
#endif
}

#ifdef CAGENT_RUNTIME_OPENVELA_TLS

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

#ifndef CAGENT_OV_TLS_READ_BUF_SIZE
#define CAGENT_OV_TLS_READ_BUF_SIZE 8192u
#endif

#ifndef CAGENT_OV_TLS_HDR_BUF_SIZE
#define CAGENT_OV_TLS_HDR_BUF_SIZE 4096u
#endif

#ifndef CAGENT_OV_SOCKET_TIMEOUT_SEC
#define CAGENT_OV_SOCKET_TIMEOUT_SEC 60
#endif

#ifndef CAGENT_OV_MAX_HEADERS
#define CAGENT_OV_MAX_HEADERS 16u
#endif

static const char *OV_TAG = "cagent_ov";

static void ov_mem_diag(const char *point)
{
    struct mallinfo info = mallinfo();

    printf("[cagent_mem] %s fordblks=%u mxordblk=%u uordblks=%u\n",
           point, info.fordblks, info.mxordblk, info.uordblks);
}

static void ov_tls_diag(const char *fmt, ...)
{
    va_list ap;

    printf("[%s] ", OV_TAG);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void ov_tls_diag_mbed(const char *step, int ret)
{
#if defined(MBEDTLS_ERROR_C)
    char err_buf[128];

    mbedtls_strerror(ret, err_buf, sizeof(err_buf));
    ov_tls_diag("%s ret=-0x%04x: %s", step, -ret, err_buf);
#else
    ov_tls_diag("%s ret=-0x%04x", step, -ret);
#endif
}

/* ── Chunked transfer decoding ──────────────────────────────── */

static size_t ov_decode_chunked(char *buf, size_t len)
{
    char *src = buf;
    char *end = buf + len;
    char *dst = buf;

    while (src < end) {
        char *crlf = (char *)memmem(src, (size_t)(end - src), "\r\n", 2);
        if (!crlf) {
            break;
        }

        char *endptr;
        long chunk_sz = strtol(src, &endptr, 16);

        while (endptr < crlf && *endptr == ' ') {
            endptr++;
        }

        if (endptr != crlf || chunk_sz < 0 || chunk_sz > (long)(end - crlf - 2)) {
            break;
        }

        if (chunk_sz == 0) {
            break;
        }

        src = crlf + 2;

        if (src + chunk_sz > end) {
            chunk_sz = (long)(end - src);
        }

        memmove(dst, src, (size_t)chunk_sz);
        dst += chunk_sz;
        src += chunk_sz;

        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') {
            src += 2;
        }
    }

    return (size_t)(dst - buf);
}

/* ── TLS context ────────────────────────────────────────────── */
/* ov_tls_ctx_t 不透明句柄声明在 runtime_openvela.h；完整定义在此。
 * ov_tls_connect / ov_tls_read / ov_tls_write / ov_tls_ctx_free
 * 供同固件应用层复用 TLS 连接。 */

struct ov_tls_ctx {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config cfg;
    mbedtls_net_context net;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
};

/* 等待 TLS 底层 fd 可读/可写（poll，带超时）。返回 0 就绪，-1 超时/错误。 */
static int ov_tls_wait_fd(const ov_tls_ctx_t *ctx, short events,
                          uint32_t timeout_ms, const char *phase)
{
    struct pollfd descriptor;
    int result;

    if (!ctx || ctx->net.fd < 0) {
        ov_tls_diag("phase=%s wait invalid fd=%d events=0x%x",
                    phase ? phase : "tls", ctx ? ctx->net.fd : -1,
                    (unsigned int)events);
        return -1;
    }
    descriptor.fd = ctx->net.fd;
    descriptor.events = events;
    descriptor.revents = 0;
    result = poll(&descriptor, 1, (int)timeout_ms);
    if (result == 0) {
        ov_tls_diag("phase=%s wait timeout fd=%d events=0x%x timeout_ms=%u",
                    phase ? phase : "tls", descriptor.fd,
                    (unsigned int)events, timeout_ms);
        return -1;
    }
    if (result < 0) {
        ov_tls_diag("phase=%s wait poll_failed fd=%d events=0x%x errno=%d",
                    phase ? phase : "tls", descriptor.fd,
                    (unsigned int)events, errno);
        return -1;
    }
    ov_tls_diag("phase=%s wait ready fd=%d events=0x%x revents=0x%x",
                phase ? phase : "tls", descriptor.fd,
                (unsigned int)events, (unsigned int)descriptor.revents);
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        ov_tls_diag("phase=%s wait socket_error fd=%d revents=0x%x",
                    phase ? phase : "tls", descriptor.fd,
                    (unsigned int)descriptor.revents);
        return -1;
    }
    return 0;
}

static uint64_t ov_tls_monotonic_ms(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static uint32_t ov_tls_remaining_ms(uint64_t deadline_ms)
{
    uint64_t now = ov_tls_monotonic_ms();
    uint64_t remaining;

    if (!now || !deadline_ms) {
        return 0u;
    }
    if (now >= deadline_ms) {
        return 0u;
    }
    remaining = deadline_ms - now;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

static int ov_tls_wait_io(ov_tls_ctx_t *ctx, int tls_ret,
                          uint64_t deadline_ms,
                          uint32_t fallback_timeout_ms,
                          const char *phase)
{
    short events;
    uint32_t remaining_ms;

    if (tls_ret == MBEDTLS_ERR_SSL_WANT_READ) {
        events = POLLIN;
    } else if (tls_ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        events = POLLOUT;
    } else {
        return AGENT_ERROR_NETWORK;
    }

    remaining_ms = deadline_ms ? ov_tls_remaining_ms(deadline_ms) :
                   fallback_timeout_ms;
    if (remaining_ms == 0u ||
        ov_tls_wait_fd(ctx, events, remaining_ms, phase) != 0) {
        return AGENT_ERROR_TIMEOUT;
    }

    return AGENT_OK;
}

ov_tls_ctx_t *ov_tls_create(void)
{
    ov_tls_ctx_t *ctx;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    ov_mem_region_log("tls-context", ctx);
    ctx->net.fd = -1;
    return ctx;
}

ssize_t ov_tls_read(ov_tls_ctx_t *ctx, void *buffer, size_t length,
                    uint32_t timeout_ms)
{
    if (!ctx || !buffer) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int count = mbedtls_ssl_read(&ctx->ssl, buffer, length);

        if (count == MBEDTLS_ERR_SSL_WANT_READ) {
            if (timeout_ms == 0u) {
                continue;
            }
            if (ov_tls_wait_fd(ctx, POLLIN, timeout_ms, "tls_read") != 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            continue;
        }
        if (count == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (ov_tls_wait_fd(ctx, POLLOUT, timeout_ms, "tls_read") != 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            continue;
        }
        if (count < 0) {
            errno = EIO;
            return -1;
        }
        return (ssize_t)count;
    }
}

ssize_t ov_tls_write(ov_tls_ctx_t *ctx, const void *buffer, size_t length,
                     uint32_t timeout_ms)
{
    if (!ctx || !buffer) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int count = mbedtls_ssl_write(&ctx->ssl, buffer, length);

        if (count == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (timeout_ms == 0u) {
                continue;
            }
            if (ov_tls_wait_fd(ctx, POLLOUT, timeout_ms, "tls_write") != 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            continue;
        }
        if (count == MBEDTLS_ERR_SSL_WANT_READ) {
            if (ov_tls_wait_fd(ctx, POLLIN, timeout_ms, "tls_write") != 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            continue;
        }
        if (count < 0) {
            errno = EIO;
            return -1;
        }
        return (ssize_t)count;
    }
}

void ov_tls_ctx_free(ov_tls_ctx_t *ctx)
{
    int fd;

    if (!ctx) {
        return;
    }

    fd = ctx->net.fd;
    if (fd >= 0) {
        ov_tls_diag("phase=tcp close_start fd=%d", fd);
    }
    mbedtls_ssl_close_notify(&ctx->ssl);
    mbedtls_net_free(&ctx->net);
    if (fd >= 0) {
        ov_tls_diag("phase=tcp close_done fd=%d", fd);
    }
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->cfg);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
}

int ov_tls_get_fd(const ov_tls_ctx_t *ctx)
{
    if (!ctx) {
        return -1;
    }
    return ctx->net.fd;
}

int ov_tls_connect(ov_tls_ctx_t *ctx,
                   const char *host,
                   const char *port,
                   uint32_t timeout_ms)
{
    int ret;
    const char *pers = "cagent_ov";
    uint32_t effective_timeout_ms;
    uint64_t deadline_ms;

    ov_mem_region_log("tls-context", ctx);
    ov_mem_diag("tls-connect-before");
    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->cfg);
    mbedtls_net_init(&ctx->net);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_entropy_init(&ctx->entropy);

    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                 &ctx->entropy,
                                 (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        syslog(LOG_ERR, "[%s] ctr_drbg_seed ret=-0x%04x\n", OV_TAG, -ret);
        ov_tls_diag_mbed("ctr_drbg_seed", ret);
        return AGENT_ERROR_NETWORK;
    }

    effective_timeout_ms = timeout_ms ? timeout_ms :
                           CAGENT_OV_SOCKET_TIMEOUT_SEC * 1000u;
    deadline_ms = ov_tls_monotonic_ms();
    if (deadline_ms) {
        deadline_ms += effective_timeout_ms;
    }

    ov_tls_diag("connecting %s:%s (timeout %u ms)",
                host, port, effective_timeout_ms);
    fflush(stdout);

    /* Use non-blocking connect with select() to enforce a timeout.
     * mbedtls_net_connect() blocks indefinitely on NuttX when the
     * remote port is filtered (no SYN-ACK, no RST).
     */

    {
        struct timeval tv_conn = {
            .tv_sec = (time_t)(effective_timeout_ms / 1000u),
            .tv_usec = (suseconds_t)(effective_timeout_ms % 1000u) * 1000,
        };
        struct addrinfo hints = { .ai_family = AF_INET,
                                  .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        int fd;

        ov_tls_diag("phase=dns resolving host=%s port=%s", host, port);
        fflush(stdout);

        {
            int dns_rc = getaddrinfo(host, port, &hints, &res);

            if (dns_rc != 0 || !res) {
                ov_tls_diag("phase=dns failed host=%s port=%s rc=%d errno=%d",
                            host, port, dns_rc, errno);
                return AGENT_ERROR_NETWORK;
            }
            ov_tls_diag("phase=dns resolved host=%s family=%d socktype=%d",
                        host, res->ai_family, res->ai_socktype);
        }

        if (res->ai_family != AF_INET || !res->ai_addr) {
            ov_tls_diag("phase=dns unsupported result host=%s family=%d",
                        host, res->ai_family);
            freeaddrinfo(res);
            return AGENT_ERROR_NETWORK;
        }

        {
            char peer_text[INET_ADDRSTRLEN];
            const struct sockaddr_in *peer =
                (const struct sockaddr_in *)res->ai_addr;

            if (inet_ntop(AF_INET, &peer->sin_addr, peer_text,
                          sizeof(peer_text))) {
                ov_tls_diag("phase=tcp peer=%s port=%s", peer_text, port);
            }
        }

        ov_tls_diag("phase=tcp creating socket ...");
        fflush(stdout);

        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(res);
            ov_tls_diag("socket() failed errno=%d", errno);
            return AGENT_ERROR_NETWORK;
        }

        ov_tls_diag("phase=tcp connect_start fd=%d nonblocking=1", fd);
        fflush(stdout);

        /* Non-blocking connect */

        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        ret = connect(fd, res->ai_addr, res->ai_addrlen);
        if (ret < 0 && errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);

            ret = select(fd + 1, NULL, &wfds, NULL, &tv_conn);
            if (ret == 0) {
                ov_tls_diag("phase=tcp connect_timeout fd=%d timeout_ms=%u",
                            fd, effective_timeout_ms);
                close(fd);
                freeaddrinfo(res);
                return AGENT_ERROR_NETWORK;
            }
            if (ret < 0) {
                ov_tls_diag("phase=tcp select_failed fd=%d errno=%d",
                            fd, errno);
                close(fd);
                freeaddrinfo(res);
                return AGENT_ERROR_NETWORK;
            }

            /* Check if connect succeeded */

            int err = 0;
            socklen_t elen = sizeof(err);
            ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
            if (ret < 0) {
                ov_tls_diag("phase=tcp getsockopt_failed fd=%d errno=%d",
                            fd, errno);
                close(fd);
                freeaddrinfo(res);
                return AGENT_ERROR_NETWORK;
            }
            if (err != 0) {
                ov_tls_diag("phase=tcp connect_failed fd=%d so_error=%d",
                            fd, err);
                close(fd);
                freeaddrinfo(res);
                return AGENT_ERROR_NETWORK;
            }
        } else if (ret < 0) {
            ov_tls_diag("phase=tcp connect_failed_immediate fd=%d errno=%d",
                        fd, errno);
            close(fd);
            freeaddrinfo(res);
            return AGENT_ERROR_NETWORK;
        }

        freeaddrinfo(res);

        /* 保持非阻塞模式：mbedTLS 返回 WANT_READ/WANT_WRITE，
         * 由 ov_tls_read/ov_tls_write 的 poll 循环控制超时。
         * 不要恢复阻塞，否则阻塞读无法超时。 */

        ctx->net.fd = fd;
    }

    ov_tls_diag("phase=tcp connected host=%s port=%s fd=%d",
                host, port, ctx->net.fd);

    if ((ret = mbedtls_ssl_config_defaults(&ctx->cfg,
                                            MBEDTLS_SSL_IS_CLIENT,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        syslog(LOG_ERR, "[%s] ssl_config_defaults ret=-0x%04x\n", OV_TAG, -ret);
        ov_tls_diag_mbed("ssl_config_defaults", ret);
        return AGENT_ERROR_NETWORK;
    }

    mbedtls_ssl_conf_min_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_3);
#else
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#endif

#if 0 /* Disabled: some servers reject the connection when ALPN is present */
#if defined(MBEDTLS_SSL_ALPN)
    {
        static const char *alpn_protos[] = { "http/1.1", NULL };
        mbedtls_ssl_conf_alpn_protocols(&ctx->cfg, alpn_protos);
    }
#endif
#endif

    mbedtls_ssl_conf_authmode(&ctx->cfg, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&ctx->cfg, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->cfg)) != 0) {
        syslog(LOG_ERR, "[%s] ssl_setup ret=-0x%04x\n", OV_TAG, -ret);
        ov_tls_diag_mbed("ssl_setup", ret);
        return AGENT_ERROR_NETWORK;
    }
    ov_mem_diag("tls-setup-after");

    if ((ret = mbedtls_ssl_set_hostname(&ctx->ssl, host)) != 0) {
        syslog(LOG_ERR, "[%s] ssl_set_hostname ret=-0x%04x\n", OV_TAG, -ret);
        ov_tls_diag_mbed("ssl_set_hostname", ret);
        return AGENT_ERROR_NETWORK;
    }

    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    ov_mem_diag("tls-handshake-before");
    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        uint32_t remaining_ms;

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
#if defined(MBEDTLS_ERROR_C)
            char err_buf[128];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            syslog(LOG_ERR, "[%s] ssl_handshake ret=-0x%04x: %s\n",
                   OV_TAG, -ret, err_buf);
#else
            syslog(LOG_ERR, "[%s] ssl_handshake ret=-0x%04x\n", OV_TAG, -ret);
#endif
            ov_tls_diag_mbed("ssl_handshake", ret);
            return AGENT_ERROR_NETWORK;
        }
        ov_tls_diag("phase=tls handshake_wait fd=%d want=%s",
                    ctx->net.fd,
                    ret == MBEDTLS_ERR_SSL_WANT_READ ? "read" : "write");
        remaining_ms = deadline_ms ? ov_tls_remaining_ms(deadline_ms) :
                       effective_timeout_ms;
        if (remaining_ms == 0u
            || ov_tls_wait_fd(ctx,
                              ret == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT,
                              remaining_ms, "tls_handshake") != 0) {
            ov_tls_diag("phase=tls handshake_wait_failed fd=%d remaining_ms=%u",
                        ctx->net.fd, remaining_ms);
            return AGENT_ERROR_TIMEOUT;
        }
    }

    syslog(LOG_INFO, "[%s] TLS handshake OK: %s / %s\n",
           OV_TAG, mbedtls_ssl_get_version(&ctx->ssl),
           mbedtls_ssl_get_ciphersuite(&ctx->ssl));
    ov_tls_diag("phase=tls handshake_ok version=%s cipher=%s fd=%d",
                mbedtls_ssl_get_version(&ctx->ssl),
                mbedtls_ssl_get_ciphersuite(&ctx->ssl), ctx->net.fd);
    ov_mem_diag("tls-handshake-after");

    return AGENT_OK;
}

/* ── HTTP/1.1 request write ─────────────────────────────────── */

static int ov_tls_write_request(ov_tls_ctx_t *ctx,
                                 const char *method,
                                 const char *host,
                                 const char *path,
                                 const char *headers_str,
                                 const char *body,
                                 size_t body_len)
{
    char *hdr = malloc(CAGENT_OV_TLS_HDR_BUF_SIZE);
    if (!hdr) {
        return AGENT_ERROR_NOMEM;
    }
    ov_mem_region_log("tls-header-buffer", hdr);

    int pos = 0;
    int n;
    int ret;

#define HDR_APPEND(fmt, ...)                                            \
    n = snprintf(hdr + pos, CAGENT_OV_TLS_HDR_BUF_SIZE - pos,          \
                 fmt, ##__VA_ARGS__);                                   \
    if (n < 0 || pos + n >= (int)CAGENT_OV_TLS_HDR_BUF_SIZE) {        \
        free(hdr);                                                      \
        return AGENT_ERROR_LIMIT;                                       \
    }                                                                   \
    pos += n;

    HDR_APPEND("%s %s HTTP/1.1\r\n", method, path);
    HDR_APPEND("Host: %s\r\n", host);
    HDR_APPEND("Connection: close\r\n");
    HDR_APPEND("User-Agent: cagent-openvela/1.0\r\n");

    if (body && body_len > 0) {
        HDR_APPEND("Content-Length: %zu\r\n", body_len);
    }

    if (headers_str && headers_str[0] != '\0') {
        HDR_APPEND("%s", headers_str);
    }

    HDR_APPEND("\r\n");
#undef HDR_APPEND

    int written = 0;
    while (written < pos) {
        ret = mbedtls_ssl_write(&ctx->ssl,
                                (const unsigned char *)(hdr + written),
                                (size_t)(pos - written));
        if (ret > 0) {
            written += ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        } else {
            ov_tls_diag_mbed("ssl_write header", ret);
            free(hdr);
            return AGENT_ERROR_NETWORK;
        }
    }

    free(hdr);

    if (body && body_len > 0) {
        size_t bw = 0;
        while (bw < body_len) {
            ret = mbedtls_ssl_write(&ctx->ssl,
                                    (const unsigned char *)(body + bw),
                                    body_len - bw);
            if (ret > 0) {
                bw += (size_t)ret;
            } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            } else {
                ov_tls_diag_mbed("ssl_write body", ret);
                return AGENT_ERROR_NETWORK;
            }
        }
    }

    return AGENT_OK;
}

/* ── HTTP/1.1 response read ─────────────────────────────────── */

static int ov_tls_read_response(ov_tls_ctx_t *ctx,
                                 char *resp_buf,
                                 size_t resp_cap,
                                 size_t *out_body_len,
                                 uint32_t timeout_ms)
{
    char *raw = malloc(CAGENT_OV_TLS_READ_BUF_SIZE);
    uint32_t effective_timeout_ms;
    uint64_t deadline_ms;
    if (!raw) {
        return AGENT_ERROR_NOMEM;
    }
    ov_mem_region_log("tls-read-buffer", raw);

    effective_timeout_ms = timeout_ms ? timeout_ms :
                           CAGENT_OV_SOCKET_TIMEOUT_SEC * 1000u;
    deadline_ms = ov_tls_monotonic_ms();
    if (deadline_ms) {
        deadline_ms += effective_timeout_ms;
    }

    size_t raw_len = 0;
    int eof = 0;
    int ret;

    while (!eof && raw_len < CAGENT_OV_TLS_READ_BUF_SIZE - 1) {
        ret = mbedtls_ssl_read(&ctx->ssl,
                               (unsigned char *)(raw + raw_len),
                               CAGENT_OV_TLS_READ_BUF_SIZE - 1 - raw_len);
        if (ret > 0) {
            raw_len += (size_t)ret;
            raw[raw_len] = '\0';
            if (memmem(raw, raw_len, "\r\n\r\n", 4)) {
                break;
            }
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            eof = 1;
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                   ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            ret = ov_tls_wait_io(ctx, ret, deadline_ms,
                                 effective_timeout_ms,
                                 "tls_response_header");
            if (ret != AGENT_OK) {
                free(raw);
                return ret;
            }
        } else {
            syslog(LOG_ERR, "[%s] ssl_read (header) ret=-0x%04x\n", OV_TAG, -ret);
            ov_tls_diag_mbed("ssl_read header", ret);
            free(raw);
            return AGENT_ERROR_NETWORK;
        }
    }
    raw[raw_len] = '\0';

    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        syslog(LOG_ERR, "[%s] Failed to parse HTTP status\n", OV_TAG);
        ov_tls_diag("parse HTTP status failed raw_len=%zu", raw_len);
        free(raw);
        return AGENT_ERROR_PARSE;
    }

    ov_tls_diag("HTTP status %d header_bytes=%zu", http_status, raw_len);

    char *body_start = (char *)memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!body_start) {
        resp_buf[0] = '\0';
        if (out_body_len) {
            *out_body_len = 0;
        }
        free(raw);
        return http_status;
    }
    body_start += 4;

    long content_length = -1;
    {
        char *cl_hdr = strcasestr(raw, "Content-Length:");
        if (cl_hdr && cl_hdr < body_start) {
            cl_hdr += strlen("Content-Length:");
            content_length = strtol(cl_hdr, NULL, 10);
            if (content_length < 0 || content_length > 10 * 1024 * 1024) {
                content_length = -1;
            }
        }
    }

    int chunked = 0;
    {
        char *te_hdr = strcasestr(raw, "Transfer-Encoding:");
        if (te_hdr && te_hdr < body_start) {
            chunked = (strcasestr(te_hdr, "chunked") != NULL);
        }
    }

    size_t initial = (size_t)(raw + raw_len - body_start);
    size_t resp_pos = 0;

    size_t copy = initial < resp_cap - 1 ? initial : resp_cap - 1;
    memcpy(resp_buf, body_start, copy);
    resp_pos = copy;

    free(raw);

    if (!eof) {
        while (resp_pos < resp_cap - 1) {
            if (content_length >= 0 && (long)resp_pos >= content_length) {
                break;
            }
            ret = mbedtls_ssl_read(&ctx->ssl,
                                   (unsigned char *)(resp_buf + resp_pos),
                                   resp_cap - 1 - resp_pos);
            if (ret > 0) {
                resp_pos += (size_t)ret;
            } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                break;
            } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                       ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                ret = ov_tls_wait_io(ctx, ret, deadline_ms,
                                     effective_timeout_ms,
                                     "tls_response_body");
                if (ret != AGENT_OK) {
                    return ret;
                }
            } else {
                ov_tls_diag_mbed("ssl_read body", ret);
                return AGENT_ERROR_NETWORK;
            }
        }
    }

    resp_buf[resp_pos] = '\0';

    if (chunked) {
        resp_pos = ov_decode_chunked(resp_buf, resp_pos);
        resp_buf[resp_pos] = '\0';
    }

    if (out_body_len) {
        *out_body_len = resp_pos;
    }

    return http_status;
}

/* ── http_post callback ─────────────────────────────────────── */

static int ov_http_post(const agent_http_request_t *request,
                         agent_http_response_t *response,
                         void *user_data)
{
    ov_tls_ctx_t ctx;
    int ret;

    (void)user_data;

    if (!request || !response || !response->body || response->body_size == 0u ||
        !request->host || !request->path || !request->port) {
        return AGENT_ERROR_INVALID;
    }

    memset(&ctx, 0, sizeof(ctx));

    ov_tls_diag("http_post %s %s:%s%s body=%zu resp_cap=%zu timeout=%u",
                request->method ? request->method : "POST",
                request->host,
                request->port,
                request->path,
                request->body_size,
                response->body_size,
                request->timeout_ms);

    ret = ov_tls_connect(&ctx, request->host, request->port, request->timeout_ms);
    if (ret != AGENT_OK) {
        ov_tls_diag("phase=http connect_failed agent_error=%d "
                    "(see phase=dns/tcp/tls diagnostics)", ret);
        ov_tls_ctx_free(&ctx);
        return ret;
    }

    ret = ov_tls_write_request(&ctx,
                                request->method ? request->method : "POST",
                                request->host,
                                request->path,
                                request->headers,
                                (const char *)request->body,
                                request->body_size);
    if (ret != AGENT_OK) {
        ov_tls_diag("http_post write failed err=%d", ret);
        ov_tls_ctx_free(&ctx);
        return ret;
    }

    size_t body_len = 0;
    int http_status = ov_tls_read_response(&ctx,
                                            response->body,
                                            response->body_size,
                                            &body_len,
                                            request->timeout_ms);

    ov_tls_ctx_free(&ctx);

    if (http_status < 0) {
        ov_tls_diag("http_post read failed err=%d", http_status);
        return http_status;
    }

    response->status_code = http_status;
    response->bytes_written = body_len;
    ov_tls_diag("http_post done status=%d body_len=%zu", http_status, body_len);
    if (http_status < 200 || http_status >= 300) {
        ov_tls_diag("http_post error body: %.300s",
                    response->body ? response->body : "(null)");
    }

    return AGENT_OK;
}

#endif /* CAGENT_RUNTIME_OPENVELA_TLS */

/* ── Basic runtime callbacks ────────────────────────────────── */

static void *ov_malloc(size_t size, void *user_data)
{
    (void)user_data;
    return malloc(size);
}

static void ov_free(void *ptr, void *user_data)
{
    (void)user_data;
    free(ptr);
}

static uint64_t ov_now_ms(void *user_data)
{
    struct timespec ts;
    (void)user_data;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (uint64_t)time(NULL) * 1000u;
    }

    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void ov_sleep_ms(uint32_t ms, void *user_data)
{
    (void)user_data;
    usleep((useconds_t)ms * 1000u);
}

static void ov_log(int level, const char *tag, const char *message, void *user_data)
{
    (void)user_data;
    int priority;

    switch (level) {
    case 0: priority = LOG_ERR; break;
    case 1: priority = LOG_WARNING; break;
    case 2: priority = LOG_INFO; break;
    default: priority = LOG_DEBUG; break;
    }

    syslog(priority, "[%s] %s\n", tag ? tag : "cagent", message ? message : "");
}

/* ── Mutex callbacks ────────────────────────────────────────── */

static void *ov_mutex_create(void *user_data)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    (void)user_data;

    if (!mtx) {
        return NULL;
    }

    pthread_mutex_init(mtx, NULL);
    return (void *)mtx;
}

static void ov_mutex_destroy(void *mutex, void *user_data)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)mutex;
    (void)user_data;

    if (mtx) {
        pthread_mutex_destroy(mtx);
        free(mtx);
    }
}

static int ov_mutex_lock(void *mutex, uint32_t timeout_ms, void *user_data)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)mutex;
    (void)user_data;

    if (!mtx) {
        return AGENT_ERROR_INVALID;
    }

    if (timeout_ms == 0u) {
        return pthread_mutex_lock(mtx) == 0 ? AGENT_OK : AGENT_ERROR;
    }

#ifdef __linux__
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000u;
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_mutex_timedlock(mtx, &ts) == 0 ? AGENT_OK : AGENT_ERROR_TIMEOUT;
#else
    return pthread_mutex_lock(mtx) == 0 ? AGENT_OK : AGENT_ERROR;
#endif
}

static void ov_mutex_unlock(void *mutex, void *user_data)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)mutex;
    (void)user_data;

    if (mtx) {
        pthread_mutex_unlock(mtx);
    }
}

/* ── Critical section callbacks (pthread fallback) ──────────── */

static pthread_mutex_t ov_critical_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t ov_enter_critical(void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&ov_critical_mtx);
    return 0u;
}

static void ov_exit_critical(uint32_t state, void *user_data)
{
    (void)state;
    (void)user_data;
    pthread_mutex_unlock(&ov_critical_mtx);
}

/* ── Public: agent_runtime_openvela_fill ─────────────────────── */

void agent_runtime_openvela_fill(agent_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    if (!runtime->malloc_fn) {
        runtime->malloc_fn = ov_malloc;
    }
    if (!runtime->free_fn) {
        runtime->free_fn = ov_free;
    }
    if (!runtime->now_ms) {
        runtime->now_ms = ov_now_ms;
    }
    if (!runtime->sleep_ms) {
        runtime->sleep_ms = ov_sleep_ms;
    }
    if (!runtime->log) {
        runtime->log = ov_log;
    }

#ifdef CAGENT_RUNTIME_OPENVELA_TLS
    if (!runtime->http_post) {
        runtime->http_post = ov_http_post;
    }
#endif

    if (!runtime->mutex_create) {
        runtime->mutex_create = ov_mutex_create;
    }
    if (!runtime->mutex_destroy) {
        runtime->mutex_destroy = ov_mutex_destroy;
    }
    if (!runtime->mutex_lock) {
        runtime->mutex_lock = ov_mutex_lock;
    }
    if (!runtime->mutex_unlock) {
        runtime->mutex_unlock = ov_mutex_unlock;
    }
    if (!runtime->enter_critical) {
        runtime->enter_critical = ov_enter_critical;
    }
    if (!runtime->exit_critical) {
        runtime->exit_critical = ov_exit_critical;
    }
}
