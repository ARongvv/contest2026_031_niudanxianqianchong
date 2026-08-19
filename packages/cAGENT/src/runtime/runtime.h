/* SPDX-License-Identifier: Apache-2.0 */
/**
 * 运行时内部辅助函数声明。
 *
 * 对 agent_runtime_t 回调的封装，提供：
 *   - NULL 安全检查（runtime 或回调为 NULL 时返回安全默认值）
 *   - 统一的 user_data 透传
 *   - fill defaults 逻辑
 *
 * 公共运行时定义在 include/cagent/runtime.h，此文件不定义第二套 API。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <agent.h>

/** 为 runtime 中 NULL 的回调填充 POSIX fallback */
void agent_runtime_fill_defaults(agent_runtime_t *runtime);

/**
 * 填充平台特定 runtime 回调（openvela / ...）。
 *
 * agent_create() 在 runtime.malloc_fn 为 NULL 时自动调用。
 * 应用层也可手动调用以提前填充平台回调。
 */
void agent_runtime_fill_platform(agent_runtime_t *runtime);

/* ── 回调封装（NULL 安全） ── */

void *agent_runtime_malloc(agent_runtime_t *runtime, size_t size);
void agent_runtime_free(agent_runtime_t *runtime, void *ptr);
uint64_t agent_runtime_now_ms(agent_runtime_t *runtime);
void agent_runtime_sleep_ms(agent_runtime_t *runtime, uint32_t ms);
void agent_runtime_log(agent_runtime_t *runtime,
                       int level,
                       const char *tag,
                       const char *message);
int agent_runtime_http_post(agent_runtime_t *runtime,
                            const agent_http_request_t *request,
                            agent_http_response_t *response);

/* mutex 未创建或回调为 NULL 时，lock 返回 OK（单线程模式） */
int agent_runtime_mutex_lock(agent_runtime_t *runtime,
                             void *mutex,
                             uint32_t timeout_ms);
void agent_runtime_mutex_unlock(agent_runtime_t *runtime, void *mutex);

/* critical section 未实现时返回 0（无操作） */
uint32_t agent_runtime_enter_critical(agent_runtime_t *runtime);
void agent_runtime_exit_critical(agent_runtime_t *runtime, uint32_t state);
