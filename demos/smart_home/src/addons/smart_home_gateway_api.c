#include "smart_home_gateway_api.h"
#include "../agent/smart_home_agent_run_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int response_error(smart_home_gateway_response_t *response,
                          int status_code, const char *code)
{
    response->status_code = status_code;
    snprintf(response->body, sizeof(response->body),
             "{\"error\":{\"code\":\"%s\"}}", code);
    return AGENT_OK;
}

static int get_json_string(const char *body, const char *key, char *out,
                           size_t out_size)
{
    const char *start;
    const char *end;
    size_t len;

    if (!body || !key || !out || out_size == 0u) {
        return AGENT_ERROR_INVALID;
    }
    start = strstr(body, key);
    if (!start || !(start = strchr(start + strlen(key), ':'))) {
        return AGENT_ERROR_PARSE;
    }
    while (*++start == ' ' || *start == '\t') {
    }
    if (*start != '"' || !(end = strchr(++start, '"'))) {
        return AGENT_ERROR_PARSE;
    }
    len = (size_t)(end - start);
    if (len >= out_size) {
        return AGENT_ERROR_LIMIT;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return AGENT_OK;
}

static int get_json_int(const char *body, const char *key, int *out)
{
    const char *value;
    char *end;
    long parsed;

    if (!body || !key || !out || !(value = strstr(body, key)) ||
        !(value = strchr(value + strlen(key), ':'))) {
        return AGENT_ERROR_PARSE;
    }
    parsed = strtol(value + 1, &end, 10);
    if (end == value + 1) {
        return AGENT_ERROR_PARSE;
    }
    *out = (int)parsed;
    return AGENT_OK;
}

static int handle_command(smart_home_agent_app_t *app,
                          const char *body,
                          smart_home_gateway_response_t *response)
{
    smart_home_state_t state;
    smart_home_device_t *device = NULL;
    char device_id[16];
    char action[32];
    int id;
    int value;
    int i;
    int ret;

    ret = get_json_string(body, "\"deviceId\"", device_id, sizeof(device_id));
    if (ret == AGENT_OK) {
        ret = get_json_string(body, "\"action\"", action, sizeof(action));
    }
    if (ret != AGENT_OK || (id = atoi(device_id)) <= 0 ||
        smart_home_device_service_copy_state(&app->device_service, &state) != AGENT_OK) {
        return response_error(response, 400, "invalid_command");
    }
    for (i = 0; i < SMART_HOME_MAX_DEVICES; i++) {
        if (state.devices[i].used && state.devices[i].id == id) {
            device = &state.devices[i];
            break;
        }
    }
    if (!device) {
        return response_error(response, 404, "device_not_found");
    }
    value = device->type == SMART_HOME_DEVICE_AC ? device->temperature :
                                                    device->brightness;
    if (strcmp(action, "turn_on") == 0) {
        ret = smart_home_device_service_set_device_control(&app->device_service,
                                                            id, 1, value,
                                                            device->ac_mode,
                                                            device->ac_fan_speed);
    } else if (strcmp(action, "turn_off") == 0) {
        ret = smart_home_device_service_set_device_control(&app->device_service,
                                                            id, 0, value,
                                                            device->ac_mode,
                                                            device->ac_fan_speed);
    } else if (strcmp(action, "set_brightness") == 0 &&
               device->type == SMART_HOME_DEVICE_LIGHT &&
               get_json_int(body, "\"value\"", &value) == AGENT_OK) {
        ret = smart_home_device_service_set_device_control(&app->device_service,
                                                            id, 1, value, 0, 0);
    } else if (strcmp(action, "set_temperature") == 0 &&
               device->type == SMART_HOME_DEVICE_AC &&
               get_json_int(body, "\"value\"", &value) == AGENT_OK) {
        ret = smart_home_device_service_set_device_control(&app->device_service,
                                                            id, 1, value,
                                                            device->ac_mode,
                                                            device->ac_fan_speed);
    } else {
        return response_error(response, 400, "unsupported_action");
    }
    if (ret != AGENT_OK) {
        return response_error(response, 409, "command_rejected");
    }
    response->status_code = 202;
    snprintf(response->body, sizeof(response->body),
             "{\"accepted\":true,\"revision\":%lu}",
             (unsigned long)smart_home_device_service_revision(&app->device_service));
    return AGENT_OK;
}

typedef struct {
    smart_home_gateway_response_t *response;
    size_t offset;
    size_t count;
} tool_catalog_writer_t;

static int append_catalog_text(tool_catalog_writer_t *writer,
                               const char *text)
{
    size_t length;

    if (!writer || !writer->response || !text ||
        writer->offset >= sizeof(writer->response->body)) {
        return AGENT_ERROR_LIMIT;
    }

    length = strlen(text);
    if (length >= sizeof(writer->response->body) - writer->offset) {
        return AGENT_ERROR_LIMIT;
    }

    memcpy(writer->response->body + writer->offset, text, length);
    writer->offset += length;
    writer->response->body[writer->offset] = '\0';
    return AGENT_OK;
}

static int append_catalog_json_string(tool_catalog_writer_t *writer,
                                      const char *text)
{
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    char escaped[7];
    int ret;

    ret = append_catalog_text(writer, "\"");
    if (ret != AGENT_OK) {
        return ret;
    }

    while (*cursor) {
        switch (*cursor) {
        case '\\':
            ret = append_catalog_text(writer, "\\\\");
            break;
        case '\"':
            ret = append_catalog_text(writer, "\\\"");
            break;
        case '\b':
            ret = append_catalog_text(writer, "\\b");
            break;
        case '\f':
            ret = append_catalog_text(writer, "\\f");
            break;
        case '\n':
            ret = append_catalog_text(writer, "\\n");
            break;
        case '\r':
            ret = append_catalog_text(writer, "\\r");
            break;
        case '\t':
            ret = append_catalog_text(writer, "\\t");
            break;
        default:
            if (*cursor < 0x20u) {
                snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
                ret = append_catalog_text(writer, escaped);
            } else {
                escaped[0] = (char)*cursor;
                escaped[1] = '\0';
                ret = append_catalog_text(writer, escaped);
            }
            break;
        }

        if (ret != AGENT_OK) {
            return ret;
        }
        cursor++;
    }

    return append_catalog_text(writer, "\"");
}

static const char *tool_source_name(uint16_t group_id)
{
    switch (group_id) {
    case 0u:
        return "local";
    case 10u:
        return "node";
    case 20u:
        return "mcp";
    default:
        return "other";
    }
}

static int append_catalog_tool(const agent_tool_info_t *tool,
                               void *user_data)
{
    tool_catalog_writer_t *writer = user_data;
    char fields[192];
    int length;
    int ret;

    if (!tool || !writer || !tool->name) {
        return AGENT_ERROR_INVALID;
    }

    ret = append_catalog_text(writer, writer->count ? ",{\"id\":" : "{\"id\":");
    if (ret == AGENT_OK) {
        ret = append_catalog_json_string(writer, tool->name);
    }
    if (ret == AGENT_OK) {
        ret = append_catalog_text(writer, ",\"name\":");
    }
    if (ret == AGENT_OK) {
        ret = append_catalog_json_string(writer, tool->name);
    }
    if (ret == AGENT_OK) {
        ret = append_catalog_text(writer, ",\"description\":");
    }
    if (ret == AGENT_OK) {
        ret = append_catalog_json_string(writer, tool->description);
    }
    if (ret != AGENT_OK) {
        return ret;
    }

    length = snprintf(fields, sizeof(fields),
                      ",\"source\":\"%s\",\"groupId\":%u,"
                      "\"categoryId\":%u,\"enabled\":%s,"
                      "\"visible\":true,\"llmVisible\":%s,"
                      "\"readOnly\":%s,\"timeoutMs\":%lu}",
                      tool_source_name(tool->group_id),
                      (unsigned int)tool->group_id,
                      (unsigned int)tool->category_id,
                      tool->enabled ? "true" : "false",
                      tool->llm_visible ? "true" : "false",
                      (tool->flags & AGENT_TOOL_FLAG_READ_ONLY) ? "true" : "false",
                      (unsigned long)tool->timeout_ms);
    if (length < 0 || (size_t)length >= sizeof(fields)) {
        return AGENT_ERROR_LIMIT;
    }

    ret = append_catalog_text(writer, fields);
    if (ret == AGENT_OK) {
        writer->count++;
    }
    return ret;
}

static int handle_catalog(smart_home_agent_app_t *app, const char *path,
                          smart_home_gateway_response_t *response)
{
    size_t offset = 0u;
    size_t i;
    int n;

    response->status_code = 200;
    if (strcmp(path, "/v1/catalog/tools") == 0) {
        tool_catalog_writer_t writer = {
            .response = response,
            .offset = 0u,
            .count = 0u,
        };
        int ret = append_catalog_text(&writer, "{\"tools\":[");

        if (ret == AGENT_OK) {
            ret = smart_home_agent_app_enumerate_tools(app,
                                                        append_catalog_tool,
                                                        &writer);
        }
        if (ret == AGENT_OK) {
            ret = append_catalog_text(&writer, "]}");
        }
        if (ret != AGENT_OK) {
            return response_error(response, 500, "response_too_large");
        }
        return AGENT_OK;
    }
    n = snprintf(response->body, sizeof(response->body), "{\"skills\":[");
    offset = n > 0 ? (size_t)n : sizeof(response->body);
    for (i = 0; i < app->skill_store.count && offset < sizeof(response->body); i++) {
        const smart_home_loaded_skill_t *skill = &app->skill_store.skills[i];
        n = snprintf(response->body + offset, sizeof(response->body) - offset,
                     "%s{\"id\":\"%s\",\"label\":\"%s\"}",
                     i ? "," : "", skill->name ? skill->name : "",
                     skill->description ? skill->description : "");
        if (n < 0 || (size_t)n >= sizeof(response->body) - offset) {
            return response_error(response, 500, "response_too_large");
        }
        offset += (size_t)n;
    }
    snprintf(response->body + offset, sizeof(response->body) - offset, "]}");
    return AGENT_OK;
}

int smart_home_gateway_api_handle(smart_home_agent_app_t *app,
                                  const smart_home_gateway_request_t *request,
                                  smart_home_gateway_response_t *response)
{
    char scene[32];
    const char *device_prefix = "/v1/devices/";
    int ret;

    if (!app || !request || !request->method || !request->path || !response) {
        return AGENT_ERROR_INVALID;
    }
    memset(response, 0, sizeof(*response));
    if (strcmp(request->method, "GET") == 0 &&
        strcmp(request->path, "/v1/home/snapshot") == 0) {
        ret = smart_home_device_service_build_snapshot_json(&app->device_service,
                                                             response->body,
                                                             sizeof(response->body));
        response->status_code = ret == AGENT_OK ? 200 : 500;
        return ret;
    }
    if (strcmp(request->method, "POST") == 0 &&
        strcmp(request->path, "/v1/commands") == 0) {
        return handle_command(app, request->body, response);
    }
    if (strcmp(request->method, "POST") == 0 &&
        strncmp(request->path, "/v1/conversations/", 18) == 0) {
        const char *suffix = strstr(request->path + 18, "/messages");
        char conversation_id[48];
        char message[512];
        char run_id[24];
        size_t length;

        if (!app->run_service) {
            return response_error(response, 503, "chat_not_enabled");
        }
        if (!suffix || suffix[9] != '\0') {
            return response_error(response, 404, "route_not_found");
        }
        length = (size_t)(suffix - (request->path + 18));
        if (length == 0u || length >= sizeof(conversation_id) ||
            get_json_string(request->body, "\"message\"", message,
                            sizeof(message)) != AGENT_OK) {
            return response_error(response, 400, "invalid_message");
        }
        memcpy(conversation_id, request->path + 18, length);
        conversation_id[length] = '\0';
        ret = smart_home_agent_run_service_submit(app->run_service,
                                                  conversation_id, message,
                                                  run_id, sizeof(run_id));
        if (ret == AGENT_ERROR_BUSY) {
            return response_error(response, 409, "run_busy");
        }
        if (ret != AGENT_OK) {
            return response_error(response, 400, "message_rejected");
        }
        response->status_code = 202;
        snprintf(response->body, sizeof(response->body),
                 "{\"runId\":\"%s\",\"status\":\"queued\"}", run_id);
        return AGENT_OK;
    }
    if (strcmp(request->method, "GET") == 0 &&
        strncmp(request->path, "/v1/runs/", 9) == 0) {
        const char *run_id = request->path + 9;
        const char *trace = strstr(run_id, "/trace");

        if (!app->run_service) {
            return response_error(response, 503, "chat_not_enabled");
        }
        if (trace != NULL) {
            if (strcmp(trace, "/trace") != 0) {
                return response_error(response, 404, "route_not_found");
            }
            response->status_code = 200;
            snprintf(response->body, sizeof(response->body),
                     "{\"runId\":\"%.*s\",\"trace\":[]}",
                     (int)(trace - run_id), run_id);
            return AGENT_OK;
        }
        ret = smart_home_agent_run_service_get(app->run_service, run_id,
                                               response->body,
                                               sizeof(response->body));
        if (ret == AGENT_ERROR_NOTFOUND) {
            return response_error(response, 404, "run_not_found");
        }
        response->status_code = ret == AGENT_OK ? 200 : 500;
        return ret;
    }
    if (strcmp(request->method, "POST") == 0 &&
        strncmp(request->path, "/v1/scenes/", 11) == 0 &&
        strstr(request->path, "/runs") != NULL) {
        size_t len = (size_t)(strstr(request->path + 11, "/runs") -
                              (request->path + 11));
        if (len == 0u || len >= sizeof(scene)) {
            return response_error(response, 400, "invalid_scene");
        }
        memcpy(scene, request->path + 11, len);
        scene[len] = '\0';
        ret = smart_home_device_service_run_scene(&app->device_service, scene);
        if (ret != AGENT_OK) {
            return response_error(response, ret == AGENT_ERROR_NOTFOUND ? 404 : 409,
                                  "scene_rejected");
        }
        response->status_code = 202;
        snprintf(response->body, sizeof(response->body),
                 "{\"accepted\":true,\"sceneId\":\"%s\",\"revision\":%lu}",
                 scene, (unsigned long)smart_home_device_service_revision(&app->device_service));
        return AGENT_OK;
    }
    if (strcmp(request->method, "GET") == 0 &&
        strcmp(request->path, "/v1/history") == 0) {
        ret = smart_home_device_service_build_events_json(&app->device_service, 0u,
                                                           response->body, sizeof(response->body));
        response->status_code = ret == AGENT_OK ? 200 : 500;
        return ret;
    }
    if (strcmp(request->method, "GET") == 0 &&
        (strcmp(request->path, "/v1/catalog/tools") == 0 ||
         strcmp(request->path, "/v1/catalog/skills") == 0)) {
        return handle_catalog(app, request->path, response);
    }
    if (strcmp(request->method, "GET") == 0 &&
        strncmp(request->path, device_prefix, strlen(device_prefix)) == 0) {
        smart_home_state_t state;
        int id = atoi(request->path + strlen(device_prefix));
        int i;

        if (id <= 0 || smart_home_device_service_copy_state(
                           &app->device_service, &state) != AGENT_OK) {
            return response_error(response, 400, "invalid_device_id");
        }
        for (i = 0; i < SMART_HOME_MAX_DEVICES; i++) {
            const smart_home_device_t *device = &state.devices[i];

            if (!device->used || device->id != id) {
                continue;
            }
            response->status_code = 200;
            if (device->type == SMART_HOME_DEVICE_AC) {
                snprintf(response->body, sizeof(response->body),
                         "{\"id\":\"%d\",\"room\":\"%s\","
                         "\"name\":\"%s\",\"type\":\"ac\","
                         "\"online\":true,\"state\":{\"on\":%s,"
                         "\"temperature\":%d,\"mode\":\"%s\","
                         "\"fanSpeed\":\"%s\"}}",
                         device->id, device->room, device->name,
                         device->on ? "true" : "false", device->temperature,
                         smart_home_ac_mode_name(device->ac_mode),
                         smart_home_ac_fan_speed_name(device->ac_fan_speed));
            } else {
                snprintf(response->body, sizeof(response->body),
                         "{\"id\":\"%d\",\"room\":\"%s\","
                         "\"name\":\"%s\",\"type\":\"light\","
                         "\"online\":true,\"state\":{\"on\":%s,"
                         "\"brightness\":%d}}",
                         device->id, device->room, device->name,
                         device->on ? "true" : "false", device->brightness);
            }
            return AGENT_OK;
        }
        return response_error(response, 404, "device_not_found");
    }
    return response_error(response, 404, "route_not_found");
}
