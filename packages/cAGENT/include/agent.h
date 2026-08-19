/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 聚合公共头文件。
 *
 * 应用代码优先使用此单一 include：
 *   #include <agent.h>
 *
 * 不要 include src/ 下的任何内部头文件。
 */

#pragma once

#include <cagent/types.h>
#include <cagent/runtime.h>
#include <cagent/config.h>
#include <cagent/event.h>
#include <cagent/policy.h>
#include <cagent/model.h>
#include <cagent/model_openai.h>
#include <cagent/model_openai_compat.h>
#include <cagent/tools.h>
#include <cagent/skill.h>
#include <cagent/context.h>
#include <cagent/session.h>
#include <cagent/memory.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Agent 生命周期 ── */

/** 创建 agent。config 为 NULL 时使用默认配置。失败返回 NULL */
agent_t *agent_create(const agent_config_t *config);

/**
 * 便捷创建 agent。
 *
 * 等价于：
 *   agent_config_t cfg = agent_config_default();
 *   cfg.name = name;
 *   cfg.system_prompt = system_prompt;
 *   return agent_create(&cfg);
 */
agent_t *agent_create_simple(const char *name, const char *system_prompt);

/** 销毁 agent，释放所有内部资源。agent 为 NULL 时安全返回 */
void agent_destroy(agent_t *agent);

/**
 * 执行一次同步 ReAct 推理。
 *
 * request->limits 可临时覆盖 agent limits，请求结束后自动恢复。
 * 同一 agent 不可重入，busy 时返回 AGENT_ERROR_BUSY。
 * 可通过 agent_cancel() 从外部请求取消。
 */
int agent_run(agent_t *agent,
              const agent_request_t *request,
              agent_response_t *response);

/**
 * 便捷运行 agent。
 *
 * 等价于：
 *   agent_request_t  req  = { .input = input };
 *   agent_response_t resp = { .output = output, .output_size = output_size };
 *   return agent_run(agent, &req, &resp);
 *
 * 使用默认 session_id，不覆盖 limits。
 */
int agent_run_simple(agent_t *agent,
                     const char *input,
                     char *output,
                     size_t output_size);

/** 协作式取消。设置 cancel flag + 调用 model.cancel（如支持） */
int agent_cancel(agent_t *agent);

/**
 * 重置 agent 状态（stats、cancel flag、deadline）。
 * 不清除已注册的 tools/skills/model/runtime。
 * agent 忙时返回 AGENT_ERROR_BUSY。
 */
int agent_reset(agent_t *agent);

/* ── 运行时配置 ── */

/** 持久修改 agent limits，影响后续所有 agent_run 调用 */
int agent_set_limits(agent_t *agent, const agent_limits_t *limits);
/** 读取 agent 累计统计 */
int agent_get_stats(agent_t *agent, agent_stats_t *stats);

#ifdef __cplusplus
}
#endif
