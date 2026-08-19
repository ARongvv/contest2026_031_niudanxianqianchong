/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT Memory 公共 API。
 *
 * 提供 session snapshot/restore 扩展点，用于长期记忆和持久化。
 * MVP 阶段可保持空实现，核心依赖 bounded in-memory session。
 *
 * 后续可扩展：
 *   - flash-backed snapshot（openvela/STM32）
 *   - 向量化长期记忆
 *   - 摘要记忆生成
 */

#pragma once

#include <stddef.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 内存快照。
 *
 * data: 快照数据指针，由 snapshot 实现 allocate
 * size: 快照数据大小
 * 释放必须通过 agent_memory_snapshot_free()
 */
typedef struct {
    void *data;
    size_t size;
} agent_memory_snapshot_t;

/** 创建 agent 当前状态的快照。MVP 返回 AGENT_ERROR_NOTSUP */
int agent_memory_snapshot(agent_t *agent, agent_memory_snapshot_t *snapshot);
/** 从快照恢复 agent 状态。MVP 返回 AGENT_ERROR_NOTSUP */
int agent_memory_restore(agent_t *agent, const agent_memory_snapshot_t *snapshot);
/** 释放快照数据 */
void agent_memory_snapshot_free(agent_t *agent, agent_memory_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
