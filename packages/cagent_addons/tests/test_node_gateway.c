/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/node_gateway.h"
#include "cagent_addons/node_proto.h"

#include <agent.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#define MOCK_SENT_MAX 24

struct caddons_transport_peer {
    uint64_t generation;
    bool closed;
};

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool started;
    bool stopped;
    bool auto_result;
    size_t sent_count;
    size_t close_count;
    char sent[MOCK_SENT_MAX][CAGENT_ADDONS_RECV_BUF_SIZE + 1];
} mock_transport_t;

static int mock_start_server(caddons_transport_t *transport,
                             const char *bind_host, uint16_t port,
                             const char *path)
{
    mock_transport_t *mock = transport->context;
    assert(strcmp(bind_host, "127.0.0.1") == 0);
    assert(port == CAGENT_ADDONS_NODE_DEFAULT_PORT);
    assert(strcmp(path, "/") == 0);
    mock->started = true;
    return CADDONS_OK;
}

static void emit(caddons_transport_t *transport,
                 caddons_transport_event_type_t type,
                 caddons_transport_peer_t *peer,
                 const char *payload)
{
    caddons_transport_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.peer = peer;
    event.peer_generation = peer->generation;
    event.payload = (const uint8_t *)payload;
    event.payload_len = payload ? strlen(payload) : 0;
    assert(transport->event_fn != NULL);
    transport->event_fn(transport, &event, transport->event_user_data);
}

static void auto_result(caddons_transport_t *transport,
                        caddons_transport_peer_t *peer,
                        const char *message, size_t message_len)
{
    caddons_node_frame_t request;
    caddons_node_frame_t result;
    cJSON *payload;
    cJSON *params;
    char *params_text;
    char wire[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    const char *id;
    const char *node_id;
    size_t wire_len;
    if (caddons_node_decode(message, message_len, &request) != CADDONS_OK
        || request.type != CADDONS_NODE_FRAME_EVENT
        || strcmp(request.name, "node.invoke.request") != 0) return;
    payload = cJSON_Parse(request.body_json);
    assert(payload != NULL);
    id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "id"));
    node_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "nodeId"));
    assert(id != NULL && node_id != NULL);
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "id", id);
    cJSON_AddStringToObject(params, "nodeId", node_id);
    cJSON_AddBoolToObject(params, "ok", true);
    cJSON_AddStringToObject(params, "payloadJSON", "{\"temperature_c\":29.5}");
    params_text = cJSON_PrintUnformatted(params);
    assert(params_text != NULL);
    memset(&result, 0, sizeof(result));
    result.type = CADDONS_NODE_FRAME_REQUEST;
    strcpy(result.id, "result-ack-1");
    strcpy(result.name, "node.invoke.result");
    strcpy(result.body_json, params_text);
    assert(caddons_node_encode(&result, wire, sizeof(wire), &wire_len) == CADDONS_OK);
    cJSON_free(params_text);
    cJSON_Delete(params);
    cJSON_Delete(payload);
    emit(transport, CADDONS_TRANSPORT_DATA, peer, wire);
}

static int mock_send(caddons_transport_t *transport,
                     caddons_transport_peer_t *peer,
                     const void *payload, size_t payload_len,
                     uint64_t deadline_ms)
{
    mock_transport_t *mock = transport->context;
    bool should_reply;
    assert(deadline_ms != 0);
    assert(payload_len <= CAGENT_ADDONS_RECV_BUF_SIZE);
    pthread_mutex_lock(&mock->mutex);
    assert(mock->sent_count < MOCK_SENT_MAX);
    memcpy(mock->sent[mock->sent_count], payload, payload_len);
    mock->sent[mock->sent_count][payload_len] = '\0';
    mock->sent_count++;
    should_reply = mock->auto_result;
    pthread_cond_broadcast(&mock->condition);
    pthread_mutex_unlock(&mock->mutex);
    if (should_reply) auto_result(transport, peer, payload, payload_len);
    return CADDONS_OK;
}

