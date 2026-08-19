/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 基础公共类型。
 *
 * 所有其他 cagent/x.h 头文件依赖此文件。应用代码不应直接依赖内部头文件。
 *
 * 编译期上限采用三级映射：
 *   1. 直接定义 CAGENT_MAX_TOOLS（最高优先级）
 *   2. 通过 Kconfig 生成 CONFIG_CAGENT_MAX_TOOLS
 *   3. 以上均未定义时使用默认值
 *
 * 所有权规则：
 *   - const char * 字段：调用者保证生命周期长于 agent，核心不拷贝
 *   - agent_request_t / agent_response_t：栈分配或由调用者管理
 *   - agent_t 内部状态：由 agent_create/agent_destroy 管理
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 编译期上限（三级映射：直接定义 > Kconfig > 默认值） ── */

#ifndef CAGENT_MAX_TOOLS
#ifdef CONFIG_CAGENT_MAX_TOOLS
#define CAGENT_MAX_TOOLS CONFIG_CAGENT_MAX_TOOLS
#else
#define CAGENT_MAX_TOOLS 12u
#endif
#endif

#ifndef CAGENT_MAX_SKILLS
#ifdef CONFIG_CAGENT_MAX_SKILLS
#define CAGENT_MAX_SKILLS CONFIG_CAGENT_MAX_SKILLS
#else
#define CAGENT_MAX_SKILLS 8u
#endif
#endif

#ifndef CAGENT_MAX_CONTEXT_PROVIDERS
#ifdef CONFIG_CAGENT_MAX_CONTEXT_PROVIDERS
#define CAGENT_MAX_CONTEXT_PROVIDERS CONFIG_CAGENT_MAX_CONTEXT_PROVIDERS
#else
#define CAGENT_MAX_CONTEXT_PROVIDERS 8u
#endif
#endif

#ifndef CAGENT_MAX_SESSIONS
#ifdef CONFIG_CAGENT_MAX_SESSIONS
#define CAGENT_MAX_SESSIONS CONFIG_CAGENT_MAX_SESSIONS
#else
#define CAGENT_MAX_SESSIONS 4u
#endif
#endif

#ifndef CAGENT_MAX_SESSION_MESSAGES
#ifdef CONFIG_CAGENT_MAX_SESSION_MESSAGES
#define CAGENT_MAX_SESSION_MESSAGES CONFIG_CAGENT_MAX_SESSION_MESSAGES
#else
#define CAGENT_MAX_SESSION_MESSAGES 24u
#endif
#endif

#ifndef CAGENT_CONTEXT_BUFFER_SIZE
#ifdef CONFIG_CAGENT_CONTEXT_BUFFER_SIZE
#define CAGENT_CONTEXT_BUFFER_SIZE CONFIG_CAGENT_CONTEXT_BUFFER_SIZE
#else
#define CAGENT_CONTEXT_BUFFER_SIZE 4096u
#endif
#endif

#ifndef CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE
#ifdef CONFIG_CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE
#define CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE CONFIG_CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE
#else
#define CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE CAGENT_CONTEXT_BUFFER_SIZE
#endif
#endif

#ifndef CAGENT_TOOL_SCHEMA_BUFFER_SIZE
#ifdef CONFIG_CAGENT_TOOL_SCHEMA_BUFFER_SIZE
#define CAGENT_TOOL_SCHEMA_BUFFER_SIZE CONFIG_CAGENT_TOOL_SCHEMA_BUFFER_SIZE
#else
#define CAGENT_TOOL_SCHEMA_BUFFER_SIZE CAGENT_CONTEXT_BUFFER_SIZE
#endif
#endif

#ifndef CAGENT_MESSAGES_BUFFER_SIZE
#ifdef CONFIG_CAGENT_MESSAGES_BUFFER_SIZE
#define CAGENT_MESSAGES_BUFFER_SIZE CONFIG_CAGENT_MESSAGES_BUFFER_SIZE
#else
#define CAGENT_MESSAGES_BUFFER_SIZE CAGENT_CONTEXT_BUFFER_SIZE
#endif
#endif

#ifndef CAGENT_HTTP_REQUEST_BUFFER_SIZE
#ifdef CONFIG_CAGENT_HTTP_REQUEST_BUFFER_SIZE
#define CAGENT_HTTP_REQUEST_BUFFER_SIZE CONFIG_CAGENT_HTTP_REQUEST_BUFFER_SIZE
#else
#define CAGENT_HTTP_REQUEST_BUFFER_SIZE CAGENT_CONTEXT_BUFFER_SIZE
#endif
#endif

