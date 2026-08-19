/* SPDX-License-Identifier: Apache-2.0 */
/**
 * OpenAI-compatible non-streaming model provider.
 *
 * This provider targets HTTP APIs that follow the OpenAI Chat Completions
 * request/response shape. It is vendor-neutral: DeepSeek, Qwen compatible
 * mode, OpenRouter, vLLM and other compatible gateways can use it when their
 * wire protocol matches.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cagent/model.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CAGENT_MODEL_OPENAI_HOST_MAX
#define CAGENT_MODEL_OPENAI_HOST_MAX 128u
#endif

#ifndef CAGENT_MODEL_OPENAI_PATH_MAX
#define CAGENT_MODEL_OPENAI_PATH_MAX 128u
#endif

#ifndef CAGENT_MODEL_OPENAI_PORT_MAX
#define CAGENT_MODEL_OPENAI_PORT_MAX 8u
#endif

#ifndef CAGENT_MODEL_OPENAI_KEY_MAX
#define CAGENT_MODEL_OPENAI_KEY_MAX 256u
#endif

#ifndef CAGENT_MODEL_OPENAI_MODEL_MAX
#define CAGENT_MODEL_OPENAI_MODEL_MAX 64u
#endif

#ifndef CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS
#define CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS 4u
#endif

typedef struct {
    const char *host;              /* e.g. "api.deepseek.com" */
    const char *path;              /* e.g. "/v1/chat/completions" */
    const char *port;              /* "443" for HTTPS */
    const char *api_key;           /* bearer token; caller may pass secure-store value */
    const char *model;             /* provider model name */

    uint32_t timeout_ms;           /* 0 = use request timeout */
    uint32_t request_buffer_size;  /* 0 = CAGENT_HTTP_REQUEST_BUFFER_SIZE */
    uint32_t response_buffer_size; /* 0 = CAGENT_HTTP_RESPONSE_BUFFER_SIZE */
} agent_model_openai_config_t;

agent_model_openai_config_t agent_model_openai_config_default(void);

agent_model_t *agent_model_openai_create(const agent_model_openai_config_t *config);

/**
 * 便捷创建 OpenAI-compatible 模型。
 *
 * 只需提供 host/api_key/model 三个必填参数，其余使用默认值：
 *   path = "/v1/chat/completions"
 *   port = "443"
 *   timeout_ms = 30000
 *   request_buffer_size = CAGENT_HTTP_REQUEST_BUFFER_SIZE
 *   response_buffer_size = CAGENT_HTTP_RESPONSE_BUFFER_SIZE
 *
 * 等价于：
 *   agent_model_openai_config_t cfg = agent_model_openai_config_default();
 *   cfg.host = host; cfg.api_key = api_key; cfg.model = model;
 *   return agent_model_openai_create(&cfg);
 */
agent_model_t *agent_model_openai_create_simple(const char *host,
                                                 const char *api_key,
                                                 const char *model);

int agent_model_openai_set_backend(agent_model_t *model,
                                   const char *host,
                                   const char *path,
                                   const char *port);
int agent_model_openai_set_api_key(agent_model_t *model, const char *api_key);
int agent_model_openai_set_model(agent_model_t *model, const char *model_name);

/**
 * 便捷创建 OpenAI-compatible 模型并绑定到 agent（转移所有权）。
 *
 * 等价于：
 *   agent_model_t *m = agent_model_openai_create_simple(host, api_key, model);
 *   agent_set_model_owned(agent, m);
 *
 * 成功返回 AGENT_OK，失败返回 agent_error_t。
 * model 创建失败时不会 attach。
 */
int agent_attach_openai(agent_t *agent,
                        const char *host,
                        const char *api_key,
                        const char *model);

#ifdef __cplusplus
}
#endif
