/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Memory 快照/恢复 — 长期记忆扩展点。
 *
 * MVP 阶段保持空实现，核心依赖 bounded in-memory session。
 * 后续可扩展：
 *   - flash-backed snapshot（openvela/STM32）
 *   - 向量化长期记忆
 *   - 摘要记忆生成
 */

#include "../core/agent_internal.h"
#include "../types_internal.h"

/** 创建 agent 当前状态快照。MVP 返回 NOTSUP */
int agent_memory_snapshot(agent_t *agent, agent_memory_snapshot_t *snapshot)
{
    CAGENT_UNUSED(agent);

    if (!snapshot) {
        return AGENT_ERROR_INVALID;
    }

    snapshot->data = NULL;
    snapshot->size = 0u;
    return AGENT_ERROR_NOTSUP;
}

/** 从快照恢复 agent 状态。MVP 返回 NOTSUP */
int agent_memory_restore(agent_t *agent, const agent_memory_snapshot_t *snapshot)
{
    CAGENT_UNUSED(agent);
    CAGENT_UNUSED(snapshot);
    return AGENT_ERROR_NOTSUP;
}

/** 释放快照数据。当前无操作（snapshot 未实现分配） */
void agent_memory_snapshot_free(agent_t *agent, agent_memory_snapshot_t *snapshot)
{
    CAGENT_UNUSED(agent);

    if (snapshot) {
        snapshot->data = NULL;
        snapshot->size = 0u;
    }
}
