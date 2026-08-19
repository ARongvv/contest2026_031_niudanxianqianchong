/* SPDX-License-Identifier: Apache-2.0 */
/**
 * STM32 runtime adapter implementation.
 *
 * Compiled when the build enables CAGENT_RUNTIME_STM32.
 * All STM32 / HAL / FreeRTOS specific includes are isolated in this file.
 *
 * Two build-time profiles control which callbacks are provided:
 *
 *   CAGENT_RUNTIME_STM32_FREERTOS defined:
 *     Multi-threaded FreeRTOS build (CubeMX default).
 *     Provides: mutex via SemaphoreHandle_t, critical section via
 *     taskENTER_CRITICAL, now_ms via xTaskGetTickCount.
 *
 *   CAGENT_RUNTIME_STM32_FREERTOS NOT defined:
 *     Bare-metal single-threaded build.
 *     Provides: mutex = no-op (AGENT_OK), critical section via
 *     __disable_irq/__enable_irq (CMSIS), now_ms via HAL_GetTick.
 *
 *   CAGENT_RUNTIME_STM32_TLS defined (requires LWIP + mbedTLS):
 *     Provides: http_post via mbedTLS over LWIP sockets.
 *     Without this, http_post returns AGENT_ERROR_NOTSUP.
 */

#include "runtime_stm32.h"
#include "../types_internal.h"

#include <stdlib.h>
#include <string.h>

/* ── CMSIS core (always available on STM32) ─────────────────── */

#include <stm32xxxx.h>  /* family-specific, resolved by CMSIS device header */

#ifdef CAGENT_RUNTIME_STM32_FREERTOS

/* ── FreeRTOS profile ───────────────────────────────────────── */

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include <stm32_hal.h>  /* HAL_Delay */

static const char *STM_TAG = "cagent";

/* ── Basic callbacks ──────────────────────────────────────── */

static void *stm32_malloc(size_t size, void *user_data)
{
    (void)user_data;
    return malloc(size);
}

static void stm32_free(void *ptr, void *user_data)
{
    (void)user_data;
    free(ptr);
}

static uint64_t stm32_now_ms(void *user_data)
{
    (void)user_data;
    /* xTaskGetTickCount returns ticks; convert to ms.
     * portTICK_PERIOD_MS is 1 when configTICK_RATE_HZ=1000 (typical). */
    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void stm32_sleep_ms(uint32_t ms, void *user_data)
{
    (void)user_data;
    if (ms == 0u) return;
    HAL_Delay(ms);
}

/* STM32 bare-metal typically has no log target; some boards route a
 * UART.  Default to no-op — application layer overrides if needed. */
static void stm32_log(int level, const char *tag, const char *message,
                       void *user_data)
{
    (void)level; (void)tag; (void)message; (void)user_data;
    /* no-op: override with UART printf in application layer */
}

/* ── Mutex (FreeRTOS binary semaphore) ────────────────────── */

static void *stm32_mutex_create(void *user_data)
{
    (void)user_data;
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem) xSemaphoreGive(sem);
    return (void *)sem;
}

static void stm32_mutex_destroy(void *mutex, void *user_data)
{
    (void)user_data;
    if (mutex) vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

static int stm32_mutex_lock(void *mutex, uint32_t timeout_ms,
                             void *user_data)
{
    (void)user_data;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    if (!sem) return AGENT_ERROR_INVALID;

    TickType_t ticks = (timeout_ms == 0u)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);

    return (xSemaphoreTake(sem, ticks) == pdTRUE)
        ? AGENT_OK : AGENT_ERROR_TIMEOUT;
}

static void stm32_mutex_unlock(void *mutex, void *user_data)
{
    (void)user_data;
    if (mutex) xSemaphoreGive((SemaphoreHandle_t)mutex);
}

/* ── Critical section (FreeRTOS) ──────────────────────────── */

static uint32_t stm32_enter_critical(void *user_data)
{
    (void)user_data;
    taskENTER_CRITICAL();
    return 0u;
}

static void stm32_exit_critical(uint32_t state, void *user_data)
{
    (void)state;
    (void)user_data;
    taskEXIT_CRITICAL();
}

#else /* !CAGENT_RUNTIME_STM32_FREERTOS — bare-metal profile */

/* ── CMSIS intrinsic helpers ────────────────────────────────── */

#include <cmsis_gcc.h>  /* __disable_irq / __enable_irq */

/* ── Basic callbacks ──────────────────────────────────────── */

static void *stm32_malloc(size_t size, void *user_data)
{
    (void)user_data;
    return malloc(size);
}

static void stm32_free(void *ptr, void *user_data)
{
    (void)user_data;
    free(ptr);
}

