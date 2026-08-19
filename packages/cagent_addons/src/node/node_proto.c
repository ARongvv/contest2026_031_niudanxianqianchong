/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/node_proto.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "cagent_addons/cjson_compat.h"

#define DEFAULT_SCHEMA "{\"type\":\"object\",\"properties\":{}}"
#define DEFAULT_TOOL_TIMEOUT_MS 8000u

static int copy_string(char *output, size_t output_size, const char *input)
{
    size_t len;
    if (!output || output_size == 0 || !input) return CADDONS_ERR_INVALID;
    len = strlen(input);
    if (len >= output_size) return CADDONS_ERR_LIMIT;
    memcpy(output, input, len + 1);
    return CADDONS_OK;
}

static cJSON *parse_json_exact(const char *json, size_t json_len)
{
    const char *end = NULL;
    cJSON *item;
    if (!json || json_len == 0) return NULL;
    item = cJSON_ParseWithLengthOpts(json, json_len, &end, 0);
    if (!item) return NULL;
    while (end < json + json_len && isspace((unsigned char)*end)) end++;
    if (end != json + json_len) {
        cJSON_Delete(item);
        return NULL;
    }
    return item;
}

static int print_json(const cJSON *item, char *output, size_t output_size,
                      size_t *written)
{
    size_t len;
    if (!item || !output || output_size == 0 || output_size > INT_MAX)
        return CADDONS_ERR_INVALID;
    if (!cJSON_PrintPreallocated((cJSON *)item, output, (int)output_size, 0))
        return CADDONS_ERR_LIMIT;
    len = strlen(output);
    if (written) *written = len;
    return CADDONS_OK;
}

static int json_item_to_buffer(const cJSON *item, char *output, size_t output_size)
{
    if (!item) return copy_string(output, output_size, "{}");
    return print_json(item, output, output_size, NULL);
}

int caddons_node_decode(const char *json, size_t json_len,
                        caddons_node_frame_t *frame)
{
    cJSON *root;
    cJSON *body;
    const char *type;
    const char *value;
    int rc = CADDONS_ERR_PARSE;
    if (!json || !frame || json_len == 0 || json_len > CAGENT_ADDONS_RECV_BUF_SIZE)
        return CADDONS_ERR_INVALID;
    memset(frame, 0, sizeof(*frame));
    root = parse_json_exact(json, json_len);
    if (!root || !cJSON_IsObject(root)) goto out;
    type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "type"));
    if (!type) goto out;
    if (strcmp(type, "evt") == 0 || strcmp(type, "event") == 0) {
        frame->type = CADDONS_NODE_FRAME_EVENT;
        value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "event"));
        body = cJSON_GetObjectItemCaseSensitive(root, "payload");
    } else if (strcmp(type, "req") == 0) {
        frame->type = CADDONS_NODE_FRAME_REQUEST;
        value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "method"));
        body = cJSON_GetObjectItemCaseSensitive(root, "params");
        value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "method"));
        {
            const char *id = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(root, "id"));
            if (!id || !value) goto out;
            rc = copy_string(frame->id, sizeof(frame->id), id);
            if (rc != CADDONS_OK) goto out;
        }
    } else if (strcmp(type, "res") == 0) {
        cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
        frame->type = CADDONS_NODE_FRAME_RESPONSE;
        value = "";
        {
            const char *id = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(root, "id"));
            if (!id || !cJSON_IsBool(ok)) goto out;
            rc = copy_string(frame->id, sizeof(frame->id), id);
            if (rc != CADDONS_OK) goto out;
        }
        frame->ok = cJSON_IsTrue(ok);
        body = cJSON_GetObjectItemCaseSensitive(root, frame->ok ? "payload" : "error");
    } else {
        rc = CADDONS_ERR_PROTOCOL; goto out;
    }
    if (!value) goto out;
    rc = copy_string(frame->name, sizeof(frame->name), value);
    if (rc != CADDONS_OK) goto out;
    rc = json_item_to_buffer(body, frame->body_json, sizeof(frame->body_json));
out:
    cJSON_Delete(root);
    return rc;
}

