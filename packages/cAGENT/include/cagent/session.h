/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT Session 公共 API。
 *
 * Session 存储 ReAct 对话历史，必须保持 tool calling 消息链完整：
 *
 *   user(content)
 *   assistant(tool_calls=[...])
 *   tool(tool_call_id=..., content)
 *   assistant(final content)
 *
 * 淘汰策略：
 *   - 按 turn 淘汰（user → assistant(final) 为一个完整 turn）
 *   - 不允许只删 tool message 留下孤立 assistant(tool_calls)
 *   - 尾部未完成 turn 不参与淘汰
 *
 * MVP: bounded in-memory session，后续可选 snapshot/restore。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_SESSION_DEFAULT_ID "default"

/** 消息角色 */
typedef enum {
    AGENT_MESSAGE_ROLE_SYSTEM = 1,
    AGENT_MESSAGE_ROLE_USER,
    AGENT_MESSAGE_ROLE_ASSISTANT,
    AGENT_MESSAGE_ROLE_TOOL
} agent_message_role_t;

/**
 * 单条消息。
 *
 * content:      文本内容（用户输入、助手回复、工具结果）
 * tool_call_id: 仅 AGENT_MESSAGE_ROLE_TOOL 使用，关联 assistant tool_calls 的 id
 * timestamp_ms: 消息时间戳，由 session append 时自动填充
 */
typedef struct {
    agent_message_role_t role;
    const char *content;
    const char *tool_call_id;
    uint64_t timestamp_ms;
} agent_message_t;

/** 清除指定 session 的所有消息；session_id 为 NULL 时使用默认 session */
int agent_session_clear(agent_t *agent, const char *session_id);
/** 清除所有 session 的消息，保留已分配的 session 槽位 */
int agent_session_clear_all(agent_t *agent);
/** 查询当前 session 数量 */
int agent_session_count(agent_t *agent, size_t *count);

#ifdef __cplusplus
}
#endif
