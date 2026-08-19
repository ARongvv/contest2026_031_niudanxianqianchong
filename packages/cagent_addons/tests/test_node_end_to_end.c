/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/node_client.h"
#include "cagent_addons/node_gateway.h"
#include "cagent_addons/ws_transport.h"

#include <agent.h>
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    size_t registered;
    size_t catalog_changes;
} test_state_t;

static uint16_t unused_loopback_port(void)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(fd, (const struct sockaddr *)&address, sizeof(address)) == 0);
    assert(getsockname(fd, (struct sockaddr *)&address, &length) == 0);
    close(fd);
    return ntohs(address.sin_port);
}

static void node_state_changed(caddons_node_client_state_t state, int reason,
                               void *user_data)
{
    test_state_t *test = user_data;
    (void)reason;
    pthread_mutex_lock(&test->mutex);
    if (state == CADDONS_NODE_REGISTERED) test->registered++;
    pthread_cond_broadcast(&test->condition);
    pthread_mutex_unlock(&test->mutex);
}

static void catalog_changed(void *user_data)
{
    test_state_t *test = user_data;
    pthread_mutex_lock(&test->mutex);
    test->catalog_changes++;
    pthread_cond_broadcast(&test->condition);
    pthread_mutex_unlock(&test->mutex);
}

static void wait_registered(test_state_t *test)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&test->mutex);
    while (test->registered == 0)
        assert(pthread_cond_timedwait(&test->condition, &test->mutex,
                                      &deadline) == 0);
    pthread_mutex_unlock(&test->mutex);
}

static int get_temperature(const char *arguments_json,
                           char *result_json, size_t result_size,
                           void *user_data)
{
    int *calls = user_data;
    int length;
    assert(strcmp(arguments_json, "{}") == 0);
    (*calls)++;
    length = snprintf(result_json, result_size, "{\"temperature_c\":26.5}");
    return length < 0 || (size_t)length >= result_size
        ? CADDONS_ERR_LIMIT : CADDONS_OK;
}

static agent_t *create_agent(void)
{
    static const agent_model_mock_step_t steps[] = {
        {AGENT_MODEL_MOCK_TOOL_CALL, NULL, "call-real-ws",
         "node_temp-01_get_temperature", "{}"},
        {AGENT_MODEL_MOCK_FINAL, "real websocket complete", NULL, NULL, NULL},
        {AGENT_MODEL_MOCK_TOOL_CALL, NULL, "call-after-reconnect",
         "node_temp-01_get_temperature", "{}"},
        {AGENT_MODEL_MOCK_FINAL, "reconnect complete", NULL, NULL, NULL},
    };
    agent_model_mock_config_t config = {steps, 4, 0};
    agent_model_t *model;
    agent_t *agent = agent_create_simple("node-e2e", "test");
    assert(agent != NULL);
    model = agent_model_mock_create(&config, NULL);
    assert(model != NULL);
    assert(agent_set_model_owned(agent, model) == AGENT_OK);
    return agent;
}

static void apply_all(caddons_remote_catalog_t *catalog, agent_t *agent)
{
    caddons_remote_mutation_t mutation;
    while (caddons_remote_catalog_next_mutation(catalog, &mutation) == CADDONS_OK)
        assert(caddons_remote_catalog_apply_mutation(catalog, agent, &mutation)
               == CADDONS_OK);
}

