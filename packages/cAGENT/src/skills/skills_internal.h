/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <agent.h>

typedef struct {
    agent_skill_t def;
    uint32_t order;
} agent_skill_entry_t;

agent_skill_entry_t *agent_skill_registry_find(agent_t *agent, const char *name);
const agent_skill_entry_t *agent_skill_registry_find_const(const agent_t *agent,
                                                           const char *name);

bool agent_skill_entry_is_enabled(const agent_skill_entry_t *entry);

int agent_skill_context_build(agent_t *agent,
                              char *buffer,
                              size_t buffer_size,
                              size_t *written);
