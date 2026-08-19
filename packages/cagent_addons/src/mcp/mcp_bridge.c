/* SPDX-License-Identifier: Apache-2.0 */
/** Standard MCP Streamable HTTP tools-only client. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/mcp_bridge.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cagent_addons/cjson_compat.h"
#include "cagent_addons/errors.h"
#include "cagent_addons/limits.h"

typedef struct {
    char name[CAGENT_ADDONS_COMMAND_NAME_SIZE + 1u];
    caddons_tool_risk_t risk;
    uint32_t timeout_ms;
} mcp_policy_t;

struct caddons_mcp_bridge {
    pthread_mutex_t mutex;
    caddons_remote_catalog_t *catalog;
    caddons_http_request_fn http_request;
    void *http_context;
    caddons_mcp_catalog_changed_fn catalog_changed_fn;
    void *catalog_changed_user_data;
    char host[CAGENT_ADDONS_MCP_HOST_SIZE + 1u];
    char path[CAGENT_ADDONS_MCP_PATH_SIZE + 1u];
    char server_id[CAGENT_ADDONS_MCP_SERVER_ID_SIZE + 1u];
    char protocol_version[CAGENT_ADDONS_MCP_PROTOCOL_VERSION_SIZE + 1u];
    char authorization[CAGENT_ADDONS_MCP_AUTHORIZATION_SIZE + 1u];
    char extra_headers[CAGENT_ADDONS_MCP_EXTRA_HEADERS_SIZE + 1u];
    char session_id[CAGENT_ADDONS_MCP_SESSION_ID_SIZE + 1u];
    uint16_t port;
    bool use_tls;
    uint32_t request_timeout_ms;
    uint64_t generation;
    uint64_t next_request_id;
    bool started;
    mcp_policy_t policies[CAGENT_ADDONS_MAX_MCP_TOOLS];
    size_t policy_count;
    char response_body[CAGENT_ADDONS_MCP_RESPONSE_BUF_SIZE + 1u];
};

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int copy_string(char *destination, size_t capacity, const char *source)
{
    size_t length;
    if (!destination || !capacity || !source) return CADDONS_ERR_INVALID;
    length = strlen(source);
    if (!length || length >= capacity) return CADDONS_ERR_LIMIT;
    memcpy(destination, source, length + 1u);
    return CADDONS_OK;
}

static cJSON *parse_json_exact(const char *json)
{
    const char *end = NULL;
    cJSON *value;
    size_t length;
    if (!json) return NULL;
    length = strlen(json);
    value = cJSON_ParseWithLengthOpts(json, length, &end, 0);
    while (value && end && end < json + length && isspace((unsigned char)*end)) end++;
    if (!value || end != json + length) {
        cJSON_Delete(value);
        return NULL;
    }
    return value;
}

static bool content_type_is_json(const char *content_type)
{
    return content_type && strncmp(content_type, "application/json", 16u) == 0;
}

/* 提取 SSE 单帧中的 JSON（"data: {...}\n\n" 或 "data:{...}"）。
 * 仅支持单 data 块；多块/流式返回 CADDONS_ERR_UNSUPPORTED。 */
static int parse_sse_json(const char *body, size_t length, cJSON **json_out)
{
    const char *data;
    const char *end;
    const char *json_start;
    size_t json_len;
    cJSON *value;

    if (!body || !json_out) {
        return CADDONS_ERR_INVALID;
    }
    *json_out = NULL;

    /* 找第一个 "data:" 前缀行 */
    data = strstr(body, "data:");
    if (!data) {
        return CADDONS_ERR_UNSUPPORTED;
    }
    data += 5; /* 跳过 "data:" */
    while (*data == ' ') {
        data++;
    }
    json_start = data;

    /* data 行到行尾（\n 或 \r\n 或字符串尾） */
    end = data;
    while (*end && *end != '\n' && *end != '\r') {
        end++;
    }
    json_len = (size_t)(end - data);

    /* 后续还有非空 data 行 → 多帧流式，不支持 */
    if (strstr(end, "data:")) {
        return CADDONS_ERR_UNSUPPORTED;
    }

    value = cJSON_ParseWithLength(json_start, json_len);
    if (!value) {
        return CADDONS_ERR_PARSE;
    }
    *json_out = value;
    return CADDONS_OK;
}

