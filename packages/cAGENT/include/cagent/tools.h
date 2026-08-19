/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 工具公共 API。
 *
 * 工具是 Agent 唯一的可执行扩展点。应用层注册工具实现，核心负责：
 *   - 注册表管理（注册/注销/启停）
 *   - schema 生成（供模型 tools 参数）
 *   - 受限执行（guard 检查 + policy 决策）
 *   - 结果归一化（JSON 契约）
 *
 * 核心不内置业务工具，示例工具放在 examples/ 或 integrations/。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 工具标志位 ── */

#define AGENT_TOOL_FLAG_LLM_VISIBLE      (1u << 0) /* 暴露给模型（默认应设置） */
#define AGENT_TOOL_FLAG_READ_ONLY        (1u << 1) /* 只读工具，无副作用 */
#define AGENT_TOOL_FLAG_SIDE_EFFECT      (1u << 2) /* 有副作用，可能触发限流 */
#define AGENT_TOOL_FLAG_REQUIRES_CONFIRM (1u << 3) /* 需用户确认（OTA/重启等） */
#define AGENT_TOOL_FLAG_DISABLED         (1u << 4) /* 已禁用，不参与 schema 和执行 */
#define AGENT_TOOL_FLAG_PARALLEL_SAFE    (1u << 5) /* 未来并行执行时标记安全 */

/** 模型发起的工具调用请求 */
typedef struct {
    const char *id;                /* call id，由模型生成，用于匹配 result */
    const char *name;              /* 工具名，对应 agent_tool_t.name */
    const char *arguments_json;    /* 调用参数 JSON 字符串 */
} agent_tool_call_t;

/**
 * 工具执行结果。
 *
 * status = AGENT_OK 时 content_json 为成功结果，
 * status 为负值时 content_json 为错误描述。
 */
typedef struct {
    int status;                    /* AGENT_OK 或 agent_error_t */
    const char *content_json;      /* JSON 结果，MVP 必须为 UTF-8 JSON */
    const char *error_message;     /* 人类可读错误信息，NULL = 无错误 */
} agent_tool_result_t;

/** 工具执行回调。返回 AGENT_OK 成功，负值为 agent_error_t */
typedef int (*agent_tool_fn)(const agent_tool_call_t *call,
                             agent_tool_result_t *result,
                             void *user_data);

/**
 * 工具定义（一体式）。
 *
 * 所有权：所有 const char * 字段由调用者持有，必须在工具注册期间有效。
 * group_id/category_id 均由应用定义：group_id = 0 表示默认组，
 * category_id = 0 表示未分类。core 只存储这些值，不解释其业务语义。
 * timeout_ms = 0 表示使用 limits.per_tool_timeout_ms。
 */
typedef struct {
    const char *name;              /* 唯一标识，不可为 NULL */
    uint16_t group_id;             /* 应用定义的生命周期/来源分组，0 = 默认组 */
    uint16_t category_id;          /* 应用定义的展示分类，0 = 未分类 */

    const char *description;       /* 工具描述，暴露给模型 */
    const char *input_schema_json; /* JSON Schema，描述参数结构 */

    agent_tool_fn execute;         /* 工具执行函数，不可为 NULL */
    void *user_data;               /* 传递给 execute 的上下文 */

    uint32_t flags;                /* AGENT_TOOL_FLAG_* 组合 */
    uint32_t timeout_ms;           /* 单工具超时，0 = 使用全局默认 */
} agent_tool_t;

/** 工具注册表的只读视图。字符串仅在 enumerate 回调期间有效。 */
typedef struct {
    const char *name;
    const char *description;
    uint16_t group_id;
    uint16_t category_id;
    uint32_t flags;
    uint32_t timeout_ms;
    int enabled;
    int llm_visible;
} agent_tool_info_t;

/**
 * 只读遍历已注册工具。回调不得修改 agent 或保留 info 内的字符串指针。
 * 调用方负责与注册/注销操作串行化。
 */
typedef int (*agent_tool_enumerate_fn)(const agent_tool_info_t *info,
                                       void *user_data);

/** 注册工具到 agent。name 重复时返回 AGENT_ERROR_INVALID */
int agent_register_tool(agent_t *agent, const agent_tool_t *tool);

/**
 * 便捷注册工具（参数式）。
 *
 * 等价于：
 *   agent_tool_t t = {0};
 *   t.name = name;
 *   t.group_id = 0;
 *   t.category_id = 0;
 *   t.description = description;
 *   t.input_schema_json = input_schema_json;
 *   t.execute = execute;
 *   t.user_data = user_data;
 *   t.flags = flags;
 *   return agent_register_tool(agent, &t);
 */
int agent_register_tool_simple(agent_t *agent,
                                const char *name,
                                const char *description,
                                const char *input_schema_json,
                                agent_tool_fn execute,
                                void *user_data,
                                uint32_t flags);
/** 注销工具。正在执行的工具不受影响 */
int agent_unregister_tool(agent_t *agent, const char *name);
/** 启用/禁用工具。禁用后不参与 schema 生成和执行 */
int agent_tool_set_enabled(agent_t *agent, const char *name, int enabled);
/** 查询工具是否启用。enabled 输出 1=启用，0=禁用 */
int agent_tool_is_enabled(const agent_t *agent, const char *name, int *enabled);
/** 遍历工具注册表的只读视图。 */
int agent_tool_enumerate(const agent_t *agent,
                         agent_tool_enumerate_fn callback,
                         void *user_data);

#ifdef __cplusplus
}
#endif
