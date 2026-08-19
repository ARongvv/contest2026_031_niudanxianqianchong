/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT Skill 公共 API。
 *
 * Skill 是上下文资源，不是可执行代码。它描述 Agent 在某类任务中
 * 应该如何使用工具，由 skill context provider 注入到 system message。
 *
 * 执行动作仍然通过 tool 完成，skill 只影响模型的行为策略。
 *
 * Skill 加载、Markdown 解析、热更新由上层 loader 实现，
 * 核心只接收已解析好的 agent_skill_t。
 */

#pragma once

#include <stdint.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Skill 标志位 ── */

#define AGENT_SKILL_FLAG_ENABLED      (1u << 0) /* 已启用，参与 context 注入 */
#define AGENT_SKILL_FLAG_LLM_VISIBLE  (1u << 1) /* 描述暴露给模型（用于 skill 列表展示） */
#define AGENT_SKILL_FLAG_SUMMARY_ONLY (1u << 2) /* 仅注入 name+description+读取提示，不展开全文 */

/**
 * Skill 定义。
 *
 * 所有权：所有 const char * 字段由调用者持有，核心不拷贝。
 * context_text 是注入到 system message 的内容主体。
 */
typedef struct {
    const char *name;              /* 唯一标识，不可为 NULL */
    uint16_t group_id;             /* 应用层定义的分组 ID，0 = 默认组 */
    const char *description;       /* 简短描述，可选 */
    const char *context_text;      /* 注入到 system message 的文本 */
    uint32_t priority;             /* context 拼接优先级，数值越大越靠前 */
    uint32_t flags;                /* AGENT_SKILL_FLAG_* 组合 */
    void *user_data;               /* 应用层上下文，核心不使用 */
} agent_skill_t;

/** 注册 skill。name 重复时返回 AGENT_ERROR_INVALID */
int agent_register_skill(agent_t *agent, const agent_skill_t *skill);

/**
 * 便捷注册 Skill（参数式）。
 *
 * 等价于：
 *   agent_skill_t s = {0};
 *   s.name = name;
 *   s.description = description;
 *   s.context_text = context_text;
 *   s.priority = priority;
 *   s.flags = flags;
 *   return agent_register_skill(agent, &s);
 *
 * 不暴露的字段：
 *   - group_id: 设为 0（默认组）。
 *   - user_data: 设为 NULL。
 *   若需要这些字段，请直接使用 agent_register_skill() 并填充完整结构体。
 */
int agent_register_skill_simple(agent_t *agent,
                                const char *name,
                                const char *description,
                                const char *context_text,
                                uint32_t priority,
                                uint32_t flags);

/** 注销 skill */
int agent_unregister_skill(agent_t *agent, const char *name);

#ifdef __cplusplus
}
#endif
