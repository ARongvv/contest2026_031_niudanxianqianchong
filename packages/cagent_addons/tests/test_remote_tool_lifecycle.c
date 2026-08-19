/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/remote_tool.h"

#include <agent.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int calls;
    uint64_t last_generation;
    char last_route[CAGENT_ADDONS_ROUTE_ID_SIZE + 1];
    bool block;
    bool entered;
    bool release;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} backend_t;

static int backend_execute(void *context, const char *route_id,
                           uint64_t connection_gen,
                           const char *arguments_json,
                           uint64_t deadline_ms,
                           char *result_json, size_t result_size)
{
    backend_t *backend = context;
    int length;
    assert(arguments_json != NULL);
    assert(deadline_ms != 0);
    backend->calls++;
    backend->last_generation = connection_gen;
    strcpy(backend->last_route, route_id);
    if (backend->block) {
        pthread_mutex_lock(&backend->mutex);
        backend->entered = true;
        pthread_cond_signal(&backend->condition);
        while (!backend->release)
            pthread_cond_wait(&backend->condition, &backend->mutex);
        pthread_mutex_unlock(&backend->mutex);
    }
    length = snprintf(result_json, result_size, "{\"value\":42}");
    return length < 0 || (size_t)length >= result_size ? CADDONS_ERR_LIMIT
                                                       : CADDONS_OK;
}

static caddons_remote_descriptor_t descriptor(backend_t *backend,
                                               uint64_t generation)
{
    caddons_remote_descriptor_t tool;
    memset(&tool, 0, sizeof(tool));
    tool.origin = CADDONS_TOOL_NODE;
    strcpy(tool.route_id, "node:temp-01:get_temperature");
    strcpy(tool.source_name, "get_temperature");
    strcpy(tool.public_name, "node_temp-01_get_temperature");
    strcpy(tool.description, "Read bedroom temperature.");
    strcpy(tool.input_schema_json, "{\"type\":\"object\",\"properties\":{}}");
    tool.risk = CADDONS_TOOL_RISK_READ_ONLY;
    tool.timeout_ms = 1000;
    tool.connection_gen = generation;
    tool.execute = backend_execute;
    tool.backend_context = backend;
    return tool;
}

static int apply_next(caddons_remote_catalog_t *catalog, agent_t *agent,
                      caddons_remote_mutation_type_t expected,
                      uint64_t expected_generation)
{
    caddons_remote_mutation_t mutation;
    int rc = caddons_remote_catalog_next_mutation(catalog, &mutation);
    assert(rc == CADDONS_OK);
    assert(mutation.type == expected);
    assert(mutation.connection_gen == expected_generation);
    return caddons_remote_catalog_apply_mutation(catalog, agent, &mutation);
}

static agent_t *create_agent_for_tool(const char *tool_name)
{
    const agent_model_mock_step_t steps[] = {
        { AGENT_MODEL_MOCK_TOOL_CALL, NULL, "call-1", tool_name, "{}" },
        { AGENT_MODEL_MOCK_FINAL, "done", NULL, NULL, NULL },
    };
    agent_model_mock_config_t model_config;
    agent_model_t *model;
    agent_t *agent = agent_create_simple("remote-test", "test");
    assert(agent != NULL);
    memset(&model_config, 0, sizeof(model_config));
    model_config.steps = steps;
    model_config.step_count = sizeof(steps) / sizeof(steps[0]);
    model = agent_model_mock_create(&model_config, NULL);
    assert(model != NULL);
    assert(agent_set_model_owned(agent, model) == AGENT_OK);
    return agent;
}

static void run_once(agent_t *agent)
{
    char output[64];
    assert(agent_run_simple(agent, "read temperature", output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "done") == 0);
}

typedef struct {
    size_t count;
    agent_tool_info_t tool;
} tool_list_t;

static int capture_tool(const agent_tool_info_t *tool, void *user_data)
{
    tool_list_t *list = user_data;

    assert(tool != NULL && list != NULL);
    list->tool = *tool;
    list->count++;
    return AGENT_OK;
}

