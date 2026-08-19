/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT Policy 公共 API。
 *
 * Policy 让应用层决定工具调用是否允许，与 guard 互补：
 *   - guard: 通用安全检查（disabled、大小、限流）
 *   - policy: 产品特定规则（权限、风险等级、用户确认）
 *
 * 执行链：registry lookup → policy → guard → handler
 *
 * policy 拒绝时返回结构化 tool error，让模型有机会选择替代方案，
 * 不会直接终止整个 ReAct loop。
 *
 * REQUIRE_CONFIRMATION 的后续确认协议（step loop 或回调式确认）
 * 为 MVP 已知缺失，当前等同于 DENY。
 */

#pragma once

#include <cagent/tools.h>
#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 策略动作类型 */
typedef enum {
    AGENT_POLICY_ACTION_TOOL_CALL = 1 /* 工具调用请求 */
} agent_policy_action_t;

/** 策略决策 */
typedef enum {
    AGENT_POLICY_ALLOW = 0,            /* 允许执行 */
    AGENT_POLICY_DENY = 1,             /* 拒绝执行，返回结构化 tool error */
    AGENT_POLICY_REQUIRE_CONFIRM = 2   /* 需要用户确认（MVP 等同 DENY） */
} agent_policy_decision_t;

/** 策略请求，传给 policy callback */
typedef struct {
    agent_policy_action_t action;   /* 动作类型 */
    const agent_tool_call_t *tool_call; /* 模型发起的工具调用 */
    const char *session_id;        /* 当前会话 ID */
    const char *trace_id;          /* 追踪 ID */
} agent_policy_request_t;

/** 策略回调。返回 ALLOW/DENY/REQUIRE_CONFIRM */
typedef agent_policy_decision_t (*agent_policy_cb_t)(const agent_policy_request_t *request,
                                                     void *user_data);

/** 注册策略回调，替换已有回调 */
int agent_set_policy_callback(agent_t *agent, agent_policy_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