static const mcp_policy_t *find_policy(const caddons_mcp_bridge_t *bridge,
                                       const char *name)
{
    size_t i;
    for (i = 0; i < bridge->policy_count; i++)
        if (strcmp(bridge->policies[i].name, name) == 0) return &bridge->policies[i];
    return NULL;
}

static int make_public_name(const caddons_mcp_bridge_t *bridge, const char *tool_name,
                            char *name, size_t name_size)
{
    const char *parts[3] = {"mcp", bridge->server_id, tool_name};
    size_t i;
    size_t j;
    size_t used = 0;
    if (!tool_name || !tool_name[0]) return CADDONS_ERR_INVALID;
    for (i = 0; i < 3u; i++) {
        if (i && used + 1u >= name_size) return CADDONS_ERR_LIMIT;
        if (i) name[used++] = '_';
        for (j = 0; parts[i][j]; j++) {
            unsigned char value = (unsigned char)parts[i][j];
            if (used + 1u >= name_size) return CADDONS_ERR_LIMIT;
            name[used++] = (isalnum(value) || value == '_' || value == '-')
                             ? (char)value : '_';
        }
    }
    name[used] = '\0';
    return CADDONS_OK;
}

/* Responses to JSON-RPC requests must be JSON.  Notifications are permitted
 * to receive an empty 2xx response (for example HTTP 202 Accepted). */
static int http_request(caddons_mcp_bridge_t *bridge, const char *method,
                        const char *body, uint64_t deadline_ms,
                        bool require_json, cJSON **json_out)
{
    caddons_http_request_t request;
    caddons_http_response_t response;
    int rc;
    pthread_mutex_lock(&bridge->mutex);
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    bridge->response_body[0] = '\0';
    request.method = method;
    request.host = bridge->host;
    request.is_tls = bridge->use_tls;
    request.port = bridge->port;
    request.path = bridge->path;
    request.authorization = bridge->authorization[0] ? bridge->authorization : NULL;
    request.extra_headers = bridge->extra_headers[0] ? bridge->extra_headers : NULL;
    request.mcp_protocol_version = bridge->started ? bridge->protocol_version : NULL;
    request.mcp_session_id = bridge->session_id[0] ? bridge->session_id : NULL;
    request.request_body = body;
    request.deadline_ms = deadline_ms;
    response.response_body = bridge->response_body;
    response.response_size = sizeof(bridge->response_body);
    rc = bridge->http_request(bridge->http_context, &request, &response);
    if (rc != CADDONS_OK) goto out;
    if (response.response_len >= response.response_size) {
        rc = CADDONS_ERR_LIMIT;
        goto out;
    }
    response.response_body[response.response_len] = '\0';
    if (response.mcp_session_id && response.mcp_session_id[0]) {
        rc = copy_string(bridge->session_id, sizeof(bridge->session_id),
                         response.mcp_session_id);
        if (rc != CADDONS_OK) goto out;
    }
    if (response.http_status == 401 || response.http_status == 403) {
        rc = CADDONS_ERR_DENIED;
        goto out;
    }
    if (response.http_status == 404) {
        rc = CADDONS_ERR_NOT_FOUND;
        goto out;
    }
    if (response.http_status < 200 || response.http_status >= 300) {
        rc = CADDONS_ERR_NETWORK;
        goto out;
    }
    if (!require_json) {
        rc = CADDONS_OK;
        goto out;
    }
    if (response.content_type && strncmp(response.content_type,
                                         "text/event-stream", 17u) == 0)
        rc = parse_sse_json(response.response_body, response.response_len,
                            json_out);
    else if (!content_type_is_json(response.content_type))
        rc = CADDONS_ERR_PROTOCOL;
    else {
        *json_out = parse_json_exact(response.response_body);
        rc = *json_out ? CADDONS_OK : CADDONS_ERR_PARSE;
    }
out:
    pthread_mutex_unlock(&bridge->mutex);
    return rc;
}