static void test_real_transport_node_gateway(void)
{
    caddons_ws_transport_config_t ws_config = {2, 1000, 1000};
    caddons_transport_t *server_transport = caddons_ws_transport_create(&ws_config);
    caddons_transport_t *client_transport = caddons_ws_transport_create(&ws_config);
    caddons_remote_catalog_t *catalog = caddons_remote_catalog_create();
    caddons_node_gateway_config_t gateway_config;
    caddons_node_client_config_t client_config;
    caddons_node_command_entry_t command;
    caddons_node_gateway_t *gateway;
    caddons_node_client_t *client;
    test_state_t test;
    agent_t *agent = create_agent();
    uint16_t port = unused_loopback_port();
    char endpoint[128];
    char output[64];
    int handler_calls = 0;
    int enabled;
    assert(server_transport && client_transport && catalog);
    memset(&test, 0, sizeof(test));
    assert(pthread_mutex_init(&test.mutex, NULL) == 0);
    assert(pthread_cond_init(&test.condition, NULL) == 0);
    memset(&gateway_config, 0, sizeof(gateway_config));
    gateway_config.transport = server_transport;
    gateway_config.catalog = catalog;
    gateway_config.bind_host = "127.0.0.1";
    gateway_config.listen_port = port;
    gateway_config.ws_path = "/";
    gateway_config.auth_token = "e2e-token";
    gateway_config.max_nodes = 2;
    gateway_config.max_pending = 2;
    gateway_config.invoke_timeout_ms = 1000;
    gateway_config.catalog_changed_fn = catalog_changed;
    gateway_config.catalog_changed_user_data = &test;
    gateway = caddons_node_gateway_create(&gateway_config);
    assert(gateway != NULL);
    assert(caddons_node_gateway_start(gateway) == CADDONS_OK);

    memset(&command, 0, sizeof(command));
    strcpy(command.metadata.command, "get_temperature");
    strcpy(command.metadata.description, "Read the sensor temperature.");
    strcpy(command.metadata.input_schema_json,
           "{\"type\":\"object\",\"properties\":{}}");
    command.metadata.risk = CADDONS_TOOL_RISK_READ_ONLY;
    command.metadata.timeout_ms = 800;
    command.execute = get_temperature;
    command.user_data = &handler_calls;
    snprintf(endpoint, sizeof(endpoint), "ws://127.0.0.1:%u/", (unsigned)port);
    memset(&client_config, 0, sizeof(client_config));
    client_config.transport = client_transport;
    client_config.endpoint = endpoint;
    client_config.auth_token = "e2e-token";
    client_config.node_id = "temp-01";
    client_config.display_name = "Bedroom sensor";
    client_config.platform = "vela";
    client_config.device_family = "sensor";
    client_config.version = "1.0.0";
    client_config.commands = &command;
    client_config.command_count = 1;
    client_config.reconnect_min_ms = 100;
    client_config.reconnect_max_ms = 200;
    client_config.keepalive_ms = 100;
    client_config.state_fn = node_state_changed;
    client_config.state_user_data = &test;
    client = caddons_node_client_create(&client_config);
    assert(client != NULL);
    assert(caddons_node_client_start(client) == CADDONS_OK);
    wait_registered(&test);
    assert(caddons_node_gateway_active_count(gateway) == 1);
    apply_all(catalog, agent);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_OK && enabled == 1);
    assert(agent_run_simple(agent, "temperature?", output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "real websocket complete") == 0);
    assert(handler_calls == 1);

    /* Exercise supervisor reconnect plus unregister/register generation swap. */
    assert(caddons_node_gateway_stop(gateway) == CADDONS_OK);
    apply_all(catalog, agent);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_ERROR_NOTFOUND);
    assert(caddons_node_gateway_start(gateway) == CADDONS_OK);
    wait_registered(&test);
    pthread_mutex_lock(&test.mutex);
    while (test.registered < 2) {
        struct timespec deadline;
        assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
        deadline.tv_sec += 3;
        assert(pthread_cond_timedwait(&test.condition, &test.mutex,
                                      &deadline) == 0);
    }
    pthread_mutex_unlock(&test.mutex);
    apply_all(catalog, agent);
    assert(agent_run_simple(agent, "temperature after reconnect?",
                            output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "reconnect complete") == 0);
    assert(handler_calls == 2);

    assert(caddons_node_client_stop(client) == CADDONS_OK);
    caddons_node_client_destroy(client);
    assert(caddons_node_gateway_stop(gateway) == CADDONS_OK);
    apply_all(catalog, agent);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_ERROR_NOTFOUND);
    caddons_node_gateway_destroy(gateway);
    caddons_ws_transport_destroy(client_transport);
    caddons_ws_transport_destroy(server_transport);
    agent_destroy(agent);
    caddons_remote_catalog_destroy(catalog);
    pthread_cond_destroy(&test.condition);
    pthread_mutex_destroy(&test.mutex);
}

int main(void)
{
    test_real_transport_node_gateway();
    puts("test_node_end_to_end: OK");
    return 0;
}