int caddons_node_encode(const caddons_node_frame_t *frame, char *output,
                        size_t output_size, size_t *written)
{
    cJSON *root = NULL;
    cJSON *body = NULL;
    const char *body_text;
    int rc = CADDONS_ERR_NOMEM;
    if (!frame || !output || !written) return CADDONS_ERR_INVALID;
    body_text = frame->body_json[0] ? frame->body_json : "{}";
    body = parse_json_exact(body_text, strlen(body_text));
    if (!body) return CADDONS_ERR_PARSE;
    root = cJSON_CreateObject();
    if (!root) goto out;
    if (frame->type == CADDONS_NODE_FRAME_EVENT) {
        if (!frame->name[0]) { rc = CADDONS_ERR_INVALID; goto out; }
        cJSON_AddStringToObject(root, "type", "evt");
        cJSON_AddStringToObject(root, "event", frame->name);
        cJSON_AddItemToObject(root, "payload", body); body = NULL;
    } else if (frame->type == CADDONS_NODE_FRAME_REQUEST) {
        if (!frame->id[0] || !frame->name[0]) { rc = CADDONS_ERR_INVALID; goto out; }
        cJSON_AddStringToObject(root, "type", "req");
        cJSON_AddStringToObject(root, "id", frame->id);
        cJSON_AddStringToObject(root, "method", frame->name);
        cJSON_AddItemToObject(root, "params", body); body = NULL;
    } else if (frame->type == CADDONS_NODE_FRAME_RESPONSE) {
        if (!frame->id[0]) { rc = CADDONS_ERR_INVALID; goto out; }
        cJSON_AddStringToObject(root, "type", "res");
        cJSON_AddStringToObject(root, "id", frame->id);
        cJSON_AddBoolToObject(root, "ok", frame->ok);
        cJSON_AddItemToObject(root, frame->ok ? "payload" : "error", body); body = NULL;
    } else {
        rc = CADDONS_ERR_INVALID; goto out;
    }
    rc = print_json(root, output, output_size, written);
out:
    cJSON_Delete(body);
    cJSON_Delete(root);
    return rc;
}

static caddons_tool_risk_t parse_risk(const char *risk)
{
    if (!risk) return CADDONS_TOOL_RISK_UNKNOWN;
    if (strcmp(risk, "read_only") == 0) return CADDONS_TOOL_RISK_READ_ONLY;
    if (strcmp(risk, "side_effect") == 0) return CADDONS_TOOL_RISK_SIDE_EFFECT;
    if (strcmp(risk, "dangerous") == 0) return CADDONS_TOOL_RISK_DANGEROUS;
    return CADDONS_TOOL_RISK_UNKNOWN;
}

const char *caddons_tool_risk_name(caddons_tool_risk_t risk)
{
    switch (risk) {
    case CADDONS_TOOL_RISK_READ_ONLY: return "read_only";
    case CADDONS_TOOL_RISK_SIDE_EFFECT: return "side_effect";
    case CADDONS_TOOL_RISK_DANGEROUS: return "dangerous";
    default: return "unknown";
    }
}

static int init_command(caddons_node_command_t *command, const char *name)
{
    int len;
    int rc = copy_string(command->command, sizeof(command->command), name);
    if (rc != CADDONS_OK || !name[0]) return rc == CADDONS_OK ? CADDONS_ERR_INVALID : rc;
    len = snprintf(command->description, sizeof(command->description),
                   "Remote command %s", name);
    if (len < 0 || (size_t)len >= sizeof(command->description)) return CADDONS_ERR_LIMIT;
    memcpy(command->input_schema_json, DEFAULT_SCHEMA, sizeof(DEFAULT_SCHEMA));
    command->risk = CADDONS_TOOL_RISK_UNKNOWN;
    command->timeout_ms = DEFAULT_TOOL_TIMEOUT_MS;
    return CADDONS_OK;
}

static caddons_node_command_t *find_command(caddons_node_connect_t *connect,
                                             const char *name)
{
    size_t i;
    for (i = 0; i < connect->command_count; i++) {
        if (strcmp(connect->commands[i].command, name) == 0)
            return &connect->commands[i];
    }
    return NULL;
}