static int serialize_request(const char *id, const char *method, cJSON *params,
                             char **request_out)
{
    cJSON *root;
    if (!request_out) {
        cJSON_Delete(params);
        return CADDONS_ERR_INVALID;
    }
    *request_out = NULL;
    root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(params);
        return CADDONS_ERR_NOMEM;
    }
    if (!cJSON_AddStringToObject(root, "jsonrpc", "2.0")
        || !cJSON_AddStringToObject(root, "id", id)
        || !cJSON_AddStringToObject(root, "method", method)) {
        cJSON_Delete(root);
        cJSON_Delete(params);
        return CADDONS_ERR_NOMEM;
    }
    if (params && !cJSON_AddItemToObject(root, "params", params)) {
        cJSON_Delete(root);
        cJSON_Delete(params);
        return CADDONS_ERR_NOMEM;
    }
    *request_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *request_out ? CADDONS_OK : CADDONS_ERR_NOMEM;
}

static int serialize_initialized_notification(char **request_out)
{
    cJSON *root = cJSON_CreateObject();
    if (!request_out || !root) {
        cJSON_Delete(root);
        return CADDONS_ERR_NOMEM;
    }
    *request_out = NULL;
    if (!cJSON_AddStringToObject(root, "jsonrpc", "2.0")
        || !cJSON_AddStringToObject(root, "method", "notifications/initialized")) {
        cJSON_Delete(root);
        return CADDONS_ERR_NOMEM;
    }
    *request_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *request_out ? CADDONS_OK : CADDONS_ERR_NOMEM;
}

static int validate_response(cJSON *root, const char *id, cJSON **result_out)
{
    cJSON *jsonrpc;
    cJSON *response_id;
    cJSON *error;
    cJSON *result;
    if (!root || !cJSON_IsObject(root)) return CADDONS_ERR_PARSE;
    jsonrpc = cJSON_GetObjectItemCaseSensitive(root, "jsonrpc");
    response_id = cJSON_GetObjectItemCaseSensitive(root, "id");
    error = cJSON_GetObjectItemCaseSensitive(root, "error");
    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0
        || !cJSON_IsString(response_id) || strcmp(response_id->valuestring, id) != 0)
        return CADDONS_ERR_PROTOCOL;
    if (error) return CADDONS_ERR_PROTOCOL;
    if (!result) return CADDONS_ERR_PARSE;
    *result_out = result;
    return CADDONS_OK;
}

static int initialize(caddons_mcp_bridge_t *bridge, uint64_t deadline_ms);

static int request_rpc(caddons_mcp_bridge_t *bridge, const char *method,
                       cJSON *params, uint64_t deadline_ms, cJSON **result_out)
{
    char id[CAGENT_ADDONS_FRAME_ID_SIZE + 1u];
    char *request = NULL;
    cJSON *root = NULL;
    cJSON *result;
    bool retry_after_session_reset = false;
    int rc;
    unsigned long long sequence;
    pthread_mutex_lock(&bridge->mutex);
    sequence = (unsigned long long)++bridge->next_request_id;
    pthread_mutex_unlock(&bridge->mutex);
    if (snprintf(id, sizeof(id), "mcp-%llu", sequence) < 0) {
        cJSON_Delete(params);
        return CADDONS_ERR_INTERNAL;
    }
    rc = serialize_request(id, method, params, &request);
    if (rc != CADDONS_OK) return rc;
retry:
    rc = http_request(bridge, "POST", request, deadline_ms, true, &root);
    if (rc == CADDONS_ERR_NOT_FOUND && bridge->session_id[0]) {
        pthread_mutex_lock(&bridge->mutex);
        bridge->session_id[0] = '\0';
        bridge->started = false;
        pthread_mutex_unlock(&bridge->mutex);
        cJSON_Delete(root);
        root = NULL;
        if (retry_after_session_reset) {
            cJSON_free(request);
            return CADDONS_ERR_OFFLINE;
        }
        retry_after_session_reset = true;
        rc = initialize(bridge, deadline_ms);
        if (rc != CADDONS_OK) {
            cJSON_free(request);
            return rc;
        }
        goto retry;
    }
    cJSON_free(request);
    if (rc != CADDONS_OK) {
        cJSON_Delete(root);
        return rc;
    }
    rc = validate_response(root, id, &result);
    if (rc == CADDONS_OK && result_out) {
        *result_out = cJSON_Duplicate(result, 1);
        if (!*result_out) rc = CADDONS_ERR_NOMEM;
    }
    cJSON_Delete(root);
    return rc;
}

static int send_initialized(caddons_mcp_bridge_t *bridge, uint64_t deadline_ms)
{
    char *request = NULL;
    int rc = serialize_initialized_notification(&request);
    if (rc != CADDONS_OK) return rc;
    rc = http_request(bridge, "POST", request, deadline_ms, false, NULL);
    cJSON_free(request);
    return rc;
}

