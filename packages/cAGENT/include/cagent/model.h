/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 模型 provider 抽象。
 *
 * 核心不绑定特定云厂商。每个 provider 实现 agent_model_ops_t，
 * 通过 agent_model_create() 包装后 attach 到 agent。
 *
 * provider 负责：
 *   - 将 agent_model_request_t 序列化为 vendor 格式
 *   - 调用 runtime.http_post 或本地推理
 *   - 将响应解析为 agent_model_response_t
 *
 * 核心负责：
 *   - 构建 context/tools_schema/session messages
 *   - 调度 provider.complete() 和 provider.cancel()
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cagent/runtime.h>
#include <cagent/tools.h>
#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 不透明模型句柄，内部定义在 agent_internal.h */
typedef struct agent_model agent_model_t;

/** 模型请求（由 loop 构建，传给 provider） */
typedef struct {
    const char *context;           /* system message，由 context_builder 生成 */
    const char *tools_json;        /* tools schema JSON，由 tool_schema_build 生成 */
    const char *messages_json;     /* session messages JSON array，不含 system */
    const char *session_id;        /* 当前会话 ID */
    const char *input;             /* 当前用户输入 */
    const char *trace_id;          /* 追踪 ID */
    uint32_t timeout_ms;           /* 本次调用超时 */
    uint32_t max_output_tokens;    /* 响应 token 预算 */
} agent_model_request_t;

/** 模型响应（由 provider 填充） */
typedef struct {
    const char *content;                    /* 最终文本回复，NULL = 有 tool_calls */
    agent_tool_call_t *tool_calls;          /* 工具调用数组，NULL = 最终回复 */
    size_t tool_call_count;                 /* tool_calls 数组长度 */
    int status;                             /* 0 = 成功，负值 = agent_error_t */
} agent_model_response_t;

/** Mock model 脚本步骤类型 */
typedef enum {
    AGENT_MODEL_MOCK_FINAL = 1,
    AGENT_MODEL_MOCK_TOOL_CALL
} agent_model_mock_step_type_t;

/** Mock model 单步响应。字符串由调用者持有，需长于 mock model 生命周期 */
typedef struct {
    agent_model_mock_step_type_t type;
    const char *content;          /* FINAL 使用 */
    const char *tool_call_id;     /* TOOL_CALL 使用，NULL 时使用默认 id */
    const char *tool_name;        /* TOOL_CALL 使用 */
    const char *arguments_json;   /* TOOL_CALL 使用，NULL 时使用 {} */
} agent_model_mock_step_t;

typedef struct {
    const agent_model_mock_step_t *steps;
    size_t step_count;
    int repeat_last;              /* 脚本耗尽后是否重复最后一步 */
} agent_model_mock_config_t;

typedef struct agent_model_mock agent_model_mock_t;

/**
 * 模型 provider 操作集。
 *
 * complete: 必须实现。执行一次模型推理。
 * cancel:   可选。请求取消正在进行的推理。
 * destroy:  可选。释放 provider 资源。
 */
typedef struct {
    int (*complete)(void *provider,
                    agent_runtime_t *runtime,
                    const agent_model_request_t *request,
                    agent_model_response_t *response);
    int (*cancel)(void *provider);
    void (*destroy)(void *provider);
} agent_model_ops_t;

/** 创建模型句柄。ops 和 provider 由调用者管理生命周期 */
agent_model_t *agent_model_create(const agent_model_ops_t *ops, void *provider);
/** 销毁模型句柄，调用 ops.destroy（如存在） */
void agent_model_destroy(agent_model_t *model);
/** 执行一次模型推理。供 core 或上层 router provider 复用 */
int agent_model_complete(agent_model_t *model,
                         agent_runtime_t *runtime,
                         const agent_model_request_t *request,
                         agent_model_response_t *response);
/** 请求取消模型调用。provider 未实现 cancel 时返回 AGENT_ERROR_NOTSUP */
int agent_model_cancel(agent_model_t *model);
/** 将模型 attach 到 agent，替换已有模型 */
int agent_set_model(agent_t *agent, agent_model_t *model);
/**
 * 将模型 attach 到 agent 并转移所有权。
 *
 * agent_destroy() 时自动调用 agent_model_destroy(model)。
 * 调用者不再需要手动销毁 model。
 */
int agent_set_model_owned(agent_t *agent, agent_model_t *model);

/**
 * 获取 agent 当前绑定的模型。
 *
 * 返回 NULL 表示未设置模型。
 * 返回的指针在 agent_destroy 或重新 set_model 后失效。
 */
agent_model_t *agent_get_model(agent_t *agent);

/** 创建脚本式 mock model；mock_out 可用于读取调用次数 */
agent_model_t *agent_model_mock_create(const agent_model_mock_config_t *config,
                                       agent_model_mock_t **mock_out);
/** 读取 mock model complete() 调用次数 */
uint32_t agent_model_mock_call_count(const agent_model_mock_t *mock);

#ifdef __cplusplus
}
#endif