static int mock_close_peer(caddons_transport_t *transport,
                           caddons_transport_peer_t *peer)
{
    mock_transport_t *mock = transport->context;
    pthread_mutex_lock(&mock->mutex);
    peer->closed = true;
    mock->close_count++;
    pthread_cond_broadcast(&mock->condition);
    pthread_mutex_unlock(&mock->mutex);
    return CADDONS_OK;
}

static uint64_t mock_peer_generation(caddons_transport_t *transport,
                                     const caddons_transport_peer_t *peer)
{
    (void)transport;
    return peer->generation;
}

static int mock_stop(caddons_transport_t *transport)
{
    mock_transport_t *mock = transport->context;
    mock->stopped = true;
    return CADDONS_OK;
}

static const caddons_transport_ops_t mock_ops = {
    .start_server = mock_start_server,
    .send = mock_send,
    .close_peer = mock_close_peer,
    .peer_generation = mock_peer_generation,
    .stop = mock_stop,
};

static void wait_sent(mock_transport_t *mock, size_t expected)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&mock->mutex);
    while (mock->sent_count < expected) {
        int rc = pthread_cond_timedwait(&mock->condition, &mock->mutex, &deadline);
        assert(rc == 0);
    }
    pthread_mutex_unlock(&mock->mutex);
}

static const char *connect_message(const char *request_id, const char *token,
                                   char *output, size_t output_size)
{
    int length = snprintf(output, output_size,
        "{\"type\":\"req\",\"id\":\"%s\",\"method\":\"connect\","
        "\"params\":{\"minProtocol\":3,\"maxProtocol\":3,"
        "\"client\":{\"id\":\"temp-01\",\"displayName\":\"Bedroom sensor\","
        "\"version\":\"1.0.0\",\"platform\":\"vela\","
        "\"deviceFamily\":\"sensor\",\"mode\":\"node\"},"
        "\"caps\":[\"node.invoke\"],\"commands\":[\"get_temperature\"],"
        "\"toolMeta\":[{\"command\":\"get_temperature\","
        "\"description\":\"Read bedroom temperature.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},"
        "\"risk\":\"read_only\",\"timeoutMs\":200}],"
        "\"role\":\"node\",\"scopes\":[],\"auth\":{\"token\":\"%s\"}}}",
        request_id, token);
    assert(length > 0 && (size_t)length < output_size);
    return output;
}

static void apply_all(caddons_remote_catalog_t *catalog, agent_t *agent)
{
    caddons_remote_mutation_t mutation;
    while (caddons_remote_catalog_next_mutation(catalog, &mutation) == CADDONS_OK)
        assert(caddons_remote_catalog_apply_mutation(catalog, agent, &mutation)
               == CADDONS_OK);
}

static agent_t *create_agent(void)
{
    static const agent_model_mock_step_t steps[] = {
        { AGENT_MODEL_MOCK_TOOL_CALL, NULL, "call-1",
          "node_temp-01_get_temperature", "{}" },
        { AGENT_MODEL_MOCK_FINAL, "temperature received", NULL, NULL, NULL },
        { AGENT_MODEL_MOCK_TOOL_CALL, NULL, "call-2",
          "node_temp-01_get_temperature", "{}" },
        { AGENT_MODEL_MOCK_FINAL, "timeout handled", NULL, NULL, NULL },
    };
    agent_model_mock_config_t config = { steps, 4, 0 };
    agent_model_t *model;
    agent_t *agent = agent_create_simple("gateway-test", "test");
    assert(agent != NULL);
    model = agent_model_mock_create(&config, NULL);
    assert(model != NULL);
    assert(agent_set_model_owned(agent, model) == AGENT_OK);
    return agent;
}

