/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Agent 内部状态定义。
 *
 * 公共代码只能看到 opaque agent_t，内部结构在此定义。
 * 所有跨模块内部函数在此声明。
 *
 * TODO: 补充 registry 存储（tools/skills/context_providers/sessions 固定数组）
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <agent.h>

#include "../memory/memory_internal.h"
#include "../skills/skills_internal.h"
#include "../tools/tools_internal.h"

/** 模型句柄内部定义 */
struct agent_model {
    agent_model_ops_t ops;         /* provider 操作集 */
    void *provider;                /* provider 上下文，传给 ops 回调 */
};

typedef struct {
    unsigned char *buffer;
    size_t size;
    size_t used;
    size_t peak;
} agent_arena_t;

/**
 * Agent 内部状态。
 *
 * 生命周期：agent_create() 分配 → agent_destroy() 释放。
 * busy 标志保证 agent_run 不可重入。
 * cancel_requested 通过 critical section 保护（多核/ISR 安全）。
 */
struct agent {
    agent_config_t config;         /* 创建时拷贝的配置 */
    agent_runtime_t runtime;       /* 运行时回调（已 fill defaults） */
    agent_model_t *model;          /* 已 attach 的模型，NULL = 未设置 */
    bool model_owned;              /* true = agent_destroy 时自动销毁 model */

    agent_event_cb_t event_cb;     /* 事件回调，NULL = 不通知 */
    void *event_user_data;
    agent_policy_cb_t policy_cb;   /* 策略回调，NULL = 默认 ALLOW */
    void *policy_user_data;

    bool busy;                     /* agent_run 进行中，防重入 */
    volatile bool cancel_requested;/* 外部取消请求，需 critical section 保护 */
    uint64_t run_start_ms;         /* 当前 agent_run 起始时间 */
    uint64_t run_deadline_ms;      /* 当前 agent_run 截止时间，0 = 不限 */
    agent_limits_t limits;         /* 当前生效的 limits（可能被 request 临时覆盖） */
    agent_stats_t stats;           /* 累计统计 */
    uint32_t current_run_tool_calls;/* 当前 agent_run 内已执行/尝试执行工具数 */
    agent_arena_t request_arena;   /* agent_run 级临时 arena，loop 结束统一释放 */

    void *mutex;                   /* 可选互斥量，NULL = 单线程模式 */

    agent_tool_entry_t tools[CAGENT_MAX_TOOLS];
    uint32_t tool_count;
    bool tool_schema_dirty;
    uint32_t tool_schema_version;
    size_t tool_schema_cache_len;
    char tool_schema_cache[CAGENT_TOOL_SCHEMA_BUFFER_SIZE];

    agent_skill_entry_t skills[CAGENT_MAX_SKILLS];
    uint32_t skill_count;
    uint32_t skill_order_next;

    agent_context_provider_t context_providers[CAGENT_MAX_CONTEXT_PROVIDERS];
    uint32_t context_provider_count;
    uint32_t context_provider_order_next;

    agent_session_t sessions[CAGENT_MAX_SESSIONS];
    uint32_t session_count;
    char tool_result_buffer[CAGENT_TOOL_OUTPUT_MAX_SIZE];
};

/* ── 跨模块内部函数声明 ── */

/** ReAct 推理循环执行体，由 agent_run 调用 */
int agent_loop_run(agent_t *agent,
                   const agent_request_t *request,
                   agent_response_t *response);

/** 统一事件派发。自动填充 timestamp_ms/trace_id/session_id */
void agent_event_emit(agent_t *agent,
                      agent_event_type_t type,
                      const agent_request_t *request,
                      uint32_t iteration,
                      int error_code,
                      const char *message,
                      const char *tool_name,
                      const char *tool_call_id);

/** 构建系统消息。当前只拼 system_prompt，后续加入 provider 和 skill */
int agent_context_build(agent_t *agent,
                        const agent_request_t *request,
                        char *buffer,
                        size_t buffer_size,
                        size_t *written);

/** 构建工具 JSON schema。当前输出 "[]"，后续遍历 registry 生成 */
int agent_tool_schema_build(agent_t *agent,
                            char *buffer,
                            size_t buffer_size,
                            size_t *written);

/**
 * 执行单个工具调用。
 *
 * 执行链：registry lookup → policy → guard → handler → 归一化 result
 * 当前返回 NOTSUP（registry 未实现）。
 */
int agent_tool_execute(agent_t *agent,
                       const agent_tool_call_t *call,
                       agent_tool_result_t *result);

/** 追加消息到 session（公共 API 入口，根据 role 分发到内部 add 函数） */
int agent_session_append(agent_t *agent,
                         const char *session_id,
                         const agent_message_t *message);