static int parse_required_string(cJSON *object, const char *key,
                                 char *output, size_t output_size)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    if (!value || !value[0]) return CADDONS_ERR_PARSE;
    return copy_string(output, output_size, value);
}

static int parse_optional_string(cJSON *object, const char *key,
                                 char *output, size_t output_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) { output[0] = '\0'; return CADDONS_OK; }
    if (!cJSON_IsString(item)) return CADDONS_ERR_PARSE;
    return copy_string(output, output_size, item->valuestring);
}

int caddons_node_parse_connect(const caddons_node_frame_t *frame,
                               caddons_node_connect_t *connect)
{
    cJSON *params = NULL;
    cJSON *client;
    cJSON *commands;
    cJSON *meta;
    cJSON *item;
    cJSON *min_protocol;
    cJSON *max_protocol;
    int rc = CADDONS_ERR_PARSE;
    if (!frame || !connect || frame->type != CADDONS_NODE_FRAME_REQUEST
        || strcmp(frame->name, "connect") != 0) return CADDONS_ERR_INVALID;
    memset(connect, 0, sizeof(*connect));
    params = parse_json_exact(frame->body_json, strlen(frame->body_json));
    if (!params || !cJSON_IsObject(params)) goto out;
    min_protocol = cJSON_GetObjectItemCaseSensitive(params, "minProtocol");
    max_protocol = cJSON_GetObjectItemCaseSensitive(params, "maxProtocol");
    if (!cJSON_IsNumber(min_protocol) || !cJSON_IsNumber(max_protocol)
        || min_protocol->valuedouble < 0 || max_protocol->valuedouble < 0
        || min_protocol->valuedouble > UINT32_MAX
        || max_protocol->valuedouble > UINT32_MAX
        || min_protocol->valuedouble != (double)(uint32_t)min_protocol->valuedouble
        || max_protocol->valuedouble != (double)(uint32_t)max_protocol->valuedouble) goto out;
    connect->min_protocol = (uint32_t)min_protocol->valuedouble;
    connect->max_protocol = (uint32_t)max_protocol->valuedouble;
    if (connect->min_protocol > CAGENT_ADDONS_NODE_PROTOCOL_VERSION
        || connect->max_protocol < CAGENT_ADDONS_NODE_PROTOCOL_VERSION) {
        rc = CADDONS_ERR_VERSION; goto out;
    }
    client = cJSON_GetObjectItemCaseSensitive(params, "client");
    if (!cJSON_IsObject(client)) goto out;
    if ((rc = parse_required_string(client, "id", connect->node_id,
                                    sizeof(connect->node_id))) != CADDONS_OK) goto out;
    if ((rc = parse_optional_string(client, "displayName", connect->display_name,
                                    sizeof(connect->display_name))) != CADDONS_OK) goto out;
    if ((rc = parse_optional_string(client, "version", connect->version,
                                    sizeof(connect->version))) != CADDONS_OK) goto out;
    if ((rc = parse_optional_string(client, "platform", connect->platform,
                                    sizeof(connect->platform))) != CADDONS_OK) goto out;
    if ((rc = parse_optional_string(client, "deviceFamily", connect->device_family,
                                    sizeof(connect->device_family))) != CADDONS_OK) goto out;
    if ((rc = parse_optional_string(client, "mode", connect->mode,
                                    sizeof(connect->mode))) != CADDONS_OK) goto out;
    if ((rc = parse_required_string(params, "role", connect->role,
                                    sizeof(connect->role))) != CADDONS_OK) goto out;
    if (strcmp(connect->role, "node") != 0) { rc = CADDONS_ERR_PROTOCOL; goto out; }
    commands = cJSON_GetObjectItemCaseSensitive(params, "commands");
    if (!cJSON_IsArray(commands)) goto out;
    cJSON_ArrayForEach(item, commands) {
        const char *name;
        if (!cJSON_IsString(item)) goto out;
        name = cJSON_GetStringValue(item);
        if (connect->command_count >= CAGENT_ADDONS_MAX_NODE_COMMANDS) {
            rc = CADDONS_ERR_LIMIT; goto out;
        }
        if (find_command(connect, name)) { rc = CADDONS_ERR_PROTOCOL; goto out; }
        rc = init_command(&connect->commands[connect->command_count], name);
        if (rc != CADDONS_OK) goto out;
        connect->command_count++;
    }
    meta = cJSON_GetObjectItemCaseSensitive(params, "toolMeta");
    if (meta && !cJSON_IsArray(meta)) goto out;
    cJSON_ArrayForEach(item, meta) {
        const char *name;
        caddons_node_command_t *command;
        cJSON *schema;
        cJSON *timeout;
        const char *description;
        if (!cJSON_IsObject(item)) continue;
        name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "command"));
        command = name ? find_command(connect, name) : NULL;
        if (!command) continue; /* Forward-compatible metadata for unknown commands. */
        description = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "description"));
        if (description && (rc = copy_string(command->description,
                                             sizeof(command->description), description)) != CADDONS_OK) goto out;
        schema = cJSON_GetObjectItemCaseSensitive(item, "inputSchema");
        if (schema && (!cJSON_IsObject(schema)
            || (rc = json_item_to_buffer(schema, command->input_schema_json,
                                         sizeof(command->input_schema_json))) != CADDONS_OK)) goto out;
        {
            cJSON *risk_item = cJSON_GetObjectItemCaseSensitive(item, "risk");
            if (risk_item) {
                const char *risk_name = cJSON_GetStringValue(risk_item);
                command->risk = parse_risk(risk_name);
                if (!risk_name || command->risk == CADDONS_TOOL_RISK_UNKNOWN) {
                    rc = CADDONS_ERR_PARSE; goto out;
                }
            }
        }
        timeout = cJSON_GetObjectItemCaseSensitive(item, "timeoutMs");
        if (timeout) {
            if (!cJSON_IsNumber(timeout) || timeout->valuedouble < 100
                || timeout->valuedouble > 60000
                || timeout->valuedouble != (double)(uint32_t)timeout->valuedouble) {
                rc = CADDONS_ERR_LIMIT; goto out;
            }
            command->timeout_ms = (uint32_t)timeout->valuedouble;
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(params, "auth");
    if (item) {
        if (!cJSON_IsObject(item)
            || (rc = parse_optional_string(item, "token", connect->auth_token,
                                           sizeof(connect->auth_token))) != CADDONS_OK) goto out;
    }
    rc = CADDONS_OK;
out:
    cJSON_Delete(params);
    return rc;
}