static void test_register_execute_disable_remove(void)
{
    backend_t backend = {0};
    caddons_remote_descriptor_t tool = descriptor(&backend, 1);
    caddons_remote_catalog_t *catalog = caddons_remote_catalog_create();
    caddons_remote_view_t view;
    agent_t *agent = create_agent_for_tool(tool.public_name);
    int enabled;
    assert(catalog != NULL);
    assert(caddons_remote_catalog_discover(catalog, &tool) == CADDONS_OK);
    memset(tool.description, 0xa5, sizeof(tool.description));
    memset(tool.route_id, 0xa5, sizeof(tool.route_id)); /* Catalog owns its copy. */
    assert(apply_next(catalog, agent, CADDONS_MUTATION_REGISTER, 1) == CADDONS_OK);
    assert(caddons_remote_catalog_list(catalog, &view, 1) == 1);
    assert(view.state == CADDONS_REMOTE_REGISTERED && view.online);
    {
        tool_list_t list = {0};

        assert(agent_tool_enumerate(agent, capture_tool, &list) == AGENT_OK);
        assert(list.count == 1);
        assert(strcmp(list.tool.name, "node_temp-01_get_temperature") == 0);
        assert(strcmp(list.tool.description, "Read bedroom temperature.") == 0);
        assert(list.tool.group_id == CADDONS_TOOL_NODE);
        assert(list.tool.enabled && list.tool.llm_visible);
        assert((list.tool.flags & AGENT_TOOL_FLAG_READ_ONLY) != 0u);
    }
    run_once(agent);
    assert(backend.calls == 1);
    assert(backend.last_generation == 1);
    assert(strcmp(backend.last_route, "node:temp-01:get_temperature") == 0);
    assert(caddons_remote_catalog_set_enabled(catalog, agent,
                                              "node_temp-01_get_temperature",
                                              false) == CADDONS_OK);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled) == AGENT_OK);
    assert(enabled == 0);
    {
        tool_list_t list = {0};

        assert(agent_tool_enumerate(agent, capture_tool, &list) == AGENT_OK);
        assert(list.count == 1 && !list.tool.enabled);
    }
    assert(caddons_remote_catalog_remove_source(catalog, CADDONS_TOOL_NODE,
                                                "node:temp-01:", 1) == CADDONS_OK);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_UNREGISTER, 1) == CADDONS_OK);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_ERROR_NOTFOUND);
    {
        tool_list_t list = {0};

        assert(agent_tool_enumerate(agent, capture_tool, &list) == AGENT_OK);
        assert(list.count == 0);
    }
    assert(caddons_remote_catalog_list(catalog, NULL, 0) == 0);
    agent_destroy(agent);
    caddons_remote_catalog_destroy(catalog);
}

static void test_reconnect_replacement_order(void)
{
    backend_t old_backend = {0};
    backend_t new_backend = {0};
    caddons_remote_descriptor_t old_tool = descriptor(&old_backend, 11);
    caddons_remote_descriptor_t new_tool = descriptor(&new_backend, 12);
    caddons_remote_catalog_t *catalog = caddons_remote_catalog_create();
    caddons_remote_view_t view;
    agent_t *agent = create_agent_for_tool(old_tool.public_name);
    assert(caddons_remote_catalog_discover(catalog, &old_tool) == CADDONS_OK);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_REGISTER, 11) == CADDONS_OK);
    assert(caddons_remote_catalog_remove_source(catalog, CADDONS_TOOL_NODE,
                                                "node:temp-01:", 11) == CADDONS_OK);
    assert(caddons_remote_catalog_discover(catalog, &new_tool) == CADDONS_OK);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_UNREGISTER, 11) == CADDONS_OK);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_REGISTER, 12) == CADDONS_OK);
    assert(caddons_remote_catalog_list(catalog, &view, 1) == 1);
    assert(view.descriptor.connection_gen == 12);
    run_once(agent);
    assert(old_backend.calls == 0 && new_backend.calls == 1);
    agent_destroy(agent);
    caddons_remote_catalog_destroy(catalog);
}

typedef struct {
    agent_t *agent;
} run_context_t;

static void *run_thread(void *context)
{
    run_context_t *run = context;
    run_once(run->agent);
    return NULL;
}

