/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Session 和 memory 内部结构定义。
 *
 * 私有于 src/memory 和 agent_loop。
 *
 * Session 必须保持 tool calling 消息链完整：
 *   user → assistant(tool_calls) → tool(call_id) → assistant(final)
 *
 * 淘汰规则（按完整 turn）：
 *   - turn 起点：user message
 *   - turn 终点：assistant final message（不含 tool_calls 的 assistant 消息）
 *   - 只淘汰完整 turn
 *   - 不允许只删 tool message 留下孤立 assistant(tool_calls)
 *   - 尾部未完成 turn 不参与淘汰
 *   - 无完整 turn 可淘汰时返回 AGENT_ERROR_LIMIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <agent.h>

/* ── 每条消息内容缓冲区上限 ── */

#ifndef CAGENT_SESSION_CONTENT_MAX_SIZE
#ifdef CONFIG_CAGENT_SESSION_CONTENT_MAX_SIZE
#define CAGENT_SESSION_CONTENT_MAX_SIZE CONFIG_CAGENT_SESSION_CONTENT_MAX_SIZE
#else
#define CAGENT_SESSION_CONTENT_MAX_SIZE 512u
#endif
#endif

/* assistant tool_calls 消息中，每个 tool_call 的序列化上限 */
#ifndef CAGENT_SESSION_TOOL_CALL_MAX_SIZE
#ifdef CONFIG_CAGENT_SESSION_TOOL_CALL_MAX_SIZE
#define CAGENT_SESSION_TOOL_CALL_MAX_SIZE CONFIG_CAGENT_SESSION_TOOL_CALL_MAX_SIZE
#else
#define CAGENT_SESSION_TOOL_CALL_MAX_SIZE 256u
#endif
#endif

/* assistant tool_calls 消息中的最大 tool_call 数 */
#ifndef CAGENT_SESSION_MAX_TOOL_CALLS
#ifdef CONFIG_CAGENT_SESSION_MAX_TOOL_CALLS
#define CAGENT_SESSION_MAX_TOOL_CALLS CONFIG_CAGENT_SESSION_MAX_TOOL_CALLS
#else
#define CAGENT_SESSION_MAX_TOOL_CALLS 4u
#endif
#endif

/* ── 内部消息条目 ── */

/**
 * 序列化的 tool_call 条目。
 *
 * 存储在 assistant(tool_calls) 消息中，包含 call id + name + arguments。
 * 均为内拷贝，不持有外部指针。
 */
typedef struct {
    char id[64];                          /* call id，由模型生成 */
    char name[64];                        /* 工具名 */
    char arguments_json[CAGENT_SESSION_TOOL_CALL_MAX_SIZE]; /* 调用参数 JSON */
} agent_session_tool_call_t;

/**
 * Session 内部消息条目。
 *
 * 所有关键字段内拷贝到固定缓冲区，不持有外部指针。
 * - role = USER / ASSISTANT / TOOL 时，content 存文本内容
 * - role = ASSISTANT 且有 tool_calls 时，content 可为空或含部分文本，
 *   tool_calls 数组存储调用信息
 * - role = TOOL 时，tool_call_id 关联 assistant 的 tool_call
 * - turn_boundary 标记 turn 的起始消息（user message）
 */
typedef struct {
    agent_message_role_t role;            /* 消息角色 */
    char content[CAGENT_SESSION_CONTENT_MAX_SIZE]; /* 文本内容，内拷贝 */
    size_t content_len;                   /* 内容实际长度（不含 '\0'） */

    /* 仅 AGENT_MESSAGE_ROLE_TOOL 使用 */
    char tool_call_id[64];               /* 关联 assistant tool_calls 的 id */

    /* 仅 AGENT_MESSAGE_ROLE_ASSISTANT + tool_calls 使用 */
    agent_session_tool_call_t tool_calls[CAGENT_SESSION_MAX_TOOL_CALLS];
    uint32_t tool_call_count;             /* tool_calls 数组实际长度 */

    uint64_t timestamp_ms;                /* 消息时间戳，append 时填充 */
    bool turn_boundary;                   /* true = 此消息是一个 turn 的起点（user） */
} agent_session_entry_t;

/* ── Session 存储 ── */

/**
 * 单个 Session。
 *
 * 固定大小消息数组，消息满时按完整 turn 淘汰。
 * session_id 为应用层标识，当前 MVP 使用 "default" 单会话。
 */
typedef struct {
    char id[64];                          /* session 标识 */
    agent_session_entry_t entries[CAGENT_MAX_SESSION_MESSAGES];
    uint32_t count;                       /* 当前消息数 */
    uint32_t complete_turns;              /* 已完成 turn 数（用于淘汰计数） */
} agent_session_t;

/* ── Session 内部函数声明 ── */

/** 查找或创建 session。不存在时自动创建，满了返回 NULL */
agent_session_t *agent_session_find_or_create(agent_t *agent, const char *session_id);

/** 按 id 查找 session，不存在返回 NULL */
agent_session_t *agent_session_find(agent_t *agent, const char *session_id);

/** 追加 user 消息，标记 turn_boundary */
int agent_session_add_user(agent_t *agent,
                           agent_session_t *session,
                           const char *content);

/** 追加 assistant final 消息（无 tool_calls），标记 turn 完成 */
int agent_session_add_assistant(agent_t *agent,
                                agent_session_t *session,
                                const char *content);

/** 追加 assistant tool_calls 消息 */
int agent_session_add_assistant_tool_calls(agent_t *agent,
                                           agent_session_t *session,
                                           const agent_tool_call_t *calls,
                                           size_t call_count);

/** 追加 tool result 消息 */
int agent_session_add_tool(agent_t *agent,
                           agent_session_t *session,
                           const char *tool_call_id,
                           const char *content);

/** 淘汰最早的完整 turn。无完整 turn 时返回 AGENT_ERROR_LIMIT */
int agent_session_evict_oldest_turn(agent_t *agent, agent_session_t *session);

/** 删除尾部未完成 turn。没有未完成 turn 时返回 AGENT_OK */
int agent_session_drop_unfinished_tail(agent_t *agent, agent_session_t *session);

/** 清除 session 所有消息 */
int agent_session_clear_internal(agent_t *agent, agent_session_t *session);

/** 清除 agent 所有 session */
int agent_session_clear_all(agent_t *agent);

/** 构建 provider 可消费的 OpenAI-compatible messages JSON array */
int agent_session_build_model_messages(agent_t *agent,
                                       agent_session_t *session,
                                       char *buffer,
                                       size_t buffer_size,
                                       size_t *written);
