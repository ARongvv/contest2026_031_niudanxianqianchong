/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT Context Provider 公共 API。
 *
 * Context provider 把动态上下文注入 LLM system message。
 * 典型用途：设备状态、用户信息、网络状态、记忆摘要。
 *
 * 构建顺序（由 context_builder.c 执行）：
 *   1. config.system_prompt          (CRITICAL，不可裁剪)
 *   2. context providers by priority (CRITICAL → NORMAL → OPTIONAL)
 *   3. skill context provider        (NORMAL)
 *
 * overflow 时不允许静默截断：
 *   - CRITICAL 放不下 → AGENT_ERROR_CONTEXT_OVERFLOW
 *   - NORMAL 放不下 → 跳过或输出摘要
 *   - OPTIONAL 放不下 → 直接跳过
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Context provider flags. No importance flag means NORMAL. */
#define AGENT_CONTEXT_FLAG_CRITICAL (1u << 0)
#define AGENT_CONTEXT_FLAG_OPTIONAL (1u << 1)
#define AGENT_CONTEXT_FLAG_DISABLED (1u << 2)

/**
 * Context provider 构建回调。
 *
 * buffer:      输出缓冲区
 * buffer_size: 缓冲区大小
 * written:     实际写入字节数（不含 '\0'）
 * user_data:   注册时传入的上下文
 *
 * 返回 AGENT_OK 成功，AGENT_ERROR_CONTEXT_OVERFLOW 表示空间不足。
 */
typedef int (*agent_context_provider_fn)(char *buffer,
                                         size_t buffer_size,
                                         size_t *written,
                                         void *user_data);

/**
 * Context provider 定义。
 *
 * priority: 数值越大越先构建，同优先级按注册顺序。
 * flags:    AGENT_CONTEXT_FLAG_* 组合。未设置 importance flag 时为 NORMAL。
 */
typedef struct {
    const char *name;                      /* 唯一标识 */
    uint32_t priority;                     /* 构建优先级，数值越大越靠前 */
    uint32_t flags;                        /* 预留 */
    agent_context_provider_fn build;       /* 构建回调，不可为 NULL */
    void *user_data;                       /* 传递给 build 的上下文 */
} agent_context_provider_t;

/** 注册 context provider。name 重复时返回 AGENT_ERROR_INVALID */
int agent_register_context_provider(agent_t *agent,
                                    const agent_context_provider_t *provider);
/** 注销 context provider */
int agent_unregister_context_provider(agent_t *agent, const char *name);

#ifdef __cplusplus
}
#endif
