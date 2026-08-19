/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT Event 公共 API。
 *
 * Event 使 Agent 内部状态变化可观测，用于调试、UI、审计、watchdog。
 *
 * 回调保证在 agent_run 所在线程同步执行，不会跨线程调用。
 * 回调应快速返回，重操作应入队到应用层自己的 worker。
 *
 * event callback 是纯通知，不能修改 loop 行为。
 * 干预工具执行应通过 policy callback。
 */

#pragma once

#include <stdint.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 事件类型，覆盖 agent_run 的完整生命周期 */
typedef enum {
    AGENT_EVENT_RUN_START = 1,    /* agent_run 入口，此时 busy=true */
    AGENT_EVENT_RUN_DONE,        /* agent_run 出口，此时 busy 即将清零 */
    AGENT_EVENT_ITERATION_START, /* ReAct 迭代开始 */
    AGENT_EVENT_MODEL_REQUEST,   /* 即将调用 model.complete() */
    AGENT_EVENT_MODEL_RESPONSE,  /* model.complete() 返回 */
    AGENT_EVENT_TOOL_CALL,       /* 即将执行工具 */
    AGENT_EVENT_TOOL_RESULT,     /* 工具执行完成 */
    AGENT_EVENT_ERROR,           /* 致命错误（非 tool 级错误） */
    AGENT_EVENT_CANCELLED,       /* 请求被取消 */
    AGENT_EVENT_TIMEOUT          /* 请求超时 */
} agent_event_type_t;

/**
 * 事件负载。
 *
 * timestamp_ms: 由 agent_event_emit 自动填充
 * trace_id / session_id: 从当前 request 透传
 * message: 可选描述文本，生命周期由 emit 内部管理
 * tool_name / tool_call_id: 仅 TOOL_CALL / TOOL_RESULT 事件使用
 */
typedef struct {
    agent_event_type_t type;
    uint64_t timestamp_ms;        /* runtime.now_ms() 自动填充 */
    const char *trace_id;         /* 请求追踪 ID，可为 NULL */
    const char *session_id;       /* 会话 ID，可为 NULL */
    uint32_t iteration;           /* 当前迭代序号 */
    int error_code;               /* agent_error_t，0 = 无错误 */
    const char *message;          /* 可选描述，如 "timeout"、"cancelled" */
    const char *tool_name;        /* 工具名，仅 TOOL_CALL/TOOL_RESULT */
    const char *tool_call_id;     /* 工具调用 ID，仅 TOOL_CALL/TOOL_RESULT */
} agent_event_t;

/** 事件回调。同步执行，必须快速返回 */
typedef void (*agent_event_cb_t)(const agent_event_t *event, void *user_data);

/** 注册事件回调，替换已有回调 */
int agent_set_event_callback(agent_t *agent, agent_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