static int initialize(caddons_mcp_bridge_t *bridge, uint64_t deadline_ms)
{
    cJSON *params = cJSON_CreateObject();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *client_info = cJSON_CreateObject();
    cJSON *result = NULL;
    cJSON *version;
    int rc;
    if (!params || !capabilities || !client_info
        || !cJSON_AddStringToObject(params, "protocolVersion", bridge->protocol_version)
        || !cJSON_AddItemToObject(params, "capabilities", capabilities)) {
        cJSON_Delete(params);
        cJSON_Delete(capabilities);
        cJSON_Delete(client_info);
        return CADDONS_ERR_NOMEM;
    }
    capabilities = NULL;
    if (!cJSON_AddStringToObject(client_info, "name", "cagent-addons")
        || !cJSON_AddStringToObject(client_info, "version", "1")
        || !cJSON_AddItemToObject(params, "clientInfo", client_info)) {
        cJSON_Delete(params);
        cJSON_Delete(client_info);
        return CADDONS_ERR_NOMEM;
    }
    client_info = NULL;
    rc = request_rpc(bridge, "initialize", params, deadline_ms, &result);
    if (rc != CADDONS_OK) return rc;
    version = cJSON_GetObjectItemCaseSensitive(result, "protocolVersion");
    if (!cJSON_IsString(version) || strcmp(version->valuestring, bridge->protocol_version) != 0) {
        cJSON_Delete(result);
        return CADDONS_ERR_VERSION;
    }
    cJSON_Delete(result);
    pthread_mutex_lock(&bridge->mutex);
    bridge->started = true;
    pthread_mutex_unlock(&bridge->mutex);
    return send_initialized(bridge, deadline_ms);
}

static int mcp_execute(void *backend_context, const char *route_id,
                       uint64_t connection_gen, const char *arguments_json,
                       uint64_t deadline_ms, char *result_json, size_t result_size)
{
    caddons_mcp_bridge_t *bridge = backend_context;
    const char *name;
    cJSON *arguments;
    cJSON *params;
    cJSON *result = NULL;
    char *serialized;
    int rc;
    if (!bridge || !route_id || !arguments_json || !result_json || !result_size)
        return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&bridge->mutex);
    if (!bridge->started || connection_gen != bridge->generation) {
        pthread_mutex_unlock(&bridge->mutex);
        return CADDONS_ERR_OFFLINE;
    }
    pthread_mutex_unlock(&bridge->mutex);
    name = strchr(route_id, ':');
    if (!name || !name[1]
        || (size_t)(name - route_id) != strlen(bridge->server_id)
        || strncmp(route_id, bridge->server_id,
                   (size_t)(name - route_id)) != 0)
        return CADDONS_ERR_INVALID;
    name++;
    arguments = parse_json_exact(arguments_json);
    if (!arguments || !cJSON_IsObject(arguments)) {
        cJSON_Delete(arguments);
        return CADDONS_ERR_PARSE;
    }
    params = cJSON_CreateObject();
    if (!params || !cJSON_AddStringToObject(params, "name", name)
        || !cJSON_AddItemToObject(params, "arguments", arguments)) {
        cJSON_Delete(params);
        cJSON_Delete(arguments);
        return CADDONS_ERR_NOMEM;
    }
    rc = request_rpc(bridge, "tools/call", params, deadline_ms, &result);
    if (rc != CADDONS_OK) return rc;
    serialized = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if (!serialized) return CADDONS_ERR_NOMEM;
    if (strlen(serialized) >= result_size) {
        cJSON_free(serialized);
        return CADDONS_ERR_LIMIT;
    }
    strcpy(result_json, serialized);
    cJSON_free(serialized);
    return CADDONS_OK;
}