int caddons_node_encode_connect(const char *request_id,
                                const caddons_node_connect_t *connect,
                                char *output, size_t output_size, size_t *written)
{
    cJSON *params = NULL;
    cJSON *client;
    cJSON *caps;
    cJSON *commands;
    cJSON *meta;
    cJSON *auth;
    cJSON *schema;
    caddons_node_frame_t frame;
    size_t params_len;
    size_t i;
    int rc = CADDONS_ERR_NOMEM;
    if (!request_id || !connect || !output || !written || !connect->node_id[0]
        || connect->command_count > CAGENT_ADDONS_MAX_NODE_COMMANDS)
        return CADDONS_ERR_INVALID;
    params = cJSON_CreateObject(); client = cJSON_CreateObject();
    caps = cJSON_CreateArray(); commands = cJSON_CreateArray(); meta = cJSON_CreateArray();
    if (!params || !client || !caps || !commands || !meta) goto out;
    cJSON_AddNumberToObject(params, "minProtocol", connect->min_protocol);
    cJSON_AddNumberToObject(params, "maxProtocol", connect->max_protocol);
    cJSON_AddStringToObject(client, "id", connect->node_id);
    if (connect->display_name[0]) cJSON_AddStringToObject(client, "displayName", connect->display_name);
    if (connect->version[0]) cJSON_AddStringToObject(client, "version", connect->version);
    if (connect->platform[0]) cJSON_AddStringToObject(client, "platform", connect->platform);
    if (connect->device_family[0]) cJSON_AddStringToObject(client, "deviceFamily", connect->device_family);
    cJSON_AddStringToObject(client, "mode", connect->mode[0] ? connect->mode : "node");
    cJSON_AddItemToObject(params, "client", client); client = NULL;
    cJSON_AddItemToArray(caps, cJSON_CreateString("node.invoke"));
    cJSON_AddItemToObject(params, "caps", caps); caps = NULL;
    for (i = 0; i < connect->command_count; i++) {
        const caddons_node_command_t *command = &connect->commands[i];
        cJSON *entry = cJSON_CreateObject();
        if (!command->command[0] || !entry) goto out;
        cJSON_AddItemToArray(commands, cJSON_CreateString(command->command));
        cJSON_AddStringToObject(entry, "command", command->command);
        cJSON_AddStringToObject(entry, "description", command->description);
        schema = parse_json_exact(command->input_schema_json,
                                  strlen(command->input_schema_json));
        if (!schema) { cJSON_Delete(entry); rc = CADDONS_ERR_PARSE; goto out; }
        cJSON_AddItemToObject(entry, "inputSchema", schema);
        cJSON_AddStringToObject(entry, "risk", caddons_tool_risk_name(command->risk));
        cJSON_AddNumberToObject(entry, "timeoutMs", command->timeout_ms);
        cJSON_AddItemToArray(meta, entry);
    }
    cJSON_AddItemToObject(params, "commands", commands); commands = NULL;
    cJSON_AddItemToObject(params, "toolMeta", meta); meta = NULL;
    cJSON_AddStringToObject(params, "role", connect->role[0] ? connect->role : "node");
    cJSON_AddItemToObject(params, "scopes", cJSON_CreateArray());
    if (connect->auth_token[0]) {
        auth = cJSON_CreateObject();
        if (!auth) goto out;
        cJSON_AddStringToObject(auth, "token", connect->auth_token);
        cJSON_AddItemToObject(params, "auth", auth);
    }
    memset(&frame, 0, sizeof(frame));
    frame.type = CADDONS_NODE_FRAME_REQUEST;
    if ((rc = copy_string(frame.id, sizeof(frame.id), request_id)) != CADDONS_OK
        || (rc = copy_string(frame.name, sizeof(frame.name), "connect")) != CADDONS_OK
        || (rc = print_json(params, frame.body_json, sizeof(frame.body_json),
                            &params_len)) != CADDONS_OK) goto out;
    rc = caddons_node_encode(&frame, output, output_size, written);
out:
    cJSON_Delete(client); cJSON_Delete(caps); cJSON_Delete(commands);
    cJSON_Delete(meta); cJSON_Delete(params);
    return rc;
}

