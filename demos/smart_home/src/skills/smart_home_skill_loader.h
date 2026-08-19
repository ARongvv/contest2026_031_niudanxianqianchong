#pragma once

#include <stddef.h>

#include <agent.h>

#define SMART_HOME_SKILL_STORE_MAX 8u

typedef struct {
    char *buffer;
    char *name;
    char *description;
    char *context_text;
} smart_home_loaded_skill_t;

typedef struct {
    smart_home_loaded_skill_t skills[SMART_HOME_SKILL_STORE_MAX];
    size_t count;
} smart_home_skill_store_t;

void smart_home_skill_store_init(smart_home_skill_store_t *store);
void smart_home_skill_store_deinit(smart_home_skill_store_t *store);

int smart_home_skill_loader_load_dir(agent_t *agent,
                                     smart_home_skill_store_t *store,
                                     const char *dir);

const smart_home_loaded_skill_t *smart_home_skill_store_find(
    const smart_home_skill_store_t *store,
    const char *name);

int smart_home_skill_read_tool(const agent_tool_call_t *call,
                               agent_tool_result_t *result,
                               void *user_data);