#ifndef CAGENT_HTTP_RESPONSE_BUFFER_SIZE
#ifdef CONFIG_CAGENT_HTTP_RESPONSE_BUFFER_SIZE
#define CAGENT_HTTP_RESPONSE_BUFFER_SIZE CONFIG_CAGENT_HTTP_RESPONSE_BUFFER_SIZE
#else
#define CAGENT_HTTP_RESPONSE_BUFFER_SIZE CAGENT_CONTEXT_BUFFER_SIZE
#endif
#endif

#ifndef CAGENT_REQUEST_ARENA_EXTRA_SIZE
#ifdef CONFIG_CAGENT_REQUEST_ARENA_EXTRA_SIZE
#define CAGENT_REQUEST_ARENA_EXTRA_SIZE CONFIG_CAGENT_REQUEST_ARENA_EXTRA_SIZE
#else
#define CAGENT_REQUEST_ARENA_EXTRA_SIZE 0u
#endif
#endif

#ifndef CAGENT_REQUEST_ARENA_SIZE
#define CAGENT_REQUEST_ARENA_SIZE                         \
    (CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE +                  \
     CAGENT_TOOL_SCHEMA_BUFFER_SIZE +                     \
     CAGENT_MESSAGES_BUFFER_SIZE +                        \
     CAGENT_REQUEST_ARENA_EXTRA_SIZE)
#endif

#ifndef CAGENT_TOOL_ARGS_MAX_SIZE
#ifdef CONFIG_CAGENT_TOOL_ARGS_MAX_SIZE
#define CAGENT_TOOL_ARGS_MAX_SIZE CONFIG_CAGENT_TOOL_ARGS_MAX_SIZE
#else
#define CAGENT_TOOL_ARGS_MAX_SIZE 1024u
#endif
#endif

#ifndef CAGENT_TOOL_OUTPUT_MAX_SIZE
#ifdef CONFIG_CAGENT_TOOL_OUTPUT_MAX_SIZE
#define CAGENT_TOOL_OUTPUT_MAX_SIZE CONFIG_CAGENT_TOOL_OUTPUT_MAX_SIZE
#else
#define CAGENT_TOOL_OUTPUT_MAX_SIZE 1024u
#endif
#endif

#ifndef CAGENT_DEFAULT_MAX_STEPS
#ifdef CONFIG_CAGENT_DEFAULT_MAX_STEPS
#define CAGENT_DEFAULT_MAX_STEPS CONFIG_CAGENT_DEFAULT_MAX_STEPS
#else
#define CAGENT_DEFAULT_MAX_STEPS 8u
#endif
#endif

#ifndef CAGENT_DEFAULT_TIMEOUT_MS
#ifdef CONFIG_CAGENT_DEFAULT_TIMEOUT_MS
#define CAGENT_DEFAULT_TIMEOUT_MS CONFIG_CAGENT_DEFAULT_TIMEOUT_MS
#else
#define CAGENT_DEFAULT_TIMEOUT_MS 30000u
#endif
#endif

#ifndef CAGENT_DEFAULT_MODEL_TIMEOUT_MS
#ifdef CONFIG_CAGENT_DEFAULT_MODEL_TIMEOUT_MS
#define CAGENT_DEFAULT_MODEL_TIMEOUT_MS CONFIG_CAGENT_DEFAULT_MODEL_TIMEOUT_MS
#else
#define CAGENT_DEFAULT_MODEL_TIMEOUT_MS 15000u
#endif
#endif

#ifndef CAGENT_DEFAULT_TOOL_TIMEOUT_MS
#ifdef CONFIG_CAGENT_DEFAULT_TOOL_TIMEOUT_MS
#define CAGENT_DEFAULT_TOOL_TIMEOUT_MS CONFIG_CAGENT_DEFAULT_TOOL_TIMEOUT_MS
#else
#define CAGENT_DEFAULT_TOOL_TIMEOUT_MS 3000u
#endif
#endif

#ifndef CAGENT_DEFAULT_MAX_OUTPUT_TOKENS
#ifdef CONFIG_CAGENT_DEFAULT_MAX_OUTPUT_TOKENS
#define CAGENT_DEFAULT_MAX_OUTPUT_TOKENS CONFIG_CAGENT_DEFAULT_MAX_OUTPUT_TOKENS
#else
#define CAGENT_DEFAULT_MAX_OUTPUT_TOKENS 512u
#endif
#endif

/* 不透明 Agent 句柄。内部定义在 src/core/agent_internal.h */
typedef struct agent agent_t;