static void test_gateway_reconnect_and_invoke(void)
{
    mock_transport_t mock;
    caddons_transport_t transport;
    caddons_transport_peer_t old_peer = {1, false};
    caddons_transport_peer_t new_peer = {2, false};
    caddons_transport_peer_t bad_peer = {3, false};
    caddons_remote_catalog_t *catalog;
    caddons_node_gateway_config_t config;
    caddons_node_gateway_t *gateway;
    caddons_node_info_t node;
    agent_t *agent;
    char connect[2048];
    char output[64];
    int enabled;
    memset(&mock, 0, sizeof(mock));
    assert(pthread_mutex_init(&mock.mutex, NULL) == 0);
    assert(pthread_cond_init(&mock.condition, NULL) == 0);
    memset(&transport, 0, sizeof(transport));
    transport.ops = &mock_ops;
    transport.context = &mock;
    catalog = caddons_remote_catalog_create();
    agent = create_agent();
    memset(&config, 0, sizeof(config));
    config.transport = &transport;
    config.catalog = catalog;
    config.bind_host = "127.0.0.1";
    config.listen_port = CAGENT_ADDONS_NODE_DEFAULT_PORT;
    config.ws_path = "/";
    config.auth_token = "shared-token";
    config.max_nodes = 4;
    config.max_pending = 4;
    config.invoke_timeout_ms = 1500;
    gateway = caddons_node_gateway_create(&config);
    assert(gateway != NULL);
    assert(caddons_node_gateway_start(gateway) == CADDONS_OK);
    assert(mock.started);

    emit(&transport, CADDONS_TRANSPORT_CONNECTED, &old_peer, NULL);
    wait_sent(&mock, 1);
    connect_message("connect-old", "shared-token", connect, sizeof(connect));
    emit(&transport, CADDONS_TRANSPORT_DATA, &old_peer, connect);
    wait_sent(&mock, 2);
    assert(caddons_node_gateway_active_count(gateway) == 1);
    assert(caddons_node_gateway_list(gateway, &node, 1) == 1);
    assert(node.connection_gen == 1 && node.online && node.command_count == 1);
    assert(strcmp(node.commands[0], "get_temperature") == 0);
    apply_all(catalog, agent);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_OK && enabled == 1);

    emit(&transport, CADDONS_TRANSPORT_CONNECTED, &new_peer, NULL);
    wait_sent(&mock, 3);
    connect_message("connect-new", "shared-token", connect, sizeof(connect));
    emit(&transport, CADDONS_TRANSPORT_DATA, &new_peer, connect);
    wait_sent(&mock, 4);
    assert(old_peer.closed);
    apply_all(catalog, agent); /* unregister generation 1, then register 2 */
    assert(caddons_node_gateway_list(gateway, &node, 1) == 1);
    assert(node.connection_gen == 2 && node.online);

    emit(&transport, CADDONS_TRANSPORT_DISCONNECTED, &old_peer, NULL);
    assert(caddons_node_gateway_active_count(gateway) == 1);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_OK);

    mock.auto_result = true;
    assert(agent_run_simple(agent, "temperature?", output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "temperature received") == 0);
    wait_sent(&mock, 5);

    mock.auto_result = false;
    assert(agent_run_simple(agent, "temperature again?", output, sizeof(output)) == AGENT_OK);
    assert(strcmp(output, "timeout handled") == 0);
    wait_sent(&mock, 6);
    auto_result(&transport, &new_peer, mock.sent[5], strlen(mock.sent[5]));
    assert(caddons_node_gateway_active_count(gateway) == 1); /* Late result ignored. */

    connect_message("connect-bad", "wrong-token", connect, sizeof(connect));
    emit(&transport, CADDONS_TRANSPORT_DATA, &bad_peer, connect);
    assert(bad_peer.closed);
    assert(caddons_node_gateway_active_count(gateway) == 1);

    assert(caddons_node_gateway_stop(gateway) == CADDONS_OK);
    assert(mock.stopped);
    assert(caddons_node_gateway_active_count(gateway) == 0);
    apply_all(catalog, agent);
    assert(agent_tool_is_enabled(agent, "node_temp-01_get_temperature", &enabled)
           == AGENT_ERROR_NOTFOUND);
    caddons_node_gateway_destroy(gateway);
    agent_destroy(agent);
    caddons_remote_catalog_destroy(catalog);
    pthread_cond_destroy(&mock.condition);
    pthread_mutex_destroy(&mock.mutex);
}

int main(void)
{
    test_gateway_reconnect_and_invoke();
    puts("test_node_gateway: OK");
    return 0;
}
