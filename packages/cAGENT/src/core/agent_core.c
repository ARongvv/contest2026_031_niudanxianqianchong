/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Agent 生命周期和同步推理入口。
 *
 * agent_run() 是"外壳"：参数校验、busy 防重入、deadline 计算、
 * event emit、stats 更新。
 * 实际推理循环在 agent_loop_run() 中执行。
 */

#include "agent_internal.h"

#include <string.h>

#include "../runtime/runtime.h"

/** 构造默认 limits */
static agent_limits_t default_limits(void)
{
    agent_limits_t limits = AGENT_LIMITS_DEFAULT;
    return limits;
}

/** 生成标准默认配置 */
agent_config_t agent_config_default(void)
{
    agent_config_t config;

    memset(&config, 0, sizeof(config));
    config.name = "cagent";
    config.system_prompt = "You are a helpful embedded agent.";
    config.limits = default_limits();
    agent_runtime_fill_platform(&config.runtime);
    agent_runtime_fill_defaults(&config.runtime);

    return config;
}

/** 生成精简配置，适用于小 MCU / bare-metal */
agent_config_t agent_config_tiny(void)
{
    agent_config_t config = agent_config_default();

    config.limits.max_steps = 4u;
    config.limits.timeout_ms = 10000u;
    config.limits.per_model_timeout_ms = 5000u;
    config.limits.per_tool_timeout_ms = 1000u;
    config.limits.max_tool_calls = 2u;
    config.limits.max_output_tokens = 128u;

    return config;
}

/**
 * 创建 agent。
 *
 * config 为 NULL 时使用默认配置。
 * runtime 中 NULL 回调先由平台 runtime 补齐，再由 fill_defaults 兜底。
 * 已设置的应用回调不会被平台 runtime 覆盖。
 * 如果 config.runtime.mutex_create 已设置，会自动创建互斥量。
 */
agent_t *agent_create(const agent_config_t *config)
{
    agent_config_t local_config;
    agent_runtime_t runtime;
    agent_t *agent;

    if (config) {
        local_config = *config;
        agent_runtime_fill_platform(&local_config.runtime);
        agent_runtime_fill_defaults(&local_config.runtime);
    } else {
        local_config = agent_config_default();
    }

    runtime = local_config.runtime;
    agent = (agent_t *)agent_runtime_malloc(&runtime, sizeof(*agent));
    if (!agent) {
        return NULL;
    }

    memset(agent, 0, sizeof(*agent));
    agent->config = local_config;
    agent->runtime = runtime;
    agent->limits = local_config.limits.max_steps ? local_config.limits : default_limits();
    agent->tool_schema_dirty = true;

    if (agent->runtime.mutex_create) {
        agent->mutex = agent->runtime.mutex_create(agent->runtime.user_data);
    }

    return agent;
}

agent_t *agent_create_simple(const char *name, const char *system_prompt)
{
    agent_config_t cfg;

    cfg = agent_config_default();
    if (name) {
        cfg.name = name;
    }
    if (system_prompt) {
        cfg.system_prompt = system_prompt;
    }

    return agent_create(&cfg);
}

/**
 * 销毁 agent。
 *
 * 释放互斥量和 agent 自身。
 * 如果 model 通过 agent_set_model_owned() 绑定，同时销毁 model。
 * 否则 model 生命周期由应用层管理。
 */
void agent_destroy(agent_t *agent)
{
    agent_runtime_t runtime;

    if (!agent) {
        return;
    }

    if (agent->model_owned && agent->model) {
        agent_model_destroy(agent->model);
        agent->model = NULL;
        agent->model_owned = false;
    }

    runtime = agent->runtime;
    if (agent->mutex && runtime.mutex_destroy) {
        runtime.mutex_destroy(agent->mutex, runtime.user_data);
    }
    agent_runtime_free(&runtime, agent);
}

/**
 * 同步推理入口。
 *
 * 流程：
 *   1. 参数校验
 *   2. mutex lock → busy 检查 → 设置 busy
 *   3. 保存/覆盖 limits，计算 deadline
 *   4. emit RUN_START
 *   5. agent_loop_run() 执行 ReAct 循环
 *   6. 更新 stats，emit RUN_DONE
 *   7. 恢复 limits，清除 busy，mutex unlock
 */
