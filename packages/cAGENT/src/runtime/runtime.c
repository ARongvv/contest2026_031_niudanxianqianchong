/* SPDX-License-Identifier: Apache-2.0 */
/**
 * POSIX/runtime 回退实现和 runtime 封装函数。
 *
 * 为 agent_runtime_t 中 NULL 的回调提供 libc/POSIX 默认实现：
 *   - malloc/free: 标准 C 库
 *   - now_ms: time(NULL) * 1000（秒级精度，生产环境应替换）
 *   - log: fprintf to stderr
 *   - http_post: 返回 NOTSUP（需应用层注入真实 HTTP）
 *
 * 封装函数（agent_runtime_malloc 等）提供 NULL 安全检查，
 * runtime 或回调为 NULL 时返回安全默认值。
 */

#include "runtime.h"
#include "runtime_openvela.h"
#include "runtime_espidf.h"
#include "runtime_stm32.h"

#include "../types_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ── POSIX fallback 回调 ── */

static void *default_malloc(size_t size, void *user_data)
{
    CAGENT_UNUSED(user_data);
    return malloc(size);
}

static void default_free(void *ptr, void *user_data)
{
    CAGENT_UNUSED(user_data);
    free(ptr);
}

static uint64_t default_now_ms(void *user_data)
{
    CAGENT_UNUSED(user_data);
    return (uint64_t)time(NULL) * 1000u;
}

static void default_log(int level, const char *tag, const char *message, void *user_data)
{
    CAGENT_UNUSED(user_data);
    fprintf(stderr,
            "[cagent][%d][%s] %s\n",
            level,
            tag ? tag : "runtime",
            message ? message : "");
}

static int default_http_post(const agent_http_request_t *request,
                             agent_http_response_t *response,
                             void *user_data)
{
    CAGENT_UNUSED(request);
    CAGENT_UNUSED(response);
    CAGENT_UNUSED(user_data);
    return AGENT_ERROR_NOTSUP;
}

/**
 * 填充平台特定 runtime 回调。
 *
 * 根据编译配置自动选择平台适配层：
 *   - 启用 CAGENT_RUNTIME_OPENVELA 时调用 agent_runtime_openvela_fill()
 *   - 启用 CAGENT_RUNTIME_ESPIDF   时调用 agent_runtime_espidf_fill()
 *   - 启用 CAGENT_RUNTIME_STM32    时调用 agent_runtime_stm32_fill()
 *   - 否则不做任何操作（由 fill_defaults 提供 POSIX fallback）
 *
 * 已设置的回调不被覆盖。
 * agent_create() 在 malloc_fn 为 NULL 时自动调用此函数。
 */
void agent_runtime_fill_platform(agent_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

#ifdef CAGENT_RUNTIME_OPENVELA
    agent_runtime_openvela_fill(runtime);
#endif
#ifdef CAGENT_RUNTIME_ESPIDF
    agent_runtime_espidf_fill(runtime);
#endif
#ifdef CAGENT_RUNTIME_STM32
    agent_runtime_stm32_fill(runtime);
#endif
}

/**
 * 填充 runtime 中 NULL 的回调为 POSIX 默认。
 *
 * 必须在 agent_create 之前或之中调用。
 * 已设置的回调不被覆盖。
 */
void agent_runtime_fill_defaults(agent_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    if (!runtime->malloc_fn) {
        runtime->malloc_fn = default_malloc;
    }
    if (!runtime->free_fn) {
        runtime->free_fn = default_free;
    }
    if (!runtime->now_ms) {
        runtime->now_ms = default_now_ms;
    }
    if (!runtime->log) {
        runtime->log = default_log;
    }
    if (!runtime->http_post) {
        runtime->http_post = default_http_post;
    }
}

/* ── 封装函数（NULL 安全） ── */

void *agent_runtime_malloc(agent_runtime_t *runtime, size_t size)
{
    if (!runtime || !runtime->malloc_fn) {
        return NULL;
    }
    return runtime->malloc_fn(size, runtime->user_data);
}

void agent_runtime_free(agent_runtime_t *runtime, void *ptr)
{
    if (runtime && runtime->free_fn) {
        runtime->free_fn(ptr, runtime->user_data);
    }
}

uint64_t agent_runtime_now_ms(agent_runtime_t *runtime)
{
    if (!runtime || !runtime->now_ms) {
        return 0u;
    }
    return runtime->now_ms(runtime->user_data);
}

void agent_runtime_sleep_ms(agent_runtime_t *runtime, uint32_t ms)
{
    if (runtime && runtime->sleep_ms) {
        runtime->sleep_ms(ms, runtime->user_data);
    }
}

void agent_runtime_log(agent_runtime_t *runtime,
                       int level,
                       const char *tag,
                       const char *message)
{
    if (runtime && runtime->log) {
        runtime->log(level, tag, message, runtime->user_data);
    }
}

int agent_runtime_http_post(agent_runtime_t *runtime,
                            const agent_http_request_t *request,
                            agent_http_response_t *response)
{
    if (!runtime || !runtime->http_post) {
        return AGENT_ERROR_NOTSUP;
    }
    return runtime->http_post(request, response, runtime->user_data);
}

/** mutex 为 NULL 或回调为 NULL 时返回 OK（单线程模式） */
int agent_runtime_mutex_lock(agent_runtime_t *runtime,
                             void *mutex,
                             uint32_t timeout_ms)
{
    if (!runtime || !mutex || !runtime->mutex_lock) {
        return AGENT_OK;
    }
    return runtime->mutex_lock(mutex, timeout_ms, runtime->user_data);
}

void agent_runtime_mutex_unlock(agent_runtime_t *runtime, void *mutex)
{
    if (runtime && mutex && runtime->mutex_unlock) {
        runtime->mutex_unlock(mutex, runtime->user_data);
    }
}

/** critical section 未实现时返回 0（无操作） */
uint32_t agent_runtime_enter_critical(agent_runtime_t *runtime)
{
    if (!runtime || !runtime->enter_critical) {
        return 0u;
    }
    return runtime->enter_critical(runtime->user_data);
}

void agent_runtime_exit_critical(agent_runtime_t *runtime, uint32_t state)
{
    if (runtime && runtime->exit_critical) {
        runtime->exit_critical(state, runtime->user_data);
    }
}
