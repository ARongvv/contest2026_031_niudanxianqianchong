/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Skill registry.
 *
 * Skills are context resources. The registry stores shallow copies of
 * developer-provided definitions and the context builder injects enabled
 * skills into the system message.
 */

#include "skills_internal.h"

#include "../core/agent_internal.h"

#include <string.h>
#include <stdio.h>

bool agent_skill_entry_is_enabled(const agent_skill_entry_t *entry)
{
    return entry && ((entry->def.flags & AGENT_SKILL_FLAG_ENABLED) != 0u);
}

agent_skill_entry_t *agent_skill_registry_find(agent_t *agent, const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return NULL;
    }

    for (i = 0u; i < agent->skill_count; i++) {
        if (agent->skills[i].def.name &&
            strcmp(agent->skills[i].def.name, name) == 0) {
            return &agent->skills[i];
        }
    }

    return NULL;
}

const agent_skill_entry_t *agent_skill_registry_find_const(const agent_t *agent,
                                                           const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return NULL;
    }

    for (i = 0u; i < agent->skill_count; i++) {
        if (agent->skills[i].def.name &&
            strcmp(agent->skills[i].def.name, name) == 0) {
            return &agent->skills[i];
        }
    }

    return NULL;
}

int agent_register_skill(agent_t *agent, const agent_skill_t *skill)
{
    agent_skill_entry_t *entry;

    if (!agent || !skill || !skill->name || !skill->context_text) {
        return AGENT_ERROR_INVALID;
    }
    if (agent_skill_registry_find(agent, skill->name)) {
        return AGENT_ERROR_INVALID;
    }
    if (agent->skill_count >= CAGENT_MAX_SKILLS) {
        return AGENT_ERROR_LIMIT;
    }

    entry = &agent->skills[agent->skill_count++];
    memset(entry, 0, sizeof(*entry));
    entry->def = *skill;
    entry->order = agent->skill_order_next++;

    return AGENT_OK;
}

int agent_register_skill_simple(agent_t *agent,
                                const char *name,
                                const char *description,
                                const char *context_text,
                                uint32_t priority,
                                uint32_t flags)
{
    agent_skill_t skill;

    if (!agent || !name || !context_text) {
        return AGENT_ERROR_INVALID;
    }

    memset(&skill, 0, sizeof(skill));
    skill.name = name;
    skill.description = description;
    skill.context_text = context_text;
    skill.priority = priority;
    skill.flags = flags;

    return agent_register_skill(agent, &skill);
}

int agent_unregister_skill(agent_t *agent, const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return AGENT_ERROR_INVALID;
    }

    for (i = 0u; i < agent->skill_count; i++) {
        if (agent->skills[i].def.name &&
            strcmp(agent->skills[i].def.name, name) == 0) {
            uint32_t tail = agent->skill_count - i - 1u;
            if (tail > 0u) {
                memmove(&agent->skills[i],
                        &agent->skills[i + 1u],
                        tail * sizeof(agent->skills[0]));
            }
            agent->skill_count--;
            memset(&agent->skills[agent->skill_count], 0, sizeof(agent->skills[0]));
            return AGENT_OK;
        }
    }

    return AGENT_ERROR_NOTFOUND;
}

static int append_text(char *buffer,
                       size_t buffer_size,
                       size_t *used,
                       const char *text)
{
    size_t len;

    if (!buffer || !used || !text) {
        return AGENT_ERROR_INVALID;
    }

    len = strlen(text);
    if (*used + len >= buffer_size) {
        if (buffer_size > 0u) {
            buffer[*used < buffer_size ? *used : buffer_size - 1u] = '\0';
        }
        return AGENT_ERROR_CONTEXT_OVERFLOW;
    }

    memcpy(buffer + *used, text, len);
    *used += len;
    buffer[*used] = '\0';
    return AGENT_OK;
}

static int append_skill(agent_skill_entry_t *entry,
                        char *buffer,
                        size_t buffer_size,
                        size_t *used)
{
    int ret;

    if (!agent_skill_entry_is_enabled(entry)) {
        return AGENT_OK;
    }

    if (*used > 0u) {
        ret = append_text(buffer, buffer_size, used, "\n\n");
        if (ret != AGENT_OK) {
            return ret;
        }
    }

    ret = append_text(buffer, buffer_size, used, "Skill: ");
    if (ret != AGENT_OK) {
        return ret;
    }
    ret = append_text(buffer, buffer_size, used, entry->def.name);
    if (ret != AGENT_OK) {
        return ret;
    }
    if (entry->def.description && entry->def.description[0] != '\0') {
        ret = append_text(buffer, buffer_size, used, "\nDescription: ");
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = append_text(buffer, buffer_size, used, entry->def.description);
        if (ret != AGENT_OK) {
            return ret;
        }
    }

    uint32_t remaining = (uint32_t)(buffer_size - *used);
    if (entry->def.flags & AGENT_SKILL_FLAG_SUMMARY_ONLY) {
        int n = snprintf(buffer + *used, remaining,
            "\n(Full content: call read_skill \"%s\")\n",
            entry->def.name);
        if (n < 0 || (uint32_t)n >= remaining) {
            return AGENT_ERROR_CONTEXT_OVERFLOW;
        }
        *used += (size_t)n;
        return AGENT_OK;
    }

    ret = append_text(buffer, buffer_size, used, "\nInstructions:\n");
    if (ret != AGENT_OK) {
        return ret;
    }
    return append_text(buffer, buffer_size, used, entry->def.context_text);
}

int agent_skill_context_build(agent_t *agent,
                              char *buffer,
                              size_t buffer_size,
                              size_t *written)
{
    bool done[CAGENT_MAX_SKILLS];
    uint32_t done_count = 0u;
    size_t used = 0u;
    int ret;

    if (!agent || !buffer || buffer_size == 0u) {
        return AGENT_ERROR_INVALID;
    }

    buffer[0] = '\0';
    memset(done, 0, sizeof(done));

    while (done_count < agent->skill_count) {
        uint32_t i;
        uint32_t best = UINT32_MAX;

        for (i = 0u; i < agent->skill_count; i++) {
            if (done[i]) {
                continue;
            }
            if (best == UINT32_MAX ||
                agent->skills[i].def.priority > agent->skills[best].def.priority) {
                best = i;
            }
        }

        if (best == UINT32_MAX) {
            break;
        }

        ret = append_skill(&agent->skills[best], buffer, buffer_size, &used);
        if (ret != AGENT_OK) {
            return ret;
        }

        done[best] = true;
        done_count++;
    }

    if (written) {
        *written = used;
    }
    return AGENT_OK;
}