static void test_unregister_waits_for_inflight(void)
{
    backend_t backend;
    caddons_remote_descriptor_t tool;
    caddons_remote_catalog_t *catalog = caddons_remote_catalog_create();
    caddons_remote_mutation_t mutation;
    caddons_remote_view_t view;
    run_context_t run;
    pthread_t thread;
    agent_t *agent = create_agent_for_tool("node_temp-01_get_temperature");
    memset(&backend, 0, sizeof(backend));
    backend.block = true;
    assert(pthread_mutex_init(&backend.mutex, NULL) == 0);
    assert(pthread_cond_init(&backend.condition, NULL) == 0);
    tool = descriptor(&backend, 21);
    assert(caddons_remote_catalog_discover(catalog, &tool) == CADDONS_OK);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_REGISTER, 21) == CADDONS_OK);
    run.agent = agent;
    assert(pthread_create(&thread, NULL, run_thread, &run) == 0);
    pthread_mutex_lock(&backend.mutex);
    while (!backend.entered)
        pthread_cond_wait(&backend.condition, &backend.mutex);
    pthread_mutex_unlock(&backend.mutex);
    assert(caddons_remote_catalog_list(catalog, &view, 1) == 1);
    assert(view.inflight == 1);
    assert(caddons_remote_catalog_remove_source(catalog, CADDONS_TOOL_NODE,
                                                "node:temp-01:", 21) == CADDONS_OK);
    assert(caddons_remote_catalog_next_mutation(catalog, &mutation)
           == CADDONS_ERR_NOT_FOUND);
    pthread_mutex_lock(&backend.mutex);
    backend.release = true;
    pthread_cond_signal(&backend.condition);
    pthread_mutex_unlock(&backend.mutex);
    pthread_join(thread, NULL);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_UNREGISTER, 21) == CADDONS_OK);
    agent_destroy(agent);
    caddons_remote_catalog_destroy(catalog);
    pthread_cond_destroy(&backend.condition);
    pthread_mutex_destroy(&backend.mutex);
}

typedef struct {
    int calls;
    bool saw_normalized_result;
    agent_tool_call_t tool_call;
} capture_model_t;

static int capture_complete(void *provider, agent_runtime_t *runtime,
                            const agent_model_request_t *request,
                            agent_model_response_t *response)
{
    capture_model_t *capture = provider;
    (void)runtime;
    memset(response, 0, sizeof(*response));
    if (capture->calls++ == 0) {
        capture->tool_call.id = "capture-call";
        capture->tool_call.name = "node_temp-01_get_temperature";
        capture->tool_call.arguments_json = "{}";
        response->tool_calls = &capture->tool_call;
        response->tool_call_count = 1;
    } else {
        capture->saw_normalized_result = request->messages_json
            && strstr(request->messages_json, "\\\"source\\\":\\\"node\\\"")
            && strstr(request->messages_json, "\\\"value\\\":42");
        response->content = "captured";
    }
    response->status = AGENT_OK;
    return AGENT_OK;
}

static void test_normalized_result_reaches_model(void)
{
    static const agent_model_ops_t ops = { .complete = capture_complete };
    capture_model_t capture;
    backend_t backend = {0};
    caddons_remote_descriptor_t tool = descriptor(&backend, 31);
    caddons_remote_catalog_t *catalog = caddons_remote_catalog_create();
    agent_model_t *model;
    agent_t *agent = agent_create_simple("capture-test", "test");
    char output[32];
    memset(&capture, 0, sizeof(capture));
    assert(agent != NULL && catalog != NULL);
    model = agent_model_create(&ops, &capture);
    assert(model != NULL);
    assert(agent_set_model_owned(agent, model) == AGENT_OK);
    assert(caddons_remote_catalog_discover(catalog, &tool) == CADDONS_OK);
    assert(apply_next(catalog, agent, CADDONS_MUTATION_REGISTER, 31) == CADDONS_OK);
    assert(agent_run_simple(agent, "capture", output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "captured") == 0);
    assert(capture.saw_normalized_result);
    agent_destroy(agent);
    caddons_remote_catalog_destroy(catalog);
}

int main(void)
{
    test_register_execute_disable_remove();
    test_reconnect_replacement_order();
    test_unregister_waits_for_inflight();
    test_normalized_result_reaches_model();
    puts("test_remote_tool_lifecycle: OK");
    return 0;
}
