/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Tool registry.
 *
 * Stores shallow copies of developer-provided agent_tool_t definitions.
 * String pointers and user_data remain owned by the caller and must stay valid
 * while the tool is registered.
 */

#include "tools_internal.h"

#include "../core/agent_internal.h"

#include <string.h>

bool agent_tool_entry_is_enabled(const agent_tool_entry_t *entry)
{
    return entry && ((entry->def.flags & AGENT_TOOL_FLAG_DISABLED) == 0u);
}

bool agent_tool_entry_is_llm_visible(const agent_tool_entry_t *entry)
{
    return entry && ((entry->def.flags & AGENT_TOOL_FLAG_LLM_VISIBLE) != 0u);
}

agent_tool_entry_t *agent_tool_registry_find(agent_t *agent, const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return NULL;
    }

    for (i = 0u; i < agent->tool_count; i++) {
        if (agent->tools[i].def.name &&
            strcmp(agent->tools[i].def.name, name) == 0) {
            return &agent->tools[i];
        }
    }

    return NULL;
}

const agent_tool_entry_t *agent_tool_registry_find_const(const agent_t *agent,
                                                         const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return NULL;
    }

    for (i = 0u; i < agent->tool_count; i++) {
        if (agent->tools[i].def.name &&
            strcmp(agent->tools[i].def.name, name) == 0) {
            return &agent->tools[i];
        }
    }

    return NULL;
}

int agent_register_tool(agent_t *agent, const agent_tool_t *tool)
{
    agent_tool_entry_t *entry;

    if (!agent || !tool || !tool->name || !tool->execute) {
        return AGENT_ERROR_INVALID;
    }

    if (agent_tool_registry_find(agent, tool->name)) {
        return AGENT_ERROR_INVALID;
    }

    if (agent->tool_count >= CAGENT_MAX_TOOLS) {
        return AGENT_ERROR_LIMIT;
    }

    entry = &agent->tools[agent->tool_count++];
    memset(entry, 0, sizeof(*entry));
    entry->def = *tool;
    agent_tool_schema_mark_dirty(agent);

    return AGENT_OK;
}

int agent_register_tool_simple(agent_t *agent,
                                const char *name,
                                const char *description,
                                const char *input_schema_json,
                                agent_tool_fn execute,
                                void *user_data,
                                uint32_t flags)
{
    agent_tool_t tool;

    if (!agent || !name || !execute) {
        return AGENT_ERROR_INVALID;
    }

    memset(&tool, 0, sizeof(tool));
    tool.name = name;
    tool.description = description;
    tool.input_schema_json = input_schema_json;
    tool.execute = execute;
    tool.user_data = user_data;
    tool.flags = flags;

    return agent_register_tool(agent, &tool);
}

int agent_unregister_tool(agent_t *agent, const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return AGENT_ERROR_INVALID;
    }

    for (i = 0u; i < agent->tool_count; i++) {
        if (agent->tools[i].def.name &&
            strcmp(agent->tools[i].def.name, name) == 0) {
            uint32_t tail = agent->tool_count - i - 1u;
            if (tail > 0u) {
                memmove(&agent->tools[i],
                        &agent->tools[i + 1u],
                        tail * sizeof(agent->tools[0]));
            }
            agent->tool_count--;
            memset(&agent->tools[agent->tool_count], 0, sizeof(agent->tools[0]));
            agent_tool_schema_mark_dirty(agent);
            return AGENT_OK;
        }
    }

    return AGENT_ERROR_NOTFOUND;
}

int agent_tool_set_enabled(agent_t *agent, const char *name, int enabled)
{
    agent_tool_entry_t *entry;

    if (!agent || !name) {
        return AGENT_ERROR_INVALID;
    }

    entry = agent_tool_registry_find(agent, name);
    if (!entry) {
        return AGENT_ERROR_NOTFOUND;
    }

    if (enabled) {
        entry->def.flags &= ~AGENT_TOOL_FLAG_DISABLED;
    } else {
        entry->def.flags |= AGENT_TOOL_FLAG_DISABLED;
    }

    agent_tool_schema_mark_dirty(agent);
    return AGENT_OK;
}

int agent_tool_is_enabled(const agent_t *agent, const char *name, int *enabled)
{
    const agent_tool_entry_t *entry;

    if (!agent || !name || !enabled) {
        return AGENT_ERROR_INVALID;
    }

    entry = agent_tool_registry_find_const(agent, name);
    if (!entry) {
        return AGENT_ERROR_NOTFOUND;
    }

    *enabled = agent_tool_entry_is_enabled(entry) ? 1 : 0;
    return AGENT_OK;
}

int agent_tool_enumerate(const agent_t *agent,
                         agent_tool_enumerate_fn callback,
                         void *user_data)
{
    uint32_t i;

    if (!agent || !callback) {
        return AGENT_ERROR_INVALID;
    }

    for (i = 0u; i < agent->tool_count; i++) {
        const agent_tool_entry_t *entry = &agent->tools[i];
        agent_tool_info_t info;
        int ret;

        memset(&info, 0, sizeof(info));
        info.name = entry->def.name;
        info.description = entry->def.description;
        info.group_id = entry->def.group_id;
        info.category_id = entry->def.category_id;
        info.flags = entry->def.flags;
        info.timeout_ms = entry->def.timeout_ms;
        info.enabled = agent_tool_entry_is_enabled(entry) ? 1 : 0;
        info.llm_visible = agent_tool_entry_is_llm_visible(entry) ? 1 : 0;

        ret = callback(&info, user_data);
        if (ret != AGENT_OK) {
            return ret;
        }
    }

    return AGENT_OK;
}
