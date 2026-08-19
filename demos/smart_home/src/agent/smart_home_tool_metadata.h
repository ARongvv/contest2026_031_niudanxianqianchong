#pragma once

#include <stdint.h>

/* cAGENT group_id：smart_home 应用定义的生命周期/来源分组。 */
#define SMART_HOME_TOOL_GROUP_LOCAL 0u
#define SMART_HOME_TOOL_GROUP_NODE  10u
#define SMART_HOME_TOOL_GROUP_MCP   20u

/* cAGENT category_id：smart_home 设置页的展示分类。 */
typedef enum {
    SMART_HOME_TOOL_CATEGORY_UNSPECIFIED = 0,
    SMART_HOME_TOOL_CATEGORY_QUERY = 1,
    SMART_HOME_TOOL_CATEGORY_CONTROL = 2,
    SMART_HOME_TOOL_CATEGORY_SKILL = 3,
} smart_home_tool_category_t;
