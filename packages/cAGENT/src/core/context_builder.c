/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Context builder.
 *
 * Build order:
 *   1. system prompt (critical)
 *   2. registered context providers by priority desc, stable by registration
 *   3. enabled skills by priority desc, stable by registration
 */

#include "agent_internal.h"

#include "../skills/skills_internal.h"
#include "../types_internal.h"

#include <stdbool.h>
#include <string.h>

static int append_bytes(char *buffer,
                        size_t buffer_size,
                        size_t *used,
                        const char *text,
                        size_t len)
{
    if (!buffer || !used || !text) {
        return AGENT_ERROR_INVALID;
    }
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

static int append_text(char *buffer,
                       size_t buffer_size,
                       size_t *used,
                       const char *text)
{
    return append_bytes(buffer,
                        buffer_size,
                        used,
                        text ? text : "",
                        text ? strlen(text) : 0u);
}

static int append_separator(char *buffer,
                            size_t buffer_size,
                            size_t *used)
{
    if (*used == 0u) {
        return AGENT_OK;
    }
    return append_text(buffer, buffer_size, used, "\n\n");
}

static bool provider_is_disabled(const agent_context_provider_t *provider)
{
    return provider &&
           ((provider->flags & AGENT_CONTEXT_FLAG_DISABLED) != 0u);
}

static bool provider_is_critical(const agent_context_provider_t *provider)
{
    return provider &&
           ((provider->flags & AGENT_CONTEXT_FLAG_CRITICAL) != 0u);
}

static int find_provider_index(agent_t *agent, const char *name)
{
    uint32_t i;

    if (!agent || !name) {
        return -1;
    }

    for (i = 0u; i < agent->context_provider_count; i++) {
        if (agent->context_providers[i].name &&
            strcmp(agent->context_providers[i].name, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

int agent_register_context_provider(agent_t *agent,
                                    const agent_context_provider_t *provider)
{
    agent_context_provider_t *slot;

    if (!agent || !provider || !provider->name || !provider->build) {
        return AGENT_ERROR_INVALID;
    }
    if (find_provider_index(agent, provider->name) >= 0) {
        return AGENT_ERROR_INVALID;
    }
    if (agent->context_provider_count >= CAGENT_MAX_CONTEXT_PROVIDERS) {
        return AGENT_ERROR_LIMIT;
    }

    slot = &agent->context_providers[agent->context_provider_count++];
    *slot = *provider;
    agent->context_provider_order_next++;

    return AGENT_OK;
}

int agent_unregister_context_provider(agent_t *agent, const char *name)
{
    int idx;

    if (!agent || !name) {
        return AGENT_ERROR_INVALID;
    }

    idx = find_provider_index(agent, name);
    if (idx < 0) {
        return AGENT_ERROR_NOTFOUND;
    }

    if ((uint32_t)idx + 1u < agent->context_provider_count) {
        memmove(&agent->context_providers[idx],
                &agent->context_providers[(uint32_t)idx + 1u],
                (agent->context_provider_count - (uint32_t)idx - 1u) *
                    sizeof(agent->context_providers[0]));
    }

    agent->context_provider_count--;
    memset(&agent->context_providers[agent->context_provider_count],
           0,
           sizeof(agent->context_providers[0]));

    return AGENT_OK;
}

static int append_one_provider(agent_t *agent,
                               const agent_context_provider_t *provider,
                               char *buffer,
                               size_t buffer_size,
                               size_t *used)
{
    size_t before;
    size_t written = 0u;
    int ret;

    CAGENT_UNUSED(agent);

    if (provider_is_disabled(provider)) {
        return AGENT_OK;
    }

    before = *used;
    ret = append_separator(buffer, buffer_size, used);
    if (ret != AGENT_OK) {
        *used = before;
        buffer[before] = '\0';
        return provider_is_critical(provider) ? ret : AGENT_OK;
    }

    ret = provider->build(buffer + *used,
                          buffer_size - *used,
                          &written,
                          provider->user_data);
    if (ret != AGENT_OK) {
        *used = before;
        buffer[before] = '\0';
        return provider_is_critical(provider) ? ret : AGENT_OK;
    }
    if (*used + written >= buffer_size) {
        *used = before;
        buffer[before] = '\0';
        return provider_is_critical(provider) ? AGENT_ERROR_CONTEXT_OVERFLOW
                                              : AGENT_OK;
    }

    *used += written;
    buffer[*used] = '\0';
    return AGENT_OK;
}

static int append_registered_providers(agent_t *agent,
                                       char *buffer,
                                       size_t buffer_size,
                                       size_t *used)
{
    bool done[CAGENT_MAX_CONTEXT_PROVIDERS];
    uint32_t done_count = 0u;
    uint32_t i;
    int ret;

    memset(done, 0, sizeof(done));

    while (done_count < agent->context_provider_count) {
        uint32_t best = UINT32_MAX;

        for (i = 0u; i < agent->context_provider_count; i++) {
            if (done[i]) {
                continue;
            }
            if (best == UINT32_MAX ||
                agent->context_providers[i].priority >
                    agent->context_providers[best].priority) {
                best = i;
            }
        }

        if (best == UINT32_MAX) {
            break;
        }

        ret = append_one_provider(agent,
                                  &agent->context_providers[best],
                                  buffer,
                                  buffer_size,
                                  used);
        if (ret != AGENT_OK) {
            return ret;
        }

        done[best] = true;
        done_count++;
    }

    return AGENT_OK;
}

int agent_context_build(agent_t *agent,
                        const agent_request_t *request,
                        char *buffer,
                        size_t buffer_size,
                        size_t *written)
{
    size_t used = 0u;
    size_t skill_written = 0u;
    int ret;

    CAGENT_UNUSED(request);

    if (!agent || !buffer || buffer_size == 0u) {
        return AGENT_ERROR_INVALID;
    }

    buffer[0] = '\0';

    ret = append_text(buffer,
                      buffer_size,
                      &used,
                      agent->config.system_prompt ? agent->config.system_prompt : "");
    if (ret != AGENT_OK) {
        return AGENT_ERROR_CONTEXT_OVERFLOW;
    }

    ret = append_registered_providers(agent, buffer, buffer_size, &used);
    if (ret != AGENT_OK) {
        return ret;
    }

    ret = agent_skill_context_build(agent, buffer + used, buffer_size - used, &skill_written);
    if (ret == AGENT_OK && skill_written > 0u) {
        if (used > 0u) {
            size_t skill_len = skill_written;
            if (used + 2u + skill_len >= buffer_size) {
                return AGENT_ERROR_CONTEXT_OVERFLOW;
            }
            memmove(buffer + used + 2u, buffer + used, skill_len + 1u);
            buffer[used] = '\n';
            buffer[used + 1u] = '\n';
            used += 2u + skill_len;
        } else {
            used += *written;
        }
    } else if (ret != AGENT_OK) {
        return ret;
    }

    if (written) {
        *written = used;
    }
    return AGENT_OK;
}
