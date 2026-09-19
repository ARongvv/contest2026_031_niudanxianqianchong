/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Miloco 网关 Agent 工具：模型可通过 miot_device_list 查看米家设备，
 * 通过 miot_device_control 下发电源控制。
 */

#include "smart_home_miloco.h"

#include "../agent/smart_home_agent.h"
#include "../config/cjson_compat.h"

#include <cagent/types.h>
#include <cagent/tools.h>

#include <stdio.h>
#include <string.h>

#define MILOCO_TOOL_LIST_BUFFER_BYTES 2048

#define SCHEMA_MIOT_DEVICE_LIST \
    "{\"type\":\"object\",\"properties\":{}}"

#define SCHEMA_MIOT_DEVICE_CONTROL                                      \
    "{\"type\":\"object\",\"properties\":"                               \
    "{\"did\":{\"type\":\"string\",\"description\":\"Mi Home device id\"}," \
    "\"power_on\":{\"type\":\"boolean\",\"description\":\"true = on\"}}," \
    "\"required\":[\"did\",\"power_on\"]}"

static const char *category_name(smart_home_miloco_category_t category)
{
    switch (category) {
    case SMART_HOME_MILOCO_CATEGORY_LIGHT:
        return "light";
    case SMART_HOME_MILOCO_CATEGORY_AC:
        return "air-conditioner";
    case SMART_HOME_MILOCO_CATEGORY_OUTLET:
        return "outlet";
    case SMART_HOME_MILOCO_CATEGORY_CAMERA:
        return "camera";
    case SMART_HOME_MILOCO_CATEGORY_FAN:
        return "fan";
    case SMART_HOME_MILOCO_CATEGORY_OTHER:
        return "other";
    default:
        return "unknown";
    }
}

static int miot_device_list_tool(const agent_tool_call_t *call,
                                 agent_tool_result_t *result,
                                 void *user_data)
{
    static char output[MILOCO_TOOL_LIST_BUFFER_BYTES];
    smart_home_agent_app_t *app = user_data;
    smart_home_miloco_device_t devices[SMART_HOME_MILOCO_MAX_DEVICES];
    size_t count;
    size_t used;
    size_t i;

    (void)call;
    result->status = AGENT_ERROR;
    result->content_json = output;
    result->error_message = "gateway unavailable";
    if (!app || !app->miloco) {
        snprintf(output, sizeof(output),
                 "{\"ok\":false,\"error\":\"miloco_gateway_not_configured\"}");
        return result->status;
    }
    if (!smart_home_miloco_reachable(app->miloco)) {
        snprintf(output, sizeof(output),
                 "{\"ok\":false,\"error\":\"miloco_gateway_unreachable\"}");
        return result->status;
    }

    count = smart_home_miloco_list(app->miloco, devices,
                                   SMART_HOME_MILOCO_MAX_DEVICES, NULL);
    used = (size_t)snprintf(output, sizeof(output),
                            "{\"ok\":true,\"source\":\"miloco\",\"devices\":[");
    for (i = 0; i < count && used < sizeof(output) - 160u; i++) {
        used += (size_t)snprintf(
            output + used, sizeof(output) - used,
            "%s{\"did\":\"%s\",\"name\":\"%s\",\"room\":\"%s\","
            "\"category\":\"%s\",\"online\":%s,\"power_on\":%s}",
            i > 0 ? "," : "", devices[i].did, devices[i].name,
            devices[i].room, category_name(devices[i].category),
            devices[i].online ? "true" : "false",
            devices[i].power_on ? "true" : "false");
    }
    snprintf(output + used, sizeof(output) - used, "]}");
    result->status = AGENT_OK;
    result->error_message = NULL;
    return AGENT_OK;
}

static int miot_device_control_tool(const agent_tool_call_t *call,
                                    agent_tool_result_t *result,
                                    void *user_data)
{
    static char output[192];
    smart_home_agent_app_t *app = user_data;
    cJSON *root;
    const cJSON *did;
    const cJSON *power_on;

    result->status = AGENT_ERROR;
    result->content_json = output;
    result->error_message = "control failed";
    if (!app || !app->miloco || !call || !call->arguments_json) {
        snprintf(output, sizeof(output),
                 "{\"ok\":false,\"error\":\"miloco_gateway_not_configured\"}");
        return result->status;
    }

    root = cJSON_Parse(call->arguments_json);
    if (!root) {
        snprintf(output, sizeof(output),
                 "{\"ok\":false,\"error\":\"invalid_arguments\"}");
        return result->status;
    }
    did = cJSON_GetObjectItemCaseSensitive(root, "did");
    power_on = cJSON_GetObjectItemCaseSensitive(root, "power_on");
    if (!cJSON_IsString(did) || !did->valuestring[0] ||
        !cJSON_IsBool(power_on)) {
        cJSON_Delete(root);
        snprintf(output, sizeof(output),
                 "{\"ok\":false,\"error\":\"invalid_arguments\"}");
        return result->status;
    }

    result->status = smart_home_miloco_submit_power(
        app->miloco, did->valuestring, cJSON_IsTrue(power_on));
    cJSON_Delete(root);
    if (result->status != AGENT_OK) {
        snprintf(output, sizeof(output),
                 "{\"ok\":false,\"error\":\"submit_failed\",\"code\":%d}",
                 result->status);
        result->status = AGENT_ERROR;
        return result->status;
    }
    snprintf(output, sizeof(output),
             "{\"ok\":true,\"did\":\"%s\",\"power_on\":%s,"
             "\"note\":\"control submitted; result visible on next poll\"}",
             did->valuestring, cJSON_IsTrue(power_on) ? "true" : "false");
    result->status = AGENT_OK;
    result->error_message = NULL;
    return AGENT_OK;
}

static int register_tool(agent_t *agent,
                         const char *name,
                         const char *description,
                         const char *input_schema_json,
                         agent_tool_fn execute,
                         void *user_data,
                         uint32_t flags)
{
    agent_tool_t tool = {0};

    tool.name = name;
    tool.description = description;
    tool.input_schema_json = input_schema_json;
    tool.execute = execute;
    tool.user_data = user_data;
    tool.flags = flags;
    return agent_register_tool(agent, &tool);
}

int smart_home_miloco_tools_register(agent_t *agent,
                                     smart_home_agent_app_t *app)
{
    int ret;

    if (!agent || !app) {
        return AGENT_ERROR_INVALID;
    }
    ret = register_tool(agent, "miot_device_list",
                        "List Mi Home devices bridged through the Miloco "
                        "home-server gateway, with online and power state.",
                        SCHEMA_MIOT_DEVICE_LIST,
                        miot_device_list_tool, app,
                        AGENT_TOOL_FLAG_LLM_VISIBLE |
                            AGENT_TOOL_FLAG_READ_ONLY);
    if (ret != AGENT_OK) {
        return ret;
    }
    return register_tool(agent, "miot_device_control",
                         "Turn a Mi Home device on or off through the "
                         "Miloco gateway. Use miot_device_list to find "
                         "the did first.",
                         SCHEMA_MIOT_DEVICE_CONTROL,
                         miot_device_control_tool, app,
                         AGENT_TOOL_FLAG_LLM_VISIBLE);
}