static int import_tools(caddons_mcp_bridge_t *bridge, cJSON *result,
                        uint64_t generation)
{
    cJSON *tools;
    cJSON *tool;
    int imported = 0;
    tools = cJSON_GetObjectItemCaseSensitive(result, "tools");
    if (!cJSON_IsArray(tools)) return CADDONS_ERR_PARSE;
    cJSON_ArrayForEach(tool, tools) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(tool, "name");
        cJSON *description = cJSON_GetObjectItemCaseSensitive(tool, "description");
        cJSON *schema = cJSON_GetObjectItemCaseSensitive(tool, "inputSchema");
        const mcp_policy_t *policy;
        caddons_remote_descriptor_t descriptor;
        char *schema_json;
        int rc;
        if (!cJSON_IsObject(tool) || !cJSON_IsString(name) || !cJSON_IsObject(schema))
            return CADDONS_ERR_PARSE;
        policy = find_policy(bridge, name->valuestring);
        if (!policy) continue;
        if (imported >= CAGENT_ADDONS_MAX_MCP_TOOLS) return CADDONS_ERR_LIMIT;
        memset(&descriptor, 0, sizeof(descriptor));
        if (snprintf(descriptor.route_id, sizeof(descriptor.route_id), "%s:%s",
                     bridge->server_id, name->valuestring) < 0
            || strlen(descriptor.route_id) >= sizeof(descriptor.route_id)
            || copy_string(descriptor.source_name, sizeof(descriptor.source_name),
                           name->valuestring) != CADDONS_OK
            || make_public_name(bridge, name->valuestring, descriptor.public_name,
                                sizeof(descriptor.public_name)) != CADDONS_OK)
            return CADDONS_ERR_LIMIT;
        if (cJSON_IsString(description) && description->valuestring[0]) {
            rc = copy_string(descriptor.description, sizeof(descriptor.description),
                             description->valuestring);
        } else {
            rc = copy_string(descriptor.description, sizeof(descriptor.description),
                             "MCP remote tool.");
        }
        if (rc != CADDONS_OK) return rc;
        schema_json = cJSON_PrintUnformatted(schema);
        if (!schema_json) return CADDONS_ERR_NOMEM;
        rc = copy_string(descriptor.input_schema_json,
                         sizeof(descriptor.input_schema_json), schema_json);
        cJSON_free(schema_json);
        if (rc != CADDONS_OK) return rc;
        descriptor.origin = CADDONS_TOOL_MCP;
        descriptor.risk = policy->risk;
        descriptor.timeout_ms = policy->timeout_ms ? policy->timeout_ms
                                                    : bridge->request_timeout_ms;
        descriptor.connection_gen = generation;
        descriptor.execute = mcp_execute;
        descriptor.backend_context = bridge;
        rc = caddons_remote_catalog_discover(bridge->catalog, &descriptor);
        if (rc != CADDONS_OK) return rc;
        imported++;
    }
    return CADDONS_OK;
}

caddons_mcp_bridge_t *caddons_mcp_bridge_create(
    const caddons_mcp_bridge_config_t *config)
{
    caddons_mcp_bridge_t *bridge;
    size_t i;
    if (!config || !config->host || !config->path || !config->server_id
        || !config->catalog || !config->http_request || !config->tool_policies
        || !config->tool_policy_count || config->tool_policy_count > CAGENT_ADDONS_MAX_MCP_TOOLS
        || !config->port || !config->request_timeout_ms) return NULL;
    bridge = calloc(1, sizeof(*bridge));
    if (!bridge) return NULL;
    if (pthread_mutex_init(&bridge->mutex, NULL) != 0) {
        free(bridge);
        return NULL;
    }
    bridge->use_tls = config->use_tls;
    if (copy_string(bridge->host, sizeof(bridge->host), config->host) != CADDONS_OK
        || copy_string(bridge->path, sizeof(bridge->path), config->path) != CADDONS_OK
        || copy_string(bridge->server_id, sizeof(bridge->server_id), config->server_id) != CADDONS_OK
        || copy_string(bridge->protocol_version, sizeof(bridge->protocol_version),
                       config->protocol_version ? config->protocol_version
                                                : CAGENT_ADDONS_MCP_PROTOCOL_VERSION) != CADDONS_OK
        || (config->authorization && copy_string(bridge->authorization,
                                                  sizeof(bridge->authorization),
                                                  config->authorization) != CADDONS_OK)
        || (config->extra_headers && copy_string(bridge->extra_headers,
                                                  sizeof(bridge->extra_headers),
                                                  config->extra_headers) != CADDONS_OK)) {
        caddons_mcp_bridge_destroy(bridge);
        return NULL;
    }
    for (i = 0; i < config->tool_policy_count; i++) {
        if (!config->tool_policies[i].name || !config->tool_policies[i].name[0]
            || copy_string(bridge->policies[i].name, sizeof(bridge->policies[i].name),
                           config->tool_policies[i].name) != CADDONS_OK) {
            caddons_mcp_bridge_destroy(bridge);
            return NULL;
        }
        bridge->policies[i].risk = config->tool_policies[i].risk;
        bridge->policies[i].timeout_ms = config->tool_policies[i].timeout_ms;
    }
    bridge->catalog = config->catalog;
    bridge->http_request = config->http_request;
    bridge->http_context = config->http_context;
    bridge->catalog_changed_fn = config->catalog_changed_fn;
    bridge->catalog_changed_user_data = config->catalog_changed_user_data;
    bridge->port = config->port;
    bridge->request_timeout_ms = config->request_timeout_ms;
    bridge->policy_count = config->tool_policy_count;
    return bridge;
}

