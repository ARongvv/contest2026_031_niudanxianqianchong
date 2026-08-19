/* SPDX-License-Identifier: Apache-2.0 */
/**
 * ESP-IDF / FreeRTOS runtime adapter implementation.
 *
 * Compiled when the build enables CAGENT_RUNTIME_ESPIDF.
 * All ESP-IDF / FreeRTOS specific includes are isolated in this file.
 *
 * The portable core works without this file (using POSIX fallback).
 *
 * ESP-IDF provides:
 *   - FreeRTOS (tasks, semaphores, critical sections)
 *   - mbedTLS (bundled, with ESP-IDF specific configuration)
 *   - esp_timer (high-resolution monotonic timer)
 *   - ESP_LOGx (tag-based logging with level filtering)
 */

#include "runtime_espidf.h"
#include "../types_internal.h"

#include <stdlib.h>
#include <string.h>

/* ── ESP-IDF / FreeRTOS system headers ──────────────────────── */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <esp_timer.h>
#include <esp_log.h>

static const char *ESP_TAG = "cagent";

/* ── TLS / HTTPS (mbedTLS bundled with ESP-IDF) ─────────────── */

#ifdef CAGENT_RUNTIME_ESPIDF_TLS

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#ifndef CAGENT_ESP_TLS_READ_BUF_SIZE
#define CAGENT_ESP_TLS_READ_BUF_SIZE 8192u
#endif

#ifndef CAGENT_ESP_TLS_HDR_BUF_SIZE
#define CAGENT_ESP_TLS_HDR_BUF_SIZE 4096u
#endif

#ifndef CAGENT_ESP_SOCKET_TIMEOUT_SEC
#define CAGENT_ESP_SOCKET_TIMEOUT_SEC 60
#endif

/* ── Chunked transfer decoding ──────────────────────────────── */

static size_t esp_decode_chunked(char *buf, size_t len)
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
        if (endptr != crlf || chunk_sz < 0 || chunk_sz > (long)(end - crlf - 2)) break;
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
} esp_tls_ctx_t;

static void esp_tls_ctx_free(esp_tls_ctx_t *ctx)
{
    if (!ctx) return;
    mbedtls_ssl_close_notify(&ctx->ssl);
    mbedtls_net_free(&ctx->net);
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->cfg);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
}

static int esp_tls_connect(esp_tls_ctx_t *ctx,
                           const char *host,
                           const char *port,
                           uint32_t timeout_ms)
{
    int ret;
    const char *pers = "cagent_esp";

    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->cfg);
    mbedtls_net_init(&ctx->net);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_entropy_init(&ctx->entropy);

    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                 &ctx->entropy,
                                 (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        ESP_LOGE(ESP_TAG, "ctr_drbg_seed ret=-0x%04x", -ret);
        return AGENT_ERROR_NETWORK;
    }

    if ((ret = mbedtls_net_connect(&ctx->net, host, port,
                                    MBEDTLS_NET_PROTO_TCP)) != 0) {
        ESP_LOGE(ESP_TAG, "net_connect %s:%s ret=-0x%04x", host, port, -ret);
        return AGENT_ERROR_NETWORK;
    }

    mbedtls_net_set_block(&ctx->net);
    if (ctx->net.fd >= 0) {
        unsigned int tsec = timeout_ms ? (timeout_ms / 1000u) : CAGENT_ESP_SOCKET_TIMEOUT_SEC;
        if (tsec == 0) tsec = CAGENT_ESP_SOCKET_TIMEOUT_SEC;
        struct timeval tv = { .tv_sec = tsec, .tv_usec = 0 };
        setsockopt(ctx->net.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(ctx->net.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    if ((ret = mbedtls_ssl_config_defaults(&ctx->cfg,
                                            MBEDTLS_SSL_IS_CLIENT,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        ESP_LOGE(ESP_TAG, "ssl_config_defaults ret=-0x%04x", -ret);
        return AGENT_ERROR_NETWORK;
    }

    mbedtls_ssl_conf_min_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_3);
#else
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#endif

#if defined(MBEDTLS_SSL_ALPN)
    {
        static const char *alpn_protos[] = { "http/1.1", NULL };
        mbedtls_ssl_conf_alpn_protocols(&ctx->cfg, alpn_protos);
    }
#endif

    /* ESP-IDF typically uses VERIFY_OPTIONAL in dev; production should
     * use VERIFY_REQUIRED with proper cert bundle.  Keep VERIFY_OPTIONAL
     * here for MVP — callers can override http_post with custom TLS. */
    mbedtls_ssl_conf_authmode(&ctx->cfg, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&ctx->cfg, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->cfg)) != 0) {
        ESP_LOGE(ESP_TAG, "ssl_setup ret=-0x%04x", -ret);
        return AGENT_ERROR_NETWORK;
    }

    if ((ret = mbedtls_ssl_set_hostname(&ctx->ssl, host)) != 0) {
        ESP_LOGE(ESP_TAG, "ssl_set_hostname ret=-0x%04x", -ret);
        return AGENT_ERROR_NETWORK;
    }

    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
#if defined(MBEDTLS_ERROR_C)
            char err_buf[128];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            ESP_LOGE(ESP_TAG, "ssl_handshake ret=-0x%04x: %s", -ret, err_buf);
#else
            ESP_LOGE(ESP_TAG, "ssl_handshake ret=-0x%04x", -ret);
#endif
            return AGENT_ERROR_NETWORK;
        }
    }

    ESP_LOGI(ESP_TAG, "TLS handshake OK: %s / %s",
             mbedtls_ssl_get_version(&ctx->ssl),
             mbedtls_ssl_get_ciphersuite(&ctx->ssl));

    return AGENT_OK;
}