int caddons_node_sanitize_name(const char *input, char *output, size_t output_size)
{
    size_t i;
    size_t len;
    if (!input || !output || output_size == 0 || !input[0]) return CADDONS_ERR_INVALID;
    len = strlen(input);
    if (len >= output_size) return CADDONS_ERR_LIMIT;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)input[i];
        output[i] = (char)(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                            || (c >= '0' && c <= '9') || c == '_' || c == '-')
                               ? c : '_');
    }
    output[len] = '\0';
    return CADDONS_OK;
}

int caddons_node_public_name(const char *prefix, const char *node_id,
                             const char *command, char *output,
                             size_t output_size)
{
    char safe_prefix[CAGENT_ADDONS_FRAME_NAME_SIZE + 1];
    char safe_node[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    char safe_command[CAGENT_ADDONS_COMMAND_NAME_SIZE + 1];
    int len;
    int rc;
    if (!output || output_size == 0) return CADDONS_ERR_INVALID;
    if ((rc = caddons_node_sanitize_name(prefix, safe_prefix, sizeof(safe_prefix))) != CADDONS_OK
        || (rc = caddons_node_sanitize_name(node_id, safe_node, sizeof(safe_node))) != CADDONS_OK
        || (rc = caddons_node_sanitize_name(command, safe_command, sizeof(safe_command))) != CADDONS_OK)
        return rc;
    len = snprintf(output, output_size, "%s_%s_%s", safe_prefix, safe_node, safe_command);
    if (len < 0 || (size_t)len >= output_size
        || len > CAGENT_ADDONS_MAX_PUBLIC_NAME_SIZE) return CADDONS_ERR_LIMIT;
    return CADDONS_OK;
}
