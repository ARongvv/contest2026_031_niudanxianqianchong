#include "smart_home_skills.h"
#include "../agent/smart_home_tool_metadata.h"
#include "../config/smart_home_config.h"

#include <stdio.h>

#ifndef CONFIG_SMART_HOME_DEMO_DATA_ROOT
#define CONFIG_SMART_HOME_DEMO_DATA_ROOT "/data"
#endif

#define SCHEMA_READ_SKILL                                                \
    "{\"type\":\"object\",\"properties\":{"                              \
    "\"name\":{\"type\":\"string\",\"description\":\"Skill name\"}},"    \
    "\"required\":[\"name\"],\"additionalProperties\":false}"

static int load_skills_from_default_paths(agent_t *agent,
                                          smart_home_skill_store_t *store)
{
    char path[160];
    int ret;

    snprintf(path,
             sizeof(path),
             "%s/res/skills",
             CONFIG_SMART_HOME_DEMO_DATA_ROOT);
    ret = smart_home_skill_loader_load_dir(agent, store, path);
    if (ret == AGENT_OK) {
        return AGENT_OK;
    }

    snprintf(path,
             sizeof(path),
             "%s/res/res/skills",
             CONFIG_SMART_HOME_DEMO_DATA_ROOT);
    return smart_home_skill_loader_load_dir(agent, store, path);
}

int smart_home_skills_register(agent_t *agent,
                               smart_home_skill_store_t *store)
{
    int ret;

    if (!agent || !store) {
        return AGENT_ERROR_INVALID;
    }

    ret = load_skills_from_default_paths(agent, store);
    if (ret != AGENT_OK) {
        return ret;
    }

    {
        agent_tool_t tool = {0};

        tool.name = "read_skill";
        tool.group_id = SMART_HOME_TOOL_GROUP_LOCAL;
        tool.category_id = SMART_HOME_TOOL_CATEGORY_SKILL;
        tool.description = "Read full Markdown instructions for a named skill.";
        tool.input_schema_json = SCHEMA_READ_SKILL;
        tool.execute = smart_home_skill_read_tool;
        tool.user_data = store;
        tool.flags = AGENT_TOOL_FLAG_LLM_VISIBLE | AGENT_TOOL_FLAG_READ_ONLY;
        return agent_register_tool(agent, &tool);
    }
}
