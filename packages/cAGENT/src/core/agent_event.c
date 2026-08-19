/* SPDX-License-Identifier: Apache-2.0 */
/**
 * 统一事件派发。
 *
 * 所有内部模块通过此函数 emit 事件，不直接触碰 event_cb。
 * 自动填充 timestamp_ms（runtime.now_ms）、trace_id、session_id。
 * callback 为 NULL 时直接返回，无开销。
 */

#include "agent_internal.h"

#include "../runtime/runtime.h"

void agent_event_emit(agent_t *agent,
                      agent_event_type_t type,
                      const agent_request_t *request,
                      uint32_t iteration,
                      int error_code,
                      const char *message,
                      const char *tool_name,
                      const char *tool_call_id)
{
    agent_event_t event;

    if (!agent || !agent->event_cb) {
        return;
    }

    event.type = type;
    event.timestamp_ms = agent_runtime_now_ms(&agent->runtime);
    event.trace_id = request ? request->trace_id : NULL;
    event.session_id = request ? request->session_id : NULL;
    event.iteration = iteration;
    event.error_code = error_code;
    event.message = message;
    event.tool_name = tool_name;
    event.tool_call_id = tool_call_id;

    agent->event_cb(&event, agent->event_user_data);
}