static uint64_t stm32_now_ms(void *user_data)
{
    (void)user_data;
    /* HAL_GetTick() returns ms since boot.  Wraps every ~49 days
     * as uint32_t; upper 32 bits stay 0.  Acceptable for relative
     * timeouts (agent_run deadline). */
    return (uint64_t)HAL_GetTick();
}

static void stm32_sleep_ms(uint32_t ms, void *user_data)
{
    (void)user_data;
    if (ms == 0u) return;
    HAL_Delay(ms);
}

static void stm32_log(int level, const char *tag, const char *message,
                       void *user_data)
{
    (void)level; (void)tag; (void)message; (void)user_data;
    /* no-op; application may override */
}

/* ── Mutex (no-op for single-threaded bare-metal) ─────────── */

static void *stm32_mutex_create(void *user_data)
{
    (void)user_data;
    /* Return non-NULL so agent_create does create a mutex slot.
     * lock always succeeds, unlock is no-op. */
    return (void *)1;
}

static void stm32_mutex_destroy(void *mutex, void *user_data)
{
    (void)mutex; (void)user_data;
}

static int stm32_mutex_lock(void *mutex, uint32_t timeout_ms,
                             void *user_data)
{
    (void)mutex; (void)timeout_ms; (void)user_data;
    return AGENT_OK; /* single-threaded: never contended */
}

static void stm32_mutex_unlock(void *mutex, void *user_data)
{
    (void)mutex; (void)user_data;
}

/* ── Critical section (ARM CMSIS) ─────────────────────────── */

static uint32_t stm32_enter_critical(void *user_data)
{
    (void)user_data;
    /* Save current PRIMASK, disable interrupts.  Returns PRIMASK
     * state so exit_critical can restore the previous mask — doesn't
     * unconditionally re-enable if they were already masked. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void stm32_exit_critical(uint32_t state, void *user_data)
{
    (void)user_data;
    /* Restore previous interrupt mask state */
    __set_PRIMASK(state);
}

#endif /* CAGENT_RUNTIME_STM32_FREERTOS */

/* ── TLS / HTTPS (requires LWIP + mbedTLS) ──────────────────────── */

#ifdef CAGENT_RUNTIME_STM32_TLS

#include <errno.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#ifndef CAGENT_STM_TLS_READ_BUF_SIZE
#define CAGENT_STM_TLS_READ_BUF_SIZE 8192u
#endif

#ifndef CAGENT_STM_TLS_HDR_BUF_SIZE
#define CAGENT_STM_TLS_HDR_BUF_SIZE 4096u
#endif

#ifndef CAGENT_STM_SOCKET_TIMEOUT_SEC
#define CAGENT_STM_SOCKET_TIMEOUT_SEC 60
#endif

/* ── Chunked transfer decoding ──────────────────────────────── */

static size_t stm32_decode_chunked(char *buf, size_t len)
{
    char *src = buf;
    char *end = buf + len;
    char *dst = buf;

    while (src < end) {
        char *crlf = (char *)memmem(src, (size_t)(end - src), "\r\n", 2);
        if (!crlf) break;

        char *endptr;
        long chunk_sz = strtol(src, &endptr, 16);
        while (endptr < crlf && *endptr == ' ') endptr++;
        if (endptr != crlf || chunk_sz < 0
            || chunk_sz > (long)(end - crlf - 2)) break;
        if (chunk_sz == 0) break;

        src = crlf + 2;
        if (src + chunk_sz > end) chunk_sz = (long)(end - src);
        memmove(dst, src, (size_t)chunk_sz);
        dst += chunk_sz;
        src += chunk_sz;
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') src += 2;
    }

    return (size_t)(dst - buf);
}

/* ── TLS context ────────────────────────────────────────────── */

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config cfg;
    mbedtls_net_context net;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
} stm32_tls_ctx_t;

static void stm32_tls_ctx_free(stm32_tls_ctx_t *ctx)
{
    if (!ctx) return;
    mbedtls_ssl_close_notify(&ctx->ssl);
    mbedtls_net_free(&ctx->net);
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->cfg);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
}

