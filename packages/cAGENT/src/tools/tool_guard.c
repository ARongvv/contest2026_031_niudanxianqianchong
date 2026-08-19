/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Tool guard and execution.
 *
 * P1 keeps execution sequential. Timeouts are detected cooperatively after the
 * handler returns; platform-specific preemption is deliberately outside core.
 */

#include "tools_internal.h"

#include "../core/agent_internal.h"
#include "../runtime/runtime.h"
#include "../types_internal.h"

#include <stdio.h>
#include <string.h>

static const char *default_ok_json = "{\"ok\":true}";

static const char *tool_error_code(int status)
{
    switch (status) {
    case AGENT_ERROR_INVALID:
        return "invalid";
    case AGENT_ERROR_LIMIT:
        return "limit";
    case AGENT_ERROR_TIMEOUT:
        return "timeout";
    case AGENT_ERROR_CANCELLED:
        return "cancelled";
    case AGENT_ERROR_NOTFOUND:
        return "tool_not_found";
    case AGENT_ERROR_POLICY_DENIED:
        return "policy_denied";
    case AGENT_ERROR_NOTSUP:
        return "not_supported";
    default:
        return "tool_error";
    }
}

static void json_escape_message(char *dst,
                                size_t dst_size,
                                const char *src)
{
    size_t out = 0u;

    if (!dst || dst_size == 0u) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (*src && out + 1u < dst_size) {
        unsigned char c = (unsigned char)*src++;
        if ((c == '"' || c == '\\') && out + 2u < dst_size) {
            dst[out++] = '\\';
            dst[out++] = (char)c;
        } else if (c == '\n' && out + 2u < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 'n';
        } else if (c == '\r' && out + 2u < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 'r';
        } else if (c == '\t' && out + 2u < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 't';
        } else if (c >= 0x20u) {
            dst[out++] = (char)c;
        }
    }

    dst[out] = '\0';
}

static int set_tool_error(agent_t *agent,
                          agent_tool_result_t *result,
                          int status,
                          const char *message)
{
    char escaped[128];

    if (!result) {
        return status;
    }

    if (!agent || sizeof(agent->tool_result_buffer) == 0u) {
        result->status = status;
        result->content_json = "{\"ok\":false,\"error\":{\"code\":\"tool_error\"}}";
        result->error_message = message;
        return status;
    }

    json_escape_message(escaped, sizeof(escaped), message ? message : tool_error_code(status));
    snprintf(agent->tool_result_buffer,
             sizeof(agent->tool_result_buffer),
             "{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
             tool_error_code(status),
             escaped);

    result->status = status;
    result->content_json = agent->tool_result_buffer;
    result->error_message = message;
    return status;
}

static int check_policy(agent_t *agent,
                        const agent_tool_call_t *call,
                        agent_tool_result_t *result)
{
    agent_policy_request_t policy_request;
    agent_policy_decision_t decision;

    if (!agent->policy_cb) {
        return AGENT_OK;
    }

    memset(&policy_request, 0, sizeof(policy_request));
    policy_request.action = AGENT_POLICY_ACTION_TOOL_CALL;
    policy_request.tool_call = call;

    /* 调用应用层注册的策略回调，对工具及参数进行产品级判定。 */
    decision = agent->policy_cb(&policy_request, agent->policy_user_data);
    if (decision == AGENT_POLICY_ALLOW) {
        return AGENT_OK;
    }

    return set_tool_error(agent,
                          result,
                          AGENT_ERROR_POLICY_DENIED,
                          decision == AGENT_POLICY_REQUIRE_CONFIRM
                              ? "tool requires confirmation"
                              : "tool denied by policy");
}

int agent_tool_execute(agent_t *agent,
                       const agent_tool_call_t *call,
                       agent_tool_result_t *result)
{
    agent_tool_entry_t *entry;
    agent_tool_result_t local_result;
    uint64_t start_ms;
    uint64_t end_ms;
    uint32_t effective_timeout_ms;
    int ret;

    if (!agent || !call || !call->name) {
        return set_tool_error(agent, result, AGENT_ERROR_INVALID, "invalid tool call");
    }

    if (!result) {
        result = &local_result;
    }
    memset(result, 0, sizeof(*result));

    entry = agent_tool_registry_find(agent, call->name);
    if (!entry) {
        return set_tool_error(agent, result, AGENT_ERROR_NOTFOUND, "tool not found");
    }
    if (!agent_tool_entry_is_enabled(entry)) {
        return set_tool_error(agent, result, AGENT_ERROR_NOTFOUND, "tool is disabled");
    }
    if (!entry->def.execute) {
        return set_tool_error(agent, result, AGENT_ERROR_INVALID, "tool has no execute callback");
    }
    if (call->arguments_json &&
        strlen(call->arguments_json) > (size_t)CAGENT_TOOL_ARGS_MAX_SIZE) {
        return set_tool_error(agent, result, AGENT_ERROR_LIMIT, "tool arguments exceed limit");
    }
    if (agent->limits.max_tool_calls > 0u &&
        agent->current_run_tool_calls >= agent->limits.max_tool_calls) {
        return set_tool_error(agent, result, AGENT_ERROR_LIMIT, "tool call limit exceeded");
    }

    ret = check_policy(agent, call, result);
    if (ret != AGENT_OK) {
        return ret;
    }

    agent->current_run_tool_calls++;
    entry->call_count++;
    agent->stats.tool_calls++;

    effective_timeout_ms = entry->def.timeout_ms ? entry->def.timeout_ms
                                                 : agent->limits.per_tool_timeout_ms;
    start_ms = agent_runtime_now_ms(&agent->runtime);
    ret = entry->def.execute(call, result, entry->def.user_data);
    end_ms = agent_runtime_now_ms(&agent->runtime);

    if (effective_timeout_ms > 0u &&
        end_ms >= start_ms &&
        end_ms - start_ms > effective_timeout_ms) {
        return set_tool_error(agent, result, AGENT_ERROR_TIMEOUT, "tool timeout");
    }

    if (ret != AGENT_OK || result->status != AGENT_OK) {
        int status = ret != AGENT_OK ? ret : result->status;
        const char *message = result->error_message ? result->error_message
                                                    : "tool execution failed";
        return set_tool_error(agent, result, status, message);
    }

    result->status = AGENT_OK;
    if (!result->content_json || result->content_json[0] == '\0') {
        result->content_json = default_ok_json;
    }

    if (strlen(result->content_json) > (size_t)CAGENT_TOOL_OUTPUT_MAX_SIZE) {
        return set_tool_error(agent, result, AGENT_ERROR_LIMIT, "tool output exceeds limit");
    }

    return AGENT_OK;
}