/**
 * 错误码。
 *
 * 工具级错误（tool not found、policy denied）不使用这些码，
 * 而是写入 tool result JSON 让模型继续推理。
 */
typedef enum {
    AGENT_OK = 0,
    AGENT_ERROR = -1,             /* 未分类错误 */
    AGENT_ERROR_NOMEM = -2,       /* 内存分配失败 */
    AGENT_ERROR_INVALID = -3,     /* 参数无效 */
    AGENT_ERROR_BUSY = -4,        /* agent 正忙，不可重入 */
    AGENT_ERROR_LIMIT = -5,       /* 迭代/缓冲区/消息上限溢出 */
    AGENT_ERROR_TIMEOUT = -6,     /* 整体/模型/工具超时 */
    AGENT_ERROR_CANCELLED = -7,   /* 外部请求取消 */
    AGENT_ERROR_CONTEXT_OVERFLOW = -8, /* context 拼接溢出，不可静默截断 */
    AGENT_ERROR_MODEL = -9,       /* 模型 provider 返回错误 */
    AGENT_ERROR_NETWORK = -10,    /* 网络传输失败 */
    AGENT_ERROR_TOOL = -11,       /* 工具执行失败 */
    AGENT_ERROR_POLICY_DENIED = -12, /* 策略回调拒绝 */
    AGENT_ERROR_NOTFOUND = -13,   /* 工具/skill/session 未找到 */
    AGENT_ERROR_NOTSUP = -14,     /* 功能未实现或未启用 */
    AGENT_ERROR_PARSE = -15       /* JSON/模型响应解析失败 */
} agent_error_t;

/**
 * Agent 运行限制。
 *
 * request->limits 可临时覆盖 agent->limits，请求结束后恢复。
 * timeout_ms = 0 表示不限制。
 */
typedef struct {
    uint32_t max_steps;            /* ReAct 最大迭代轮数 */
    uint32_t timeout_ms;           /* 单次 agent_run 整体超时，0 = 不限 */
    uint32_t per_model_timeout_ms; /* 单次模型调用超时，传给 provider */
    uint32_t per_tool_timeout_ms;  /* 单次工具执行超时，0 = 使用此默认值 */
    uint32_t max_tool_calls;       /* 单次请求内工具调用总数上限 */
    uint32_t max_output_tokens;    /* 模型响应 token 预算，映射到 provider 的 max_tokens */
} agent_limits_t;

/** Agent 累计统计。由 agent_run 自动更新，可通过 agent_get_stats 读取 */
typedef struct {
    uint32_t runs;                 /* 总运行次数 */
    uint32_t completed_runs;       /* 成功完成次数 */
    uint32_t failed_runs;          /* 失败次数（含 timeout/cancel） */
    uint32_t cancelled_runs;       /* 被取消次数 */
    uint32_t timeout_runs;         /* 超时次数 */
    uint32_t iterations;           /* 累计迭代轮数 */
    uint32_t model_calls;          /* 累计模型调用次数 */
    uint32_t tool_calls;           /* 累计工具调用次数 */
    uint64_t last_run_elapsed_ms;  /* 最近一次 agent_run 耗时 */
    size_t last_run_arena_peak_bytes; /* 最近一次 agent_run request arena 峰值 */
    size_t arena_peak_bytes;       /* 历史 request arena 峰值 */
} agent_stats_t;

/**
 * Agent 请求。
 *
 * 所有权：所有指针由调用者持有，必须在 agent_run 期间有效。
 * limits 可为 NULL（使用 agent 当前 limits）；trace_id 可为 NULL。
 */
typedef struct {
    const char *session_id;        /* 会话标识，NULL = 默认会话 */
    const char *input;             /* 用户输入文本，不可为 NULL */
    const char *trace_id;          /* 可选追踪 ID，透传到 event */
    const agent_limits_t *limits;  /* 可选临时覆盖，NULL = 使用 agent limits */
    void *user_data;               /* 可选用户数据，不核心使用 */
} agent_request_t;

/**
 * Agent 响应。
 *
 * 调用者负责分配 output 缓冲区。agent_run 成功时 output 包含最终回复，
 * 失败时 output 为空字符串。
 */
typedef struct {
    char *output;                  /* 调用者分配的输出缓冲区 */
    size_t output_size;            /* 缓冲区大小 */
    int status;                    /* agent_error_t 结果码 */
    agent_stats_t stats;           /* 本次运行的统计快照 */
} agent_response_t;

#ifdef __cplusplus
}
#endif
