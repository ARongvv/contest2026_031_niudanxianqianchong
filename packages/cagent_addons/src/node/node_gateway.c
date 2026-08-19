/* SPDX-License-Identifier: Apache-2.0 */
/** OpenClaw Node gateway: authentication, generations and pending invokes. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/node_gateway.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cagent_addons/cjson_compat.h"

#define GATEWAY_SEND_QUEUE_SIZE (CAGENT_ADDONS_MAX_NODES * 2 + 2)
#define GATEWAY_CONTROL_MESSAGE_SIZE 512
#define GATEWAY_SEND_TIMEOUT_MS 2000u

typedef struct {
    bool used;
    bool online;
    caddons_transport_peer_t *peer;
    uint64_t peer_generation;
    uint64_t connection_gen;
    char source_node_id[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    caddons_node_info_t info;
} node_entry_t;

typedef struct {
    bool active;
    bool done;
    bool ok;
    caddons_transport_peer_t *peer;
    uint64_t peer_generation;
    uint64_t connection_gen;
    char invoke_id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    char result[CAGENT_ADDONS_MAX_RESULT_SIZE + 1];
    int error;
    pthread_cond_t condition;
} pending_entry_t;

typedef struct {
    caddons_transport_peer_t *peer;
    uint64_t peer_generation;
    size_t length;
    char message[GATEWAY_CONTROL_MESSAGE_SIZE];
} send_item_t;

struct caddons_node_gateway {
    caddons_transport_t *transport;
    caddons_remote_catalog_t *catalog;
    char bind_host[64];
    char ws_path[64];
    char auth_token[CAGENT_ADDONS_AUTH_TOKEN_MAX_LENGTH + 1];
    uint16_t listen_port;
    size_t max_nodes;
    size_t max_pending;
    uint32_t invoke_timeout_ms;
    caddons_node_catalog_fn catalog_changed_fn;
    void *catalog_changed_user_data;

    pthread_mutex_t mutex;
    pthread_cond_t send_condition;
    pthread_t send_thread;
    caddons_thread_stack_t send_stack;
    caddons_thread_stack_provider_t stack_provider;
    bool send_thread_started;
    bool running;
    uint64_t next_connection_gen;
    uint64_t next_invoke_id;
    size_t send_head;
    size_t send_count;
    send_item_t send_queue[GATEWAY_SEND_QUEUE_SIZE];
    node_entry_t nodes[CAGENT_ADDONS_MAX_NODES];
    pending_entry_t pending[CAGENT_ADDONS_MAX_PENDING];
};

static bool stack_provider_enabled(
    const caddons_thread_stack_provider_t *provider)
{
    return provider && provider->alloc_fn && provider->free_fn;
}

static void release_send_stack(caddons_node_gateway_t *gateway)
{
    if (!gateway->send_stack.allocation) return;
    if (stack_provider_enabled(&gateway->stack_provider))
        gateway->stack_provider.free_fn(&gateway->send_stack,
                                        gateway->stack_provider.user_data);
    else
        memset(&gateway->send_stack, 0, sizeof(gateway->send_stack));
}

static int configure_send_stack(caddons_node_gateway_t *gateway,
                                pthread_attr_t *attributes)
{
    int rc;

    if (stack_provider_enabled(&gateway->stack_provider)) {
        memset(&gateway->send_stack, 0, sizeof(gateway->send_stack));
        rc = gateway->stack_provider.alloc_fn(CAGENT_ADDONS_GATEWAY_STACK_SIZE,
                                              &gateway->send_stack,
                                              gateway->stack_provider.user_data);
        if (rc != CADDONS_OK) return rc;
        if (!gateway->send_stack.allocation || !gateway->send_stack.stack
            || gateway->send_stack.stack_size < CAGENT_ADDONS_GATEWAY_STACK_SIZE) {
            release_send_stack(gateway);
            return CADDONS_ERR_INVALID;
        }
        if (pthread_attr_setstack(attributes, gateway->send_stack.stack,
                                  gateway->send_stack.stack_size) != 0) {
            release_send_stack(gateway);
            return CADDONS_ERR_INVALID;
        }
        return CADDONS_OK;
    }

#ifdef __NuttX__
    if (pthread_attr_setstacksize(attributes,
                                  CAGENT_ADDONS_GATEWAY_STACK_SIZE) != 0)
        return CADDONS_ERR_INVALID;
#endif
    return CADDONS_OK;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void monotonic_deadline(uint64_t deadline_ms, struct timespec *deadline)
{
    deadline->tv_sec = (time_t)(deadline_ms / 1000u);
    deadline->tv_nsec = (long)(deadline_ms % 1000u) * 1000000L;
}

static bool token_equal(const char *expected, const char *actual)
{
    size_t expected_len = expected ? strlen(expected) : 0;
    size_t actual_len = actual ? strlen(actual) : 0;
    size_t max_len = expected_len > actual_len ? expected_len : actual_len;
    unsigned diff = (unsigned)(expected_len ^ actual_len);
    size_t i;
    for (i = 0; i < max_len; i++) {
        unsigned char a = i < expected_len ? (unsigned char)expected[i] : 0;
        unsigned char b = i < actual_len ? (unsigned char)actual[i] : 0;
        diff |= (unsigned)(a ^ b);
    }
    return diff == 0;
}

static int random_bytes(void *output, size_t length)
{
    uint8_t *bytes = output;
    size_t offset = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return CADDONS_ERR_INTERNAL;
    while (offset < length) {
        ssize_t count = read(fd, bytes + offset, length - offset);
        if (count <= 0) {
            close(fd);
            return CADDONS_ERR_INTERNAL;
        }
        offset += (size_t)count;
    }
    close(fd);
    return CADDONS_OK;
}

static int make_id(char *output, size_t output_size, uint64_t sequence)
{
    uint8_t random[4];
    int length;
    if (random_bytes(random, sizeof(random)) != CADDONS_OK) {
        uint64_t now = monotonic_ms();
        random[0] = (uint8_t)(now >> 24); random[1] = (uint8_t)(now >> 16);
        random[2] = (uint8_t)(now >> 8); random[3] = (uint8_t)now;
    }
    length = snprintf(output, output_size, "%02x%02x%02x%02x-%08lx",
                      random[0], random[1], random[2], random[3],
                      (unsigned long)(sequence & 0xffffffffu));
    return length < 0 || (size_t)length >= output_size ? CADDONS_ERR_LIMIT
                                                       : CADDONS_OK;
}

static uint64_t current_peer_generation(caddons_node_gateway_t *gateway,
                                        caddons_transport_peer_t *peer)
{
    if (!gateway->transport->ops->peer_generation) return 0;
    return gateway->transport->ops->peer_generation(gateway->transport, peer);
}

static int queue_send(caddons_node_gateway_t *gateway,
                      caddons_transport_peer_t *peer,
                      uint64_t peer_generation,
                      const char *message, size_t length)
{
    size_t tail;
    send_item_t *item;
    if (length >= GATEWAY_CONTROL_MESSAGE_SIZE) return CADDONS_ERR_LIMIT;
    pthread_mutex_lock(&gateway->mutex);
    if (!gateway->running || gateway->send_count >= GATEWAY_SEND_QUEUE_SIZE) {
        pthread_mutex_unlock(&gateway->mutex);
        return CADDONS_ERR_BUSY;
    }
    tail = (gateway->send_head + gateway->send_count) % GATEWAY_SEND_QUEUE_SIZE;
    item = &gateway->send_queue[tail];
    item->peer = peer;
    item->peer_generation = peer_generation;
    item->length = length;
    memcpy(item->message, message, length);
    item->message[length] = '\0';
    gateway->send_count++;
    pthread_cond_signal(&gateway->send_condition);
    pthread_mutex_unlock(&gateway->mutex);
    return CADDONS_OK;
}

static int queue_frame(caddons_node_gateway_t *gateway,
                       caddons_transport_peer_t *peer,
                       uint64_t peer_generation,
                       const caddons_node_frame_t *frame)
{
    char message[GATEWAY_CONTROL_MESSAGE_SIZE];
    size_t length;
    int rc = caddons_node_encode(frame, message, sizeof(message), &length);
    return rc == CADDONS_OK
        ? queue_send(gateway, peer, peer_generation, message, length) : rc;
}

static void *send_worker(void *context)
{
    caddons_node_gateway_t *gateway = context;
    for (;;) {
        send_item_t item;
        uint64_t actual_generation;
        pthread_mutex_lock(&gateway->mutex);
        while (gateway->running && gateway->send_count == 0)
            pthread_cond_wait(&gateway->send_condition, &gateway->mutex);
        if (!gateway->running && gateway->send_count == 0) {
            pthread_mutex_unlock(&gateway->mutex);
            return NULL;
        }
        item = gateway->send_queue[gateway->send_head];
        gateway->send_head = (gateway->send_head + 1) % GATEWAY_SEND_QUEUE_SIZE;
        gateway->send_count--;
        pthread_mutex_unlock(&gateway->mutex);
        actual_generation = current_peer_generation(gateway, item.peer);
        if (gateway->transport->ops->peer_generation
            && actual_generation != item.peer_generation) continue;
        gateway->transport->ops->send(gateway->transport, item.peer,
                                      item.message, item.length,
                                      monotonic_ms() + GATEWAY_SEND_TIMEOUT_MS);
    }
}

static void queue_challenge(caddons_node_gateway_t *gateway,
                            caddons_transport_peer_t *peer,
                            uint64_t peer_generation)
{
    caddons_node_frame_t frame;
    char nonce[32];
    memset(&frame, 0, sizeof(frame));
    frame.type = CADDONS_NODE_FRAME_EVENT;
    strcpy(frame.name, "connect.challenge");
    if (make_id(nonce, sizeof(nonce), peer_generation) != CADDONS_OK) return;
    snprintf(frame.body_json, sizeof(frame.body_json), "{\"nonce\":\"%s\"}", nonce);
    if (queue_frame(gateway, peer, peer_generation, &frame) != CADDONS_OK
        && gateway->transport->ops->close_peer)
        gateway->transport->ops->close_peer(gateway->transport, peer);
}

static void queue_response(caddons_node_gateway_t *gateway,
                           caddons_transport_peer_t *peer,
                           uint64_t peer_generation,
                           const char *request_id,
                           bool ok, const char *body_json)
{
    caddons_node_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = CADDONS_NODE_FRAME_RESPONSE;
    frame.ok = ok;
    if (snprintf(frame.id, sizeof(frame.id), "%s", request_id) >= (int)sizeof(frame.id)
        || snprintf(frame.body_json, sizeof(frame.body_json), "%s", body_json)
           >= (int)sizeof(frame.body_json)) return;
    queue_frame(gateway, peer, peer_generation, &frame);
}

static node_entry_t *find_node_id(caddons_node_gateway_t *gateway,
                                  const char *safe_node_id)
{
    size_t i;
    for (i = 0; i < gateway->max_nodes; i++) {
        if (gateway->nodes[i].used
            && strcmp(gateway->nodes[i].info.node_id, safe_node_id) == 0)
            return &gateway->nodes[i];
    }
    return NULL;
}

static node_entry_t *find_node_peer(caddons_node_gateway_t *gateway,
                                    caddons_transport_peer_t *peer,
                                    uint64_t peer_generation)
{
    size_t i;
    for (i = 0; i < gateway->max_nodes; i++) {
        if (gateway->nodes[i].used && gateway->nodes[i].peer == peer
            && gateway->nodes[i].peer_generation == peer_generation)
            return &gateway->nodes[i];
    }
    return NULL;
}

static void fail_pending_peer_locked(caddons_node_gateway_t *gateway,
                                     caddons_transport_peer_t *peer,
                                     uint64_t peer_generation, int error)
{
    size_t i;
    for (i = 0; i < gateway->max_pending; i++) {
        pending_entry_t *pending = &gateway->pending[i];
        if (pending->active && !pending->done && pending->peer == peer
            && pending->peer_generation == peer_generation) {
            pending->done = true;
            pending->ok = false;
            pending->error = error;
            pthread_cond_signal(&pending->condition);
        }
    }
}

static int gateway_backend_execute(void *context, const char *route_id,
                                   uint64_t connection_gen,
                                   const char *arguments_json,
                                   uint64_t deadline_ms,
                                   char *result_json, size_t result_size)
{
    caddons_node_gateway_t *gateway = context;
    caddons_transport_peer_t *peer;
    uint64_t peer_generation;
    pending_entry_t *pending = NULL;
    caddons_node_frame_t frame;
    cJSON *payload = NULL;
    char *payload_text = NULL;
    const char *command;
    const char *first_colon;
    const char *second_colon;
    char node_id[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    size_t node_len;
    size_t i;
    size_t frame_len;
    char wire[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    int rc = CADDONS_ERR_INTERNAL;

    first_colon = strchr(route_id, ':');
    second_colon = first_colon ? strchr(first_colon + 1, ':') : NULL;
    if (!first_colon || !second_colon || second_colon[1] == '\0')
        return CADDONS_ERR_INVALID;
    node_len = (size_t)(second_colon - first_colon - 1);
    if (node_len == 0 || node_len >= sizeof(node_id)) return CADDONS_ERR_INVALID;
    memcpy(node_id, first_colon + 1, node_len); node_id[node_len] = '\0';
    command = second_colon + 1;

    pthread_mutex_lock(&gateway->mutex);
    {
        node_entry_t *node = find_node_id(gateway, node_id);
        if (!gateway->running || !node || !node->online
            || node->connection_gen != connection_gen) {
            pthread_mutex_unlock(&gateway->mutex);
            return CADDONS_ERR_OFFLINE;
        }
        peer = node->peer;
        peer_generation = node->peer_generation;
    }
    for (i = 0; i < gateway->max_pending; i++) {
        if (!gateway->pending[i].active) {
            pending = &gateway->pending[i];
            memset(pending->invoke_id, 0, sizeof(pending->invoke_id));
            pending->active = true; pending->done = false; pending->ok = false;
            pending->peer = peer; pending->peer_generation = peer_generation;
            pending->connection_gen = connection_gen; pending->error = CADDONS_ERR_INTERNAL;
            make_id(pending->invoke_id, sizeof(pending->invoke_id), ++gateway->next_invoke_id);
            break;
        }
    }
    pthread_mutex_unlock(&gateway->mutex);
    if (!pending) return CADDONS_ERR_BUSY;

    payload = cJSON_CreateObject();
    if (!payload) { rc = CADDONS_ERR_NOMEM; goto finish; }
    cJSON_AddStringToObject(payload, "id", pending->invoke_id);
    cJSON_AddStringToObject(payload, "nodeId", node_id);
    cJSON_AddStringToObject(payload, "command", command);
    cJSON_AddStringToObject(payload, "paramsJSON", arguments_json ? arguments_json : "{}");
    payload_text = cJSON_PrintUnformatted(payload);
    if (!payload_text || strlen(payload_text) >= sizeof(frame.body_json)) {
        rc = CADDONS_ERR_LIMIT; goto finish;
    }
    memset(&frame, 0, sizeof(frame));
    frame.type = CADDONS_NODE_FRAME_EVENT;
    strcpy(frame.name, "node.invoke.request");
    strcpy(frame.body_json, payload_text);
    rc = caddons_node_encode(&frame, wire, sizeof(wire), &frame_len);
    if (rc != CADDONS_OK) goto finish;
    rc = gateway->transport->ops->send(gateway->transport, peer, wire, frame_len,
                                       deadline_ms);
    if (rc != CADDONS_OK) { rc = CADDONS_ERR_NETWORK; goto finish; }

    pthread_mutex_lock(&gateway->mutex);
    while (!pending->done) {
        uint64_t now = monotonic_ms();
        struct timespec wait_until;
        int wait_rc;
        if (deadline_ms && now >= deadline_ms) break;
        monotonic_deadline(deadline_ms ? deadline_ms
                                      : now + gateway->invoke_timeout_ms,
                           &wait_until);
        wait_rc = pthread_cond_timedwait(&pending->condition, &gateway->mutex, &wait_until);
        if (wait_rc == ETIMEDOUT && deadline_ms && monotonic_ms() >= deadline_ms) break;
    }
    if (!pending->done) rc = CADDONS_ERR_TIMEOUT;
    else if (!pending->ok) rc = pending->error;
    else if (strlen(pending->result) >= result_size) rc = CADDONS_ERR_LIMIT;
    else { strcpy(result_json, pending->result); rc = CADDONS_OK; }
    pthread_mutex_unlock(&gateway->mutex);

finish:
    cJSON_free(payload_text);
    cJSON_Delete(payload);
    pthread_mutex_lock(&gateway->mutex);
    pending->active = false;
    pending->done = false;
    pthread_mutex_unlock(&gateway->mutex);
    return rc;
}

static void notify_catalog(caddons_node_gateway_t *gateway)
{
    if (gateway->catalog_changed_fn)
        gateway->catalog_changed_fn(gateway->catalog_changed_user_data);
}

static void handle_connect(caddons_node_gateway_t *gateway,
                           caddons_transport_peer_t *peer,
                           uint64_t peer_generation,
                           const caddons_node_frame_t *frame)
{
    caddons_node_connect_t *connect;
    node_entry_t *node;
    caddons_transport_peer_t *old_peer = NULL;
    uint64_t old_peer_generation = 0;
    uint64_t old_connection_gen = 0;
    uint64_t new_connection_gen;
    char safe_node_id[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    char route_prefix[CAGENT_ADDONS_ROUTE_ID_SIZE + 1];
    size_t i;
    int rc;
    connect = malloc(sizeof(*connect));
    if (!connect) {
        if (gateway->transport->ops->close_peer)
            gateway->transport->ops->close_peer(gateway->transport, peer);
        return;
    }
    rc = caddons_node_parse_connect(frame, connect);
    if (rc != CADDONS_OK || !token_equal(gateway->auth_token, connect->auth_token)) {
        queue_response(gateway, peer, peer_generation, frame->id, false,
                       rc == CADDONS_ERR_VERSION
                           ? "{\"code\":\"INVALID_PROTOCOL\",\"message\":\"protocol 3 required\"}"
                           : "{\"code\":\"UNAUTHORIZED\",\"message\":\"connection rejected\"}");
        if (gateway->transport->ops->close_peer)
            gateway->transport->ops->close_peer(gateway->transport, peer);
        free(connect);
        return;
    }
    if (caddons_node_sanitize_name(connect->node_id, safe_node_id,
                                   sizeof(safe_node_id)) != CADDONS_OK) {
        free(connect);
        return;
    }

    pthread_mutex_lock(&gateway->mutex);
    node = find_node_id(gateway, safe_node_id);
    if (node && strcmp(node->source_node_id, connect->node_id) != 0) {
        pthread_mutex_unlock(&gateway->mutex);
        queue_response(gateway, peer, peer_generation, frame->id, false,
                       "{\"code\":\"NAME_COLLISION\",\"message\":\"sanitized node id collision\"}");
        if (gateway->transport->ops->close_peer)
            gateway->transport->ops->close_peer(gateway->transport, peer);
        free(connect);
        return;
    }
    if (node) {
        old_peer = node->peer;
        old_peer_generation = node->peer_generation;
        old_connection_gen = node->connection_gen;
        node->online = false;
        node->info.online = false;
        fail_pending_peer_locked(gateway, old_peer, old_peer_generation,
                                 CADDONS_ERR_OFFLINE);
    } else {
        for (i = 0; i < gateway->max_nodes; i++) {
            if (!gateway->nodes[i].used || !gateway->nodes[i].online) {
                node = &gateway->nodes[i];
                break;
            }
        }
    }
    if (!node) {
        pthread_mutex_unlock(&gateway->mutex);
        queue_response(gateway, peer, peer_generation, frame->id, false,
                       "{\"code\":\"MAX_NODES\",\"message\":\"node limit reached\"}");
        free(connect);
        return;
    }
    memset(node, 0, sizeof(*node));
    node->used = true; node->online = true; node->peer = peer;
    node->peer_generation = peer_generation;
    node->connection_gen = ++gateway->next_connection_gen;
    strcpy(node->source_node_id, connect->node_id);
    strcpy(node->info.node_id, safe_node_id);
    snprintf(node->info.display_name, sizeof(node->info.display_name), "%s",
             connect->display_name);
    snprintf(node->info.platform, sizeof(node->info.platform), "%s", connect->platform);
    snprintf(node->info.device_family, sizeof(node->info.device_family), "%s",
             connect->device_family);
    node->info.connection_gen = node->connection_gen;
    node->info.command_count = connect->command_count < CAGENT_ADDONS_MAX_NODE_COMMANDS
        ? connect->command_count : CAGENT_ADDONS_MAX_NODE_COMMANDS;
    for (i = 0; i < node->info.command_count; i++) {
        snprintf(node->info.commands[i], sizeof(node->info.commands[i]), "%s",
                 connect->commands[i].command);
    }
    node->info.online = true;
    new_connection_gen = node->connection_gen;
    pthread_mutex_unlock(&gateway->mutex);

    if (old_connection_gen) {
        snprintf(route_prefix, sizeof(route_prefix), "node:%s:", safe_node_id);
        caddons_remote_catalog_remove_source(gateway->catalog, CADDONS_TOOL_NODE,
                                             route_prefix, old_connection_gen);
    }
    for (i = 0; i < connect->command_count; i++) {
        caddons_remote_descriptor_t tool;
        memset(&tool, 0, sizeof(tool));
        tool.origin = CADDONS_TOOL_NODE;
        if (snprintf(tool.route_id, sizeof(tool.route_id), "node:%s:%s",
                     safe_node_id, connect->commands[i].command) >= (int)sizeof(tool.route_id)
            || caddons_node_public_name("node", safe_node_id,
                                        connect->commands[i].command,
                                        tool.public_name, sizeof(tool.public_name)) != CADDONS_OK)
            continue;
        strcpy(tool.source_name, connect->commands[i].command);
        strcpy(tool.description, connect->commands[i].description);
        strcpy(tool.input_schema_json, connect->commands[i].input_schema_json);
        tool.risk = connect->commands[i].risk;
        tool.timeout_ms = connect->commands[i].timeout_ms < gateway->invoke_timeout_ms
            ? connect->commands[i].timeout_ms : gateway->invoke_timeout_ms;
        tool.connection_gen = new_connection_gen;
        tool.execute = gateway_backend_execute;
        tool.backend_context = gateway;
        caddons_remote_catalog_discover(gateway->catalog, &tool);
    }
    {
        char hello[160];
        char conn_id[32];
        make_id(conn_id, sizeof(conn_id), new_connection_gen);
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello-ok\",\"server\":{\"connId\":\"%s\"}}",
                 conn_id);
        queue_response(gateway, peer, peer_generation, frame->id, true, hello);
    }
    if (old_peer && gateway->transport->ops->close_peer)
        gateway->transport->ops->close_peer(gateway->transport, old_peer);
    free(connect);
    notify_catalog(gateway);
}

static void handle_invoke_result(caddons_node_gateway_t *gateway,
                                 caddons_transport_peer_t *peer,
                                 uint64_t peer_generation,
                                 const caddons_node_frame_t *frame)
{
    cJSON *params = cJSON_Parse(frame->body_json);
    const char *id;
    bool ok;
    size_t i;
    if (!params) return;
    id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params, "id"));
    ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params, "ok"));
    if (!id) { cJSON_Delete(params); return; }
    pthread_mutex_lock(&gateway->mutex);
    for (i = 0; i < gateway->max_pending; i++) {
        pending_entry_t *pending = &gateway->pending[i];
        if (!pending->active || pending->done || strcmp(pending->invoke_id, id) != 0
            || pending->peer != peer || pending->peer_generation != peer_generation)
            continue;
        pending->ok = ok;
        pending->done = true;
        if (ok) {
            const char *payload = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(params, "payloadJSON"));
            if (!payload || strlen(payload) >= sizeof(pending->result)) {
                pending->ok = false;
                pending->error = payload ? CADDONS_ERR_LIMIT : CADDONS_ERR_PARSE;
            } else strcpy(pending->result, payload);
        } else {
            cJSON *error = cJSON_GetObjectItemCaseSensitive(params, "error");
            const char *code = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(error, "code"));
            pending->error = code && strcmp(code, "TIMEOUT") == 0
                ? CADDONS_ERR_TIMEOUT : CADDONS_ERR_INTERNAL;
        }
        pthread_cond_signal(&pending->condition);
        break;
    }
    pthread_mutex_unlock(&gateway->mutex);
    cJSON_Delete(params);
}

static void handle_disconnect(caddons_node_gateway_t *gateway,
                              caddons_transport_peer_t *peer,
                              uint64_t peer_generation)
{
    node_entry_t *node;
    char route_prefix[CAGENT_ADDONS_ROUTE_ID_SIZE + 1];
    uint64_t connection_gen;
    pthread_mutex_lock(&gateway->mutex);
    node = find_node_peer(gateway, peer, peer_generation);
    if (!node || !node->online) {
        pthread_mutex_unlock(&gateway->mutex);
        return;
    }
    node->online = false;
    node->info.online = false;
    connection_gen = node->connection_gen;
    snprintf(route_prefix, sizeof(route_prefix), "node:%s:", node->info.node_id);
    fail_pending_peer_locked(gateway, peer, peer_generation, CADDONS_ERR_OFFLINE);
    pthread_mutex_unlock(&gateway->mutex);
    caddons_remote_catalog_remove_source(gateway->catalog, CADDONS_TOOL_NODE,
                                         route_prefix, connection_gen);
    notify_catalog(gateway);
}

static void transport_event(caddons_transport_t *transport,
                            const caddons_transport_event_t *event,
                            void *user_data)
{
    caddons_node_gateway_t *gateway = user_data;
    caddons_node_frame_t frame;
    (void)transport;
    if (!gateway || !event || !event->peer) return;
    if (event->type == CADDONS_TRANSPORT_CONNECTED) {
        queue_challenge(gateway, event->peer, event->peer_generation);
    } else if (event->type == CADDONS_TRANSPORT_DISCONNECTED) {
        handle_disconnect(gateway, event->peer, event->peer_generation);
    } else if (event->type == CADDONS_TRANSPORT_DATA
               && event->payload_len <= CAGENT_ADDONS_RECV_BUF_SIZE
               && caddons_node_decode((const char *)event->payload,
                                       event->payload_len, &frame) == CADDONS_OK) {
        if (frame.type == CADDONS_NODE_FRAME_REQUEST
            && strcmp(frame.name, "connect") == 0)
            handle_connect(gateway, event->peer, event->peer_generation, &frame);
        else if (frame.type == CADDONS_NODE_FRAME_REQUEST
                 && strcmp(frame.name, "node.invoke.result") == 0)
            handle_invoke_result(gateway, event->peer, event->peer_generation, &frame);
    }
}

caddons_node_gateway_t *caddons_node_gateway_create(
    const caddons_node_gateway_config_t *config)
{
    caddons_node_gateway_t *gateway;
    pthread_condattr_t condition_attributes;
    size_t i;
    if (!config || !config->transport || !config->transport->ops
        || !config->transport->ops->start_server || !config->transport->ops->send
        || !config->transport->ops->stop || !config->transport->ops->peer_generation
        || !config->catalog
        || !config->auth_token || !config->auth_token[0])
        return NULL;
    if ((config->bind_host && strlen(config->bind_host) >= sizeof(gateway->bind_host))
        || (config->ws_path && strlen(config->ws_path) >= sizeof(gateway->ws_path))
        || strlen(config->auth_token) >= sizeof(gateway->auth_token)) return NULL;
    gateway = calloc(1, sizeof(*gateway));
    if (!gateway) return NULL;
    gateway->transport = config->transport;
    gateway->catalog = config->catalog;
    snprintf(gateway->bind_host, sizeof(gateway->bind_host), "%s",
             config->bind_host ? config->bind_host : "0.0.0.0");
    snprintf(gateway->ws_path, sizeof(gateway->ws_path), "%s",
             config->ws_path ? config->ws_path : CAGENT_ADDONS_NODE_DEFAULT_PATH);
    snprintf(gateway->auth_token, sizeof(gateway->auth_token), "%s",
             config->auth_token ? config->auth_token : "");
    gateway->listen_port = config->listen_port ? config->listen_port
                                               : CAGENT_ADDONS_NODE_DEFAULT_PORT;
    gateway->max_nodes = config->max_nodes && config->max_nodes < CAGENT_ADDONS_MAX_NODES
        ? config->max_nodes : CAGENT_ADDONS_MAX_NODES;
    gateway->max_pending = config->max_pending
        && config->max_pending < CAGENT_ADDONS_MAX_PENDING
        ? config->max_pending : CAGENT_ADDONS_MAX_PENDING;
    gateway->invoke_timeout_ms = config->invoke_timeout_ms
        ? config->invoke_timeout_ms : CAGENT_ADDONS_INVOKE_TIMEOUT_MS;
    gateway->catalog_changed_fn = config->catalog_changed_fn;
    gateway->catalog_changed_user_data = config->catalog_changed_user_data;
    if (stack_provider_enabled(&config->stack_provider))
        gateway->stack_provider = config->stack_provider;
    if (pthread_mutex_init(&gateway->mutex, NULL) != 0) {
        free(gateway);
        return NULL;
    }
    if (pthread_condattr_init(&condition_attributes) != 0) goto mutex_error;
    if (pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC) != 0
        || pthread_cond_init(&gateway->send_condition,
                             &condition_attributes) != 0)
        goto condition_error;
    for (i = 0; i < CAGENT_ADDONS_MAX_PENDING; i++) {
        if (pthread_cond_init(&gateway->pending[i].condition,
                              &condition_attributes) != 0)
            goto pending_condition_error;
    }
    pthread_condattr_destroy(&condition_attributes);
    caddons_transport_set_event_fn(gateway->transport, transport_event, gateway);
    return gateway;

pending_condition_error:
    while (i > 0) pthread_cond_destroy(&gateway->pending[--i].condition);
    pthread_cond_destroy(&gateway->send_condition);
condition_error:
    pthread_condattr_destroy(&condition_attributes);
mutex_error:
    pthread_mutex_destroy(&gateway->mutex);
    free(gateway);
    return NULL;
}

int caddons_node_gateway_start(caddons_node_gateway_t *gateway)
{
    pthread_attr_t attributes;
    int rc;
    if (!gateway) return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&gateway->mutex);
    if (gateway->running) { pthread_mutex_unlock(&gateway->mutex); return CADDONS_OK; }
    gateway->running = true;
    pthread_mutex_unlock(&gateway->mutex);
    if (pthread_attr_init(&attributes) != 0) {
        pthread_mutex_lock(&gateway->mutex);
        gateway->running = false;
        pthread_mutex_unlock(&gateway->mutex);
        return CADDONS_ERR_INTERNAL;
    }
    rc = configure_send_stack(gateway, &attributes);
    if (rc != CADDONS_OK) {
        pthread_attr_destroy(&attributes);
        pthread_mutex_lock(&gateway->mutex);
        gateway->running = false;
        pthread_mutex_unlock(&gateway->mutex);
        return rc;
    }
    if (pthread_create(&gateway->send_thread, &attributes, send_worker, gateway) != 0) {
        pthread_attr_destroy(&attributes);
        release_send_stack(gateway);
        pthread_mutex_lock(&gateway->mutex); gateway->running = false;
        pthread_mutex_unlock(&gateway->mutex); return CADDONS_ERR_INTERNAL;
    }
    pthread_attr_destroy(&attributes);
    gateway->send_thread_started = true;
    rc = gateway->transport->ops->start_server(gateway->transport,
                                                gateway->bind_host,
                                                gateway->listen_port,
                                                gateway->ws_path);
    if (rc != CADDONS_OK) {
        caddons_node_gateway_stop(gateway);
        return rc;
    }
    return CADDONS_OK;
}

int caddons_node_gateway_stop(caddons_node_gateway_t *gateway)
{
    char route_prefixes[CAGENT_ADDONS_MAX_NODES][CAGENT_ADDONS_ROUTE_ID_SIZE + 1];
    uint64_t generations[CAGENT_ADDONS_MAX_NODES];
    size_t remove_count = 0;
    size_t i;
    if (!gateway) return CADDONS_ERR_INVALID;
    gateway->transport->ops->stop(gateway->transport);
    pthread_mutex_lock(&gateway->mutex);
    gateway->running = false;
    for (i = 0; i < gateway->max_pending; i++) {
        if (gateway->pending[i].active && !gateway->pending[i].done) {
            gateway->pending[i].done = true;
            gateway->pending[i].ok = false;
            gateway->pending[i].error = CADDONS_ERR_OFFLINE;
            pthread_cond_signal(&gateway->pending[i].condition);
        }
    }
    for (i = 0; i < gateway->max_nodes; i++) {
        node_entry_t *node = &gateway->nodes[i];
        if (!node->used || !node->online) continue;
        node->online = false;
        node->info.online = false;
        snprintf(route_prefixes[remove_count], sizeof(route_prefixes[remove_count]),
                 "node:%s:", node->info.node_id);
        generations[remove_count++] = node->connection_gen;
    }
    pthread_cond_broadcast(&gateway->send_condition);
    pthread_mutex_unlock(&gateway->mutex);
    if (gateway->send_thread_started) {
        pthread_join(gateway->send_thread, NULL);
        gateway->send_thread_started = false;
        release_send_stack(gateway);
    }
    for (i = 0; i < remove_count; i++)
        caddons_remote_catalog_remove_source(gateway->catalog, CADDONS_TOOL_NODE,
                                             route_prefixes[i], generations[i]);
    if (remove_count) notify_catalog(gateway);
    return CADDONS_OK;
}

void caddons_node_gateway_destroy(caddons_node_gateway_t *gateway)
{
    size_t i;
    if (!gateway) return;
    if (gateway->running || gateway->send_thread_started)
        caddons_node_gateway_stop(gateway);
    release_send_stack(gateway);
    caddons_transport_set_event_fn(gateway->transport, NULL, NULL);
    for (i = 0; i < CAGENT_ADDONS_MAX_PENDING; i++)
        pthread_cond_destroy(&gateway->pending[i].condition);
    pthread_cond_destroy(&gateway->send_condition);
    pthread_mutex_destroy(&gateway->mutex);
    free(gateway);
}

size_t caddons_node_gateway_list(const caddons_node_gateway_t *gateway,
                                 caddons_node_info_t *nodes, size_t capacity)
{
    caddons_node_gateway_t *mutable_gateway = (caddons_node_gateway_t *)gateway;
    size_t i;
    size_t count = 0;
    if (!gateway) return 0;
    pthread_mutex_lock(&mutable_gateway->mutex);
    for (i = 0; i < gateway->max_nodes; i++) {
        if (!gateway->nodes[i].used) continue;
        if (nodes && count < capacity) nodes[count] = gateway->nodes[i].info;
        count++;
    }
    pthread_mutex_unlock(&mutable_gateway->mutex);
    return count;
}

size_t caddons_node_gateway_active_count(const caddons_node_gateway_t *gateway)
{
    caddons_node_gateway_t *mutable_gateway = (caddons_node_gateway_t *)gateway;
    size_t i;
    size_t count = 0;
    if (!gateway) return 0;
    pthread_mutex_lock(&mutable_gateway->mutex);
    for (i = 0; i < gateway->max_nodes; i++)
        if (gateway->nodes[i].used && gateway->nodes[i].online) count++;
    pthread_mutex_unlock(&mutable_gateway->mutex);
    return count;
}