int agent_run(agent_t *agent,
              const agent_request_t *request,
              agent_response_t *response)
{
    agent_limits_t saved_limits;
    uint64_t start_ms;
    uint64_t end_ms;
    int ret;

    if (!agent || !request || !request->input || !response) {
        return AGENT_ERROR_INVALID;
    }

    /* 尝试加锁，失败说明另一线程持有 */
    if (agent_runtime_mutex_lock(&agent->runtime, agent->mutex, 0u) != AGENT_OK) {
        return AGENT_ERROR_BUSY;
    }

    if (agent->busy) {
        agent_runtime_mutex_unlock(&agent->runtime, agent->mutex);
        return AGENT_ERROR_BUSY;
    }

    agent->busy = true;

    /* 保存当前 limits，允许 request 临时覆盖 */
    saved_limits = agent->limits;
    if (request->limits) {
        agent->limits = *request->limits;
    }

    response->status = AGENT_ERROR;
    response->stats = agent->stats;
    if (response->output && response->output_size > 0u) {
        response->output[0] = '\0';
    }

    /* 重置取消标志，计算本次请求 deadline */
    agent->cancel_requested = false;
    agent->current_run_tool_calls = 0u;
    start_ms = agent_runtime_now_ms(&agent->runtime);
    agent->run_start_ms = start_ms;
    agent->run_deadline_ms = agent->limits.timeout_ms ? start_ms + agent->limits.timeout_ms : 0u;
    agent->stats.runs++;

    agent_event_emit(agent, AGENT_EVENT_RUN_START, request, 0u, AGENT_OK, "start", NULL, NULL);
    ret = agent_loop_run(agent, request, response);

    end_ms = agent_runtime_now_ms(&agent->runtime);
    agent->stats.last_run_elapsed_ms = end_ms >= start_ms ? end_ms - start_ms : 0u;

    /* 根据结果更新统计和 emit 事件 */
    if (ret == AGENT_OK) {
        agent->stats.completed_runs++;
    } else {
        agent->stats.failed_runs++;
        if (ret == AGENT_ERROR_CANCELLED) {
            agent->stats.cancelled_runs++;
            agent_event_emit(agent, AGENT_EVENT_CANCELLED, request, 0u, ret, "cancelled", NULL, NULL);
        } else if (ret == AGENT_ERROR_TIMEOUT) {
            agent->stats.timeout_runs++;
            agent_event_emit(agent, AGENT_EVENT_TIMEOUT, request, 0u, ret, "timeout", NULL, NULL);
        } else {
            agent_event_emit(agent, AGENT_EVENT_ERROR, request, 0u, ret, "agent_run failed", NULL, NULL);
        }
    }

    response->status = ret;
    response->stats = agent->stats;
    agent_event_emit(agent, AGENT_EVENT_RUN_DONE, request, 0u, ret, "done", NULL, NULL);

    /* 恢复 limits，清理运行状态 */
    agent->limits = saved_limits;
    agent->run_deadline_ms = 0u;
    agent->busy = false;
    agent_runtime_mutex_unlock(&agent->runtime, agent->mutex);

    return ret;
}

/**
 * 协作式取消。
 *
 * 通过 critical section 安全设置 cancel_requested（ISR 安全）。
 * 如果 model 支持 cancel，同时通知 provider。
 * 实际取消发生在 loop 的下一个检查点。
 */
int agent_cancel(agent_t *agent)
{
    uint32_t state;

    if (!agent) {
        return AGENT_ERROR_INVALID;
    }

    state = agent_runtime_enter_critical(&agent->runtime);
    agent->cancel_requested = true;
    agent_runtime_exit_critical(&agent->runtime, state);

    agent_model_cancel(agent->model);

    return AGENT_OK;
}

/**
 * 重置 agent 状态。
 *
 * 清除 stats、cancel flag、deadline、sessions。
 * 不清除已注册的 tools/skills/model/runtime。
 */
int agent_reset(agent_t *agent)
{
    if (!agent) {
        return AGENT_ERROR_INVALID;
    }
    if (agent->busy) {
        return AGENT_ERROR_BUSY;
    }

    memset(&agent->stats, 0, sizeof(agent->stats));
    agent->cancel_requested = false;
    agent->run_start_ms = 0u;
    agent->run_deadline_ms = 0u;

    /* 清理所有 session 消息，但保留 session 槽位 */
    agent_session_clear_all(agent);

    return AGENT_OK;
}

/** 将模型 attach 到 agent，替换已有模型。model 生命周期由调用者管理 */
int agent_set_model(agent_t *agent, agent_model_t *model)
{
    if (!agent) {
        return AGENT_ERROR_INVALID;
    }

    agent->model = model;
    agent->model_owned = false;
    return AGENT_OK;
}

/**
 * 将模型 attach 到 agent 并转移所有权。
 *
 * agent_destroy() 时自动调用 agent_model_destroy(model)。
 * 调用者不再需要手动销毁 model。
 */
int agent_set_model_owned(agent_t *agent, agent_model_t *model)
{
    if (!agent) {
        return AGENT_ERROR_INVALID;
    }

    agent->model = model;
    agent->model_owned = true;
    return AGENT_OK;
}

agent_model_t *agent_get_model(agent_t *agent)
{
    if (!agent) {
        return NULL;
    }

    return agent->model;
}

/** 注册事件回调，替换已有回调 */
int agent_set_event_callback(agent_t *agent, agent_event_cb_t cb, void *user_data)
{
    if (!agent) {
        return AGENT_ERROR_INVALID;
    }

    agent->event_cb = cb;
    agent->event_user_data = user_data;
    return AGENT_OK;
}

/** 注册策略回调，替换已有回调 */
int agent_set_policy_callback(agent_t *agent, agent_policy_cb_t cb, void *user_data)
{
    if (!agent) {
        return AGENT_ERROR_INVALID;
    }

    agent->policy_cb = cb;
    agent->policy_user_data = user_data;
    return AGENT_OK;
}

/**
 * 持久修改 agent limits。
 *
 * 同时更新 config.limits 和 agent->limits。
 * 不影响正在执行的 agent_run（它使用 saved_limits 恢复）。
 */
int agent_set_limits(agent_t *agent, const agent_limits_t *limits)
{
    if (!agent || !limits) {
        return AGENT_ERROR_INVALID;
    }

    agent->limits = *limits;
    agent->config.limits = *limits;
    return AGENT_OK;
}

/** 读取 agent 累计统计快照 */
int agent_get_stats(agent_t *agent, agent_stats_t *stats)
{
    if (!agent || !stats) {
        return AGENT_ERROR_INVALID;
    }

    *stats = agent->stats;
    return AGENT_OK;
}

int agent_run_simple(agent_t *agent,
                     const char *input,
                     char *output,
                     size_t output_size)
{
    agent_request_t request;
    agent_response_t response;

    if (!input || !output || output_size == 0u) {
        return AGENT_ERROR_INVALID;
    }

    memset(&request, 0, sizeof(request));
    request.input = input;

    memset(&response, 0, sizeof(response));
    response.output = output;
    response.output_size = output_size;

    return agent_run(agent, &request, &response);
}
