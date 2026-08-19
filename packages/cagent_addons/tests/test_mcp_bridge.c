/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/mcp_bridge.h"

#include <agent.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

typedef struct {
    unsigned int calls;
    bool saw_session;
    bool saw_tool_call;
} fake_http_t;

static void set_json(caddons_http_response_t *response, const char *json)
{
    size_t length = strlen(json);
    assert(length < response->response_size);
    memcpy(response->response_body, json, length);
    response->response_len = length;
    response->http_status = 200;
    response->content_type = "application/json";
}

static void set_empty_accepted(caddons_http_response_t *response)
{
    response->response_len = 0;
    response->http_status = 202;
    response->content_type = NULL;
}

static int fake_http_request(void *context, const caddons_http_request_t *request,
                             caddons_http_response_t *response)
{
    fake_http_t *fake = context;
    cJSON *root;
    cJSON *method;
    cJSON *id;
    char reply[2048];
    int length;
    assert(request != NULL && response != NULL);
    assert(strcmp(request->method, "POST") == 0);
    assert(strcmp(request->host, "127.0.0.1") == 0);
    assert(request->port == 18800);
    assert(strcmp(request->path, "/mcp") == 0);
    assert(request->deadline_ms != 0);
    root = cJSON_Parse(request->request_body);
    assert(root != NULL);
    method = cJSON_GetObjectItemCaseSensitive(root, "method");
    id = cJSON_GetObjectItemCaseSensitive(root, "id");
    assert(cJSON_IsString(method));
    fake->calls++;
    if (strcmp(method->valuestring, "initialize") == 0) {
        assert(request->mcp_protocol_version == NULL);
        assert(cJSON_IsString(id));
        length = snprintf(reply, sizeof(reply),
            "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{"
            "\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},"
            "\"serverInfo\":{\"name\":\"fake\",\"version\":\"1\"}}}",
            id->valuestring);
        assert(length > 0 && (size_t)length < sizeof(reply));
        response->mcp_session_id = "session-1";
        set_json(response, reply);
    } else if (strcmp(method->valuestring, "notifications/initialized") == 0) {
        assert(strcmp(request->mcp_protocol_version, "2025-03-26") == 0);
        assert(strcmp(request->mcp_session_id, "session-1") == 0);
        fake->saw_session = true;
        set_empty_accepted(response);
    } else if (strcmp(method->valuestring, "tools/list") == 0) {
        assert(cJSON_IsString(id));
        assert(strcmp(request->mcp_session_id, "session-1") == 0);
        length = snprintf(reply, sizeof(reply),
            "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"tools\":["
            "{\"name\":\"get_weather\",\"description\":\"Get weather.\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\"}}}},"
            "{\"name\":\"not_allowed\",\"description\":\"Skip.\","
            "\"inputSchema\":{\"type\":\"object\"}}]}}",
            id->valuestring);
        assert(length > 0 && (size_t)length < sizeof(reply));
        set_json(response, reply);
    } else if (strcmp(method->valuestring, "tools/call") == 0) {
        cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
        assert(cJSON_IsString(id) && cJSON_IsString(name));
        assert(strcmp(name->valuestring, "get_weather") == 0);
        fake->saw_tool_call = true;
        length = snprintf(reply, sizeof(reply),
            "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{"
            "\"content\":[{\"type\":\"text\",\"text\":\"sunny\"}],"
            "\"isError\":false}}", id->valuestring);
        assert(length > 0 && (size_t)length < sizeof(reply));
        set_json(response, reply);
    } else {
        assert(false);
    }
    cJSON_Delete(root);
    return CADDONS_OK;
}

static void apply_all(caddons_remote_catalog_t *catalog, agent_t *agent)
{
    caddons_remote_mutation_t mutation;
    while (caddons_remote_catalog_next_mutation(catalog, &mutation) == CADDONS_OK)
        assert(caddons_remote_catalog_apply_mutation(catalog, agent, &mutation)
               == CADDONS_OK);
}

static void test_start_discover_and_call(void)
{
    const caddons_mcp_tool_policy_t policies[] = {
        { "get_weather", CADDONS_TOOL_RISK_READ_ONLY, 1000 },
    };
    const agent_model_mock_step_t steps[] = {
        { AGENT_MODEL_MOCK_TOOL_CALL, NULL, "call-1", "mcp_weather_get_weather",
          "{\"city\":\"Beijing\"}" },
        { AGENT_MODEL_MOCK_FINAL, "done", NULL, NULL, NULL },
    };
    agent_model_mock_config_t model_config;
    caddons_mcp_bridge_config_t config;
    caddons_remote_catalog_t *catalog = caddons_remote_catalog_create();
    caddons_mcp_bridge_t *bridge;
    caddons_remote_view_t tools[2];
    agent_model_t *model;
    agent_t *agent;
    fake_http_t fake = {0};
    char output[32];
    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1";
    config.port = 18800;
    config.path = "/mcp";
    config.server_id = "weather";
    config.protocol_version = "2025-03-26";
    config.tool_policies = policies;
    config.tool_policy_count = 1;
    config.catalog = catalog;
    config.http_request = fake_http_request;
    config.http_context = &fake;
    config.request_timeout_ms = 1000;
    assert(catalog != NULL);
    bridge = caddons_mcp_bridge_create(&config);
    assert(bridge != NULL);
    assert(caddons_mcp_bridge_start(bridge) == CADDONS_OK);
    assert(fake.saw_session);
    assert(caddons_remote_catalog_list(catalog, tools, 2) == 1);
    assert(strcmp(tools[0].descriptor.public_name, "mcp_weather_get_weather") == 0);
    assert(tools[0].descriptor.origin == CADDONS_TOOL_MCP);
    agent = agent_create_simple("mcp-test", "test");
    assert(agent != NULL);
    memset(&model_config, 0, sizeof(model_config));
    model_config.steps = steps;
    model_config.step_count = sizeof(steps) / sizeof(steps[0]);
    model = agent_model_mock_create(&model_config, NULL);
    assert(model != NULL && agent_set_model_owned(agent, model) == AGENT_OK);
    apply_all(catalog, agent);
    assert(agent_run_simple(agent, "weather", output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "done") == 0 && fake.saw_tool_call);
    assert(caddons_mcp_bridge_stop(bridge) == CADDONS_OK);
    apply_all(catalog, agent);
    assert(caddons_remote_catalog_list(catalog, tools, 2) == 0);

    /* Settings may request discovery again. A fresh start must not reuse the
     * old session and must repopulate the catalog after its removal. */
    assert(caddons_mcp_bridge_start(bridge) == CADDONS_OK);
    assert(caddons_remote_catalog_list(catalog, tools, 2) == 1);
    assert(strcmp(tools[0].descriptor.public_name, "mcp_weather_get_weather") == 0);
    apply_all(catalog, agent);
    assert(caddons_mcp_bridge_stop(bridge) == CADDONS_OK);
    apply_all(catalog, agent);
    assert(caddons_remote_catalog_list(catalog, tools, 2) == 0);

    agent_destroy(agent);
    caddons_mcp_bridge_destroy(bridge);
    caddons_remote_catalog_destroy(catalog);
}

int main(void)
{
    test_start_discover_and_call();
    puts("mcp bridge tests passed");
    return 0;
}