static int stm32_tls_connect(stm32_tls_ctx_t *ctx,
                              const char *host,
                              const char *port,
                              uint32_t timeout_ms)
{
    int ret;
    const char *pers = "cagent_stm";

    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->cfg);
    mbedtls_net_init(&ctx->net);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_entropy_init(&ctx->entropy);

    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                 &ctx->entropy,
                                 (const unsigned char *)pers, strlen(pers));
    if (ret != 0) return AGENT_ERROR_NETWORK;

    if ((ret = mbedtls_net_connect(&ctx->net, host, port,
                                    MBEDTLS_NET_PROTO_TCP)) != 0)
        return AGENT_ERROR_NETWORK;

    mbedtls_net_set_block(&ctx->net);
    if (ctx->net.fd >= 0) {
        unsigned int tsec = timeout_ms
            ? (timeout_ms / 1000u) : CAGENT_STM_SOCKET_TIMEOUT_SEC;
        if (tsec == 0) tsec = CAGENT_STM_SOCKET_TIMEOUT_SEC;
        struct timeval tv = { .tv_sec = tsec, .tv_usec = 0 };
        lwip_setsockopt(ctx->net.fd, SOL_SOCKET, SO_RCVTIMEO,
                        &tv, sizeof(tv));
        lwip_setsockopt(ctx->net.fd, SOL_SOCKET, SO_SNDTIMEO,
                        &tv, sizeof(tv));
    }

    if ((ret = mbedtls_ssl_config_defaults(&ctx->cfg,
                                            MBEDTLS_SSL_IS_CLIENT,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
        return AGENT_ERROR_NETWORK;

    mbedtls_ssl_conf_min_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_3);
#else
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#endif

    /* STM32 + LWIP may have limited ALPN support; skip if unavailable */
#if defined(MBEDTLS_SSL_ALPN)
    {
        static const char *alpn_protos[] = { "http/1.1", NULL };
        mbedtls_ssl_conf_alpn_protocols(&ctx->cfg, alpn_protos);
    }
#endif

    mbedtls_ssl_conf_authmode(&ctx->cfg, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&ctx->cfg, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->cfg)) != 0)
        return AGENT_ERROR_NETWORK;

    if ((ret = mbedtls_ssl_set_hostname(&ctx->ssl, host)) != 0)
        return AGENT_ERROR_NETWORK;

    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ
            && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return AGENT_ERROR_NETWORK;
    }

    return AGENT_OK;
}

/* ── HTTP write / read (same shape as openvela adapter) ────── */

static int stm32_tls_write_request(stm32_tls_ctx_t *ctx,
                                    const char *method,
                                    const char *host,
                                    const char *path,
                                    const char *headers_str,
                                    const char *body,
                                    size_t body_len)
{
    char *hdr = malloc(CAGENT_STM_TLS_HDR_BUF_SIZE);
    int pos = 0;
    int n;
    int ret;

    if (!hdr) return AGENT_ERROR_NOMEM;

#define HDR_APPEND(fmt, ...)                                            \
    n = snprintf(hdr + pos, CAGENT_STM_TLS_HDR_BUF_SIZE - pos,         \
                 fmt, ##__VA_ARGS__);                                   \
    if (n < 0 || pos + n >= (int)CAGENT_STM_TLS_HDR_BUF_SIZE) {        \
        free(hdr);                                                      \
        return AGENT_ERROR_LIMIT;                                       \
    }                                                                   \
    pos += n;

    HDR_APPEND("%s %s HTTP/1.1\r\n", method, path);
    HDR_APPEND("Host: %s\r\n", host);
    HDR_APPEND("Connection: close\r\n");
    HDR_APPEND("User-Agent: cagent-stm32/1.0\r\n");

    if (body && body_len > 0) HDR_APPEND("Content-Length: %zu\r\n", body_len);
    if (headers_str && headers_str[0] != '\0') HDR_APPEND("%s", headers_str);
    HDR_APPEND("\r\n");
#undef HDR_APPEND

    int written = 0;
    while (written < pos) {
        ret = mbedtls_ssl_write(&ctx->ssl,
                                (const unsigned char *)(hdr + written),
                                (size_t)(pos - written));
        if (ret > 0) written += ret;
        else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        else { free(hdr); return AGENT_ERROR_NETWORK; }
    }
    free(hdr);

    if (body && body_len > 0) {
        size_t bw = 0;
        while (bw < body_len) {
            ret = mbedtls_ssl_write(&ctx->ssl,
                                    (const unsigned char *)(body + bw),
                                    body_len - bw);
            if (ret > 0) bw += (size_t)ret;
            else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            else return AGENT_ERROR_NETWORK;
        }
    }

    return AGENT_OK;
}

static int stm32_tls_read_response(stm32_tls_ctx_t *ctx,
                                    char *resp_buf,
                                    size_t resp_cap,
                                    size_t *out_body_len)
{
    char *raw = malloc(CAGENT_STM_TLS_READ_BUF_SIZE);
    if (!raw) return AGENT_ERROR_NOMEM;

    size_t raw_len = 0;
    int eof = 0;
    int ret;

    while (!eof && raw_len < CAGENT_STM_TLS_READ_BUF_SIZE - 1) {
        ret = mbedtls_ssl_read(&ctx->ssl,
                               (unsigned char *)(raw + raw_len),
                               CAGENT_STM_TLS_READ_BUF_SIZE - 1 - raw_len);
        if (ret > 0) {
            raw_len += (size_t)ret;
            raw[raw_len] = '\0';
            if (memmem(raw, raw_len, "\r\n\r\n", 4)) break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            eof = 1; break;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
            free(raw);
            return AGENT_ERROR_NETWORK;
        }
    }
    raw[raw_len] = '\0';

    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        free(raw);
        return AGENT_ERROR_PARSE;
    }

    char *body_start = (char *)memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!body_start) {
        resp_buf[0] = '\0';
        if (out_body_len) *out_body_len = 0;
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
            if (content_length < 0 || content_length > 10 * 1024 * 1024)
                content_length = -1;
        }
    }

    int chunked = 0;
    {
        char *te_hdr = strcasestr(raw, "Transfer-Encoding:");
        if (te_hdr && te_hdr < body_start)
            chunked = (strcasestr(te_hdr, "chunked") != NULL);
    }

    size_t initial = (size_t)(raw + raw_len - body_start);
    size_t resp_pos = 0;
    size_t copy = initial < resp_cap - 1 ? initial : resp_cap - 1;
    memcpy(resp_buf, body_start, copy);
    resp_pos = copy;
    free(raw);

    if (!eof) {
        while (resp_pos < resp_cap - 1) {
            if (content_length >= 0 && (long)resp_pos >= content_length) break;
            ret = mbedtls_ssl_read(&ctx->ssl,
                                   (unsigned char *)(resp_buf + resp_pos),
                                   resp_cap - 1 - resp_pos);
            if (ret > 0) resp_pos += (size_t)ret;
            else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
            else if (ret != MBEDTLS_ERR_SSL_WANT_READ) break;
        }
    }

    resp_buf[resp_pos] = '\0';

    if (chunked) {
        resp_pos = stm32_decode_chunked(resp_buf, resp_pos);
        resp_buf[resp_pos] = '\0';
    }

    if (out_body_len) *out_body_len = resp_pos;
    return http_status;
}