int caddons_mcp_bridge_start(caddons_mcp_bridge_t *bridge)
{
    uint64_t deadline;
    int rc;
    if (!bridge) return CADDONS_ERR_INVALID;
    deadline = monotonic_ms() + bridge->request_timeout_ms;
    rc = initialize(bridge, deadline);
    if (rc != CADDONS_OK) return rc;
    return caddons_mcp_bridge_refresh(bridge, deadline);
}

int caddons_mcp_bridge_refresh(caddons_mcp_bridge_t *bridge, uint64_t deadline_ms)
{
    cJSON *result = NULL;
    char prefix[CAGENT_ADDONS_MCP_SERVER_ID_SIZE + 2u];
    uint64_t old_generation;
    uint64_t generation;
    int rc;
    if (!bridge || !deadline_ms) return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&bridge->mutex);
    if (!bridge->started) {
        pthread_mutex_unlock(&bridge->mutex);
        return CADDONS_ERR_OFFLINE;
    }
    old_generation = bridge->generation;
    generation = old_generation + 1u;
    pthread_mutex_unlock(&bridge->mutex);
    rc = request_rpc(bridge, "tools/list", cJSON_CreateObject(), deadline_ms, &result);
    if (rc != CADDONS_OK) return rc;
    if (old_generation) {
        if (snprintf(prefix, sizeof(prefix), "%s:", bridge->server_id) < 0) {
            cJSON_Delete(result);
            return CADDONS_ERR_INTERNAL;
        }
        rc = caddons_remote_catalog_remove_source(bridge->catalog, CADDONS_TOOL_MCP,
                                                  prefix, old_generation);
        if (rc != CADDONS_OK && rc != CADDONS_ERR_NOT_FOUND) {
            cJSON_Delete(result);
            return rc;
        }
    }
    rc = import_tools(bridge, result, generation);
    cJSON_Delete(result);
    if (rc != CADDONS_OK) return rc;
    pthread_mutex_lock(&bridge->mutex);
    bridge->generation = generation;
    pthread_mutex_unlock(&bridge->mutex);
    /* 通知应用层：catalog 有新 mutation 待消费（唤醒其 worker） */
    if (bridge->catalog_changed_fn) {
        bridge->catalog_changed_fn(bridge->catalog_changed_user_data);
    }
    return CADDONS_OK;
}

int caddons_mcp_bridge_stop(caddons_mcp_bridge_t *bridge)
{
    char prefix[CAGENT_ADDONS_MCP_SERVER_ID_SIZE + 2u];
    uint64_t generation;
    int rc = CADDONS_OK;

    if (!bridge) return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&bridge->mutex);
    bridge->started = false;
    bridge->session_id[0] = '\0';
    generation = bridge->generation;
    bridge->generation = 0;
    pthread_mutex_unlock(&bridge->mutex);
    if (generation) {
        if (snprintf(prefix, sizeof(prefix), "%s:", bridge->server_id) < 0)
            return CADDONS_ERR_INTERNAL;
        rc = caddons_remote_catalog_remove_source(bridge->catalog, CADDONS_TOOL_MCP,
                                                  prefix, generation);
        if (rc == CADDONS_ERR_NOT_FOUND) rc = CADDONS_OK;
    }
    return rc;
}

void caddons_mcp_bridge_destroy(caddons_mcp_bridge_t *bridge)
{
    if (!bridge) return;
    pthread_mutex_destroy(&bridge->mutex);
    free(bridge);
}