/* ── HTTP/1.1 request write ─────────────────────────────────── */

static int esp_tls_write_request(esp_tls_ctx_t *ctx,
                                  const char *method,
                                  const char *host,
                                  const char *path,
                                  const char *headers_str,
                                  const char *body,
                                  size_t body_len)
{
    char *hdr = malloc(CAGENT_ESP_TLS_HDR_BUF_SIZE);
    int pos = 0;
    int n;
    int ret;

    if (!hdr) return AGENT_ERROR_NOMEM;

#define HDR_APPEND(fmt, ...)                                           \
    n = snprintf(hdr + pos, CAGENT_ESP_TLS_HDR_BUF_SIZE - pos,         \
                 fmt, ##__VA_ARGS__);                                   \
    if (n < 0 || pos + n >= (int)CAGENT_ESP_TLS_HDR_BUF_SIZE) {       \
        free(hdr);                                                      \
        return AGENT_ERROR_LIMIT;                                       \
    }                                                                   \
    pos += n;

    HDR_APPEND("%s %s HTTP/1.1\r\n", method, path);
    HDR_APPEND("Host: %s\r\n", host);
    HDR_APPEND("Connection: close\r\n");
    HDR_APPEND("User-Agent: cagent-espidf/1.0\r\n");

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

/* ── HTTP/1.1 response read ─────────────────────────────────── */

static int esp_tls_read_response(esp_tls_ctx_t *ctx,
                                  char *resp_buf,
                                  size_t resp_cap,
                                  size_t *out_body_len)
{
    char *raw = malloc(CAGENT_ESP_TLS_READ_BUF_SIZE);
    if (!raw) return AGENT_ERROR_NOMEM;

    size_t raw_len = 0;
    int eof = 0;
    int ret;

    while (!eof && raw_len < CAGENT_ESP_TLS_READ_BUF_SIZE - 1) {
        ret = mbedtls_ssl_read(&ctx->ssl,
                               (unsigned char *)(raw + raw_len),
                               CAGENT_ESP_TLS_READ_BUF_SIZE - 1 - raw_len);
        if (ret > 0) {
            raw_len += (size_t)ret;
            raw[raw_len] = '\0';
            if (memmem(raw, raw_len, "\r\n\r\n", 4)) break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            eof = 1; break;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
            ESP_LOGE(ESP_TAG, "ssl_read (header) ret=-0x%04x", -ret);
            free(raw);
            return AGENT_ERROR_NETWORK;
        }
    }
    raw[raw_len] = '\0';

    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        ESP_LOGE(ESP_TAG, "Failed to parse HTTP status");
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
        resp_pos = esp_decode_chunked(resp_buf, resp_pos);
        resp_buf[resp_pos] = '\0';
    }

    if (out_body_len) *out_body_len = resp_pos;
    return http_status;
}

/* ── http_post callback ─────────────────────────────────────── */

static int esp_http_post(const agent_http_request_t *request,
                          agent_http_response_t *response,
                          void *user_data)
{
    esp_tls_ctx_t ctx;
    int ret;

    (void)user_data;

    if (!request || !response || !response->body || response->body_size == 0u ||
        !request->host || !request->path || !request->port)
        return AGENT_ERROR_INVALID;

    memset(&ctx, 0, sizeof(ctx));

    ret = esp_tls_connect(&ctx, request->host, request->port, request->timeout_ms);
    if (ret != AGENT_OK) { esp_tls_ctx_free(&ctx); return ret; }

    ret = esp_tls_write_request(&ctx,
                                 request->method ? request->method : "POST",
                                 request->host,
                                 request->path,
                                 request->headers,
                                 (const char *)request->body,
                                 request->body_size);
    if (ret != AGENT_OK) { esp_tls_ctx_free(&ctx); return ret; }

    size_t body_len = 0;
    int http_status = esp_tls_read_response(&ctx, response->body,
                                             response->body_size, &body_len);
    esp_tls_ctx_free(&ctx);

    if (http_status < 0) return http_status;

    response->status_code = http_status;
    response->bytes_written = body_len;
    return AGENT_OK;
}

#endif /* CAGENT_RUNTIME_ESPIDF_TLS */

/* ── Basic runtime callbacks ────────────────────────────────── */

static void *esp_malloc(size_t size, void *user_data)
{
    (void)user_data;
    return malloc(size);
}

static void esp_free(void *ptr, void *user_data)
{
    (void)user_data;
    free(ptr);
}

static uint64_t esp_now_ms(void *user_data)
{
    (void)user_data;
    /* esp_timer_get_time() returns microseconds since boot, monotonic */
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void esp_sleep_ms(uint32_t ms, void *user_data)
{
    (void)user_data;
    if (ms == 0u) { vTaskDelay(1); return; }
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void esp_log(int level, const char *tag, const char *message, void *user_data)
{
    (void)user_data;
    const char *lvl_tag = tag ? tag : ESP_TAG;
    const char *msg = message ? message : "";

    switch (level) {
    case 0: ESP_LOGE(lvl_tag, "%s", msg); break;
    case 1: ESP_LOGW(lvl_tag, "%s", msg); break;
    case 2: ESP_LOGI(lvl_tag, "%s", msg); break;
    default: ESP_LOGD(lvl_tag, "%s", msg); break;
    }
}

/* ── Mutex callbacks (FreeRTOS mutex = binary semaphore) ────── */

static void *esp_mutex_create(void *user_data)
{
    (void)user_data;
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem) xSemaphoreGive(sem); /* start available */
    return (void *)sem;
}

static void esp_mutex_destroy(void *mutex, void *user_data)
{
    (void)user_data;
    if (mutex) vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

static int esp_mutex_lock(void *mutex, uint32_t timeout_ms, void *user_data)
{
    (void)user_data;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    if (!sem) return AGENT_ERROR_INVALID;

    TickType_t ticks = (timeout_ms == 0u)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(sem, ticks) == pdTRUE)
        return AGENT_OK;
    return AGENT_ERROR_TIMEOUT;
}

static void esp_mutex_unlock(void *mutex, void *user_data)
{
    (void)user_data;
    if (mutex) xSemaphoreGive((SemaphoreHandle_t)mutex);
}

/* ── Critical section callbacks ─────────────────────────────── */

static uint32_t esp_enter_critical(void *user_data)
{
    (void)user_data;
    /* ESP-IDF FreeRTOS: save current interrupt level + disable interrupts.
     * portENTER_CRITICAL returns nothing on ESP-IDF; we use a static to
     * track nesting and always exit with portEXIT_CRITICAL. */
    portENTER_CRITICAL(NULL);
    return 0u;
}

static void esp_exit_critical(uint32_t state, void *user_data)
{
    (void)state;
    (void)user_data;
    portEXIT_CRITICAL(NULL);
}

/* ── Public: agent_runtime_espidf_fill ──────────────────────── */

void agent_runtime_espidf_fill(agent_runtime_t *runtime)
{
    if (!runtime) return;

    if (!runtime->malloc_fn)  runtime->malloc_fn  = esp_malloc;
    if (!runtime->free_fn)    runtime->free_fn    = esp_free;
    if (!runtime->now_ms)     runtime->now_ms     = esp_now_ms;
    if (!runtime->sleep_ms)   runtime->sleep_ms   = esp_sleep_ms;
    if (!runtime->log)        runtime->log        = esp_log;

#ifdef CAGENT_RUNTIME_ESPIDF_TLS
    if (!runtime->http_post) runtime->http_post = esp_http_post;
#endif

    if (!runtime->mutex_create)      runtime->mutex_create      = esp_mutex_create;
    if (!runtime->mutex_destroy)     runtime->mutex_destroy     = esp_mutex_destroy;
    if (!runtime->mutex_lock)        runtime->mutex_lock        = esp_mutex_lock;
    if (!runtime->mutex_unlock)      runtime->mutex_unlock      = esp_mutex_unlock;
    if (!runtime->enter_critical)    runtime->enter_critical    = esp_enter_critical;
    if (!runtime->exit_critical)     runtime->exit_critical     = esp_exit_critical;
}
