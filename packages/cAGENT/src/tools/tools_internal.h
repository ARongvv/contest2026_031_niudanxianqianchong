/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <agent.h>

typedef struct {
    agent_tool_t def;
    uint32_t call_count;
} agent_tool_entry_t;

agent_tool_entry_t *agent_tool_registry_find(agent_t *agent, const char *name);
const agent_tool_entry_t *agent_tool_registry_find_const(const agent_t *agent,
                                                         const char *name);

bool agent_tool_entry_is_enabled(const agent_tool_entry_t *entry);
bool agent_tool_entry_is_llm_visible(const agent_tool_entry_t *entry);

void agent_tool_schema_mark_dirty(agent_t *agent);