static int stm32_http_post(const agent_http_request_t *request,
                            agent_http_response_t *response,
                            void *user_data)
{
    stm32_tls_ctx_t ctx;
    int ret;

    (void)user_data;

    if (!request || !response || !response->body || response->body_size == 0u ||
        !request->host || !request->path || !request->port)
        return AGENT_ERROR_INVALID;

    memset(&ctx, 0, sizeof(ctx));

    ret = stm32_tls_connect(&ctx, request->host, request->port,
                             request->timeout_ms);
    if (ret != AGENT_OK) { stm32_tls_ctx_free(&ctx); return ret; }

    ret = stm32_tls_write_request(&ctx,
                                   request->method ? request->method : "POST",
                                   request->host,
                                   request->path,
                                   request->headers,
                                   (const char *)request->body,
                                   request->body_size);
    if (ret != AGENT_OK) { stm32_tls_ctx_free(&ctx); return ret; }

    size_t body_len = 0;
    int http_status = stm32_tls_read_response(&ctx, response->body,
                                               response->body_size, &body_len);
    stm32_tls_ctx_free(&ctx);

    if (http_status < 0) return http_status;

    response->status_code = http_status;
    response->bytes_written = body_len;
    return AGENT_OK;
}

#endif /* CAGENT_RUNTIME_STM32_TLS */

/* ── Public: agent_runtime_stm32_fill ──────────────────────────── */

void agent_runtime_stm32_fill(agent_runtime_t *runtime)
{
    if (!runtime) return;

    if (!runtime->malloc_fn)  runtime->malloc_fn  = stm32_malloc;
    if (!runtime->free_fn)    runtime->free_fn    = stm32_free;
    if (!runtime->now_ms)     runtime->now_ms     = stm32_now_ms;
    if (!runtime->sleep_ms)   runtime->sleep_ms   = stm32_sleep_ms;
    if (!runtime->log)        runtime->log        = stm32_log;

#ifdef CAGENT_RUNTIME_STM32_TLS
    if (!runtime->http_post) runtime->http_post = stm32_http_post;
#endif

    if (!runtime->mutex_create)      runtime->mutex_create      = stm32_mutex_create;
    if (!runtime->mutex_destroy)     runtime->mutex_destroy     = stm32_mutex_destroy;
    if (!runtime->mutex_lock)        runtime->mutex_lock        = stm32_mutex_lock;
    if (!runtime->mutex_unlock)      runtime->mutex_unlock      = stm32_mutex_unlock;
    if (!runtime->enter_critical)    runtime->enter_critical    = stm32_enter_critical;
    if (!runtime->exit_critical)     runtime->exit_critical     = stm32_exit_critical;
}
