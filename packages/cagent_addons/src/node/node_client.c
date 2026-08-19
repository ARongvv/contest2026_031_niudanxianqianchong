/* SPDX-License-Identifier: Apache-2.0 */
/** Transport-neutral OpenClaw protocol-3 node client. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/node_client.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cagent_addons/cjson_compat.h"

#define NODE_ENDPOINT_SIZE 256
#define NODE_RECONNECT_MIN_MS 1000u
#define NODE_RECONNECT_MAX_MS 30000u
#define NODE_KEEPALIVE_MS 25000u

typedef struct {
    char invoke_id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    char node_id[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    char command[CAGENT_ADDONS_COMMAND_NAME_SIZE + 1];
    char arguments_json[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    uint64_t peer_generation;
} invoke_job_t;

struct caddons_node_client {
    caddons_transport_t *transport;
    char endpoint[NODE_ENDPOINT_SIZE];
    caddons_node_connect_t connect_metadata;
    caddons_node_command_fn handlers[CAGENT_ADDONS_MAX_NODE_COMMANDS];
    void *handler_data[CAGENT_ADDONS_MAX_NODE_COMMANDS];
    uint32_t reconnect_min_ms;
    uint32_t reconnect_max_ms;
    uint32_t keepalive_ms;
    caddons_node_state_fn state_fn;
    void *state_user_data;

    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t supervisor_thread;
    pthread_t command_thread;
    bool supervisor_started;
    bool command_started;
    bool running;
    bool connect_pending;
    caddons_node_client_state_t state;
    int state_reason;
    caddons_transport_peer_t *peer;
    uint64_t peer_generation;
    uint64_t next_request_id;
    char connect_request_id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    size_t queue_head;
    size_t queue_count;
    invoke_job_t queue[CAGENT_ADDONS_NODE_QUEUE_SIZE];
    invoke_job_t incoming_job;
    invoke_job_t active_job;
    caddons_node_frame_t incoming_frame;
    char result_json[CAGENT_ADDONS_MAX_RESULT_SIZE + 1];
    char wire_json[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
};

static uint64_t monotonic_ms(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static void deadline_timespec(uint64_t deadline_ms, struct timespec *value)
{
    value->tv_sec = (time_t)(deadline_ms / 1000u);
    value->tv_nsec = (long)(deadline_ms % 1000u) * 1000000L;
}

static bool bounded_string(const char *value, size_t capacity)
{
    return value && memchr(value, '\0', capacity) != NULL;
}

static int copy_string(char *output, size_t output_size, const char *input,
                       bool required)
{
    size_t length;
    if (!output || output_size == 0 || !input) return CADDONS_ERR_INVALID;
    length = strlen(input);
    if ((required && length == 0) || length >= output_size)
        return CADDONS_ERR_LIMIT;
    memcpy(output, input, length + 1);
    return CADDONS_OK;
}

static bool client_running(caddons_node_client_t *client)
{
    bool running;
    pthread_mutex_lock(&client->mutex);
    running = client->running;
    pthread_mutex_unlock(&client->mutex);
    return running;
}

static void transition_state(caddons_node_client_t *client,
                             caddons_node_client_state_t state, int reason)
{
    caddons_node_state_fn callback;
    void *user_data;
    bool changed;
    pthread_mutex_lock(&client->mutex);
    changed = client->state != state || client->state_reason != reason;
    client->state = state;
    client->state_reason = reason;
    callback = client->state_fn;
    user_data = client->state_user_data;
    pthread_cond_broadcast(&client->condition);
    pthread_mutex_unlock(&client->mutex);
    if (changed && callback) callback(state, reason, user_data);
}

static int random_u32(uint32_t *value)
{
    size_t offset = 0;
    int fd;
    if (!value) return CADDONS_ERR_INVALID;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return CADDONS_ERR_INTERNAL;
    while (offset < sizeof(*value)) {
        ssize_t count = read(fd, (uint8_t *)value + offset,
                             sizeof(*value) - offset);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else {
            close(fd);
            return CADDONS_ERR_INTERNAL;
        }
    }
    close(fd);
    return CADDONS_OK;
}

static int make_request_id(caddons_node_client_t *client,
                           char *output, size_t output_size)
{
    uint32_t random_value;
    uint64_t sequence;
    int length;
    if (random_u32(&random_value) != CADDONS_OK)
        random_value = (uint32_t)monotonic_ms();
    pthread_mutex_lock(&client->mutex);
    sequence = ++client->next_request_id;
    pthread_mutex_unlock(&client->mutex);
    length = snprintf(output, output_size, "%08lx-%08lx",
                      (unsigned long)random_value,
                      (unsigned long)(sequence & 0xffffffffu));
    return length < 0 || (size_t)length >= output_size
        ? CADDONS_ERR_LIMIT : CADDONS_OK;
}

static int send_wire(caddons_node_client_t *client,
                     const char *wire, size_t wire_length,
                     uint64_t expected_generation)
{
    caddons_transport_peer_t *peer;
    uint64_t generation;
    pthread_mutex_lock(&client->mutex);
    peer = client->peer;
    generation = client->peer_generation;
    pthread_mutex_unlock(&client->mutex);
    if (!peer || generation != expected_generation
        || client->transport->ops->peer_generation(client->transport, peer)
           != expected_generation)
        return CADDONS_ERR_OFFLINE;
    return client->transport->ops->send(client->transport, peer,
                                        wire, wire_length,
                                        monotonic_ms() + CAGENT_ADDONS_WS_IO_TIMEOUT_MS);
}

static int send_connect(caddons_node_client_t *client)
{
    char request_id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    uint64_t generation;
    size_t wire_length;
    int rc;
    pthread_mutex_lock(&client->mutex);
    generation = client->peer_generation;
    pthread_mutex_unlock(&client->mutex);
    rc = make_request_id(client, request_id, sizeof(request_id));
    if (rc != CADDONS_OK) return rc;
    pthread_mutex_lock(&client->mutex);
    strcpy(client->connect_request_id, request_id);
    pthread_mutex_unlock(&client->mutex);
    rc = caddons_node_encode_connect(request_id, &client->connect_metadata,
                                     client->wire_json,
                                     sizeof(client->wire_json), &wire_length);
    if (rc != CADDONS_OK) return rc;
    return send_wire(client, client->wire_json, wire_length, generation);
}

static caddons_node_command_fn find_handler(caddons_node_client_t *client,
                                            const char *command,
                                            void **user_data)
{
    size_t i;
    for (i = 0; i < client->connect_metadata.command_count; i++) {
        if (strcmp(client->connect_metadata.commands[i].command, command) == 0) {
            if (user_data) *user_data = client->handler_data[i];
            return client->handlers[i];
        }
    }
    return NULL;
}

static bool exact_json(const char *text)
{
    const char *end = NULL;
    size_t length;
    cJSON *value;
    if (!text) return false;
    length = strlen(text);
    value = cJSON_ParseWithLengthOpts(text, length, &end, 0);
    while (value && end < text + length
           && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end++;
    if (!value || end != text + length) {
        cJSON_Delete(value);
        return false;
    }
    cJSON_Delete(value);
    return true;
}

static int encode_result(caddons_node_client_t *client,
                         const invoke_job_t *job, int command_result,
                         size_t *wire_length)
{
    caddons_node_frame_t frame;
    cJSON *params = NULL;
    cJSON *error = NULL;
    char request_id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    bool ok = command_result == CADDONS_OK && exact_json(client->result_json);
    int rc = make_request_id(client, request_id, sizeof(request_id));
    if (rc != CADDONS_OK) return rc;
    params = cJSON_CreateObject();
    if (!params) return CADDONS_ERR_NOMEM;
    cJSON_AddStringToObject(params, "id", job->invoke_id);
    if (job->node_id[0]) cJSON_AddStringToObject(params, "nodeId", job->node_id);
    cJSON_AddBoolToObject(params, "ok", ok);
    if (ok) {
        cJSON_AddStringToObject(params, "payloadJSON", client->result_json);
    } else {
        const char *code = caddons_error_code(command_result == CADDONS_OK
                                              ? CADDONS_ERR_PARSE
                                              : command_result);
        error = cJSON_CreateObject();
        if (!error) {
            cJSON_Delete(params);
            return CADDONS_ERR_NOMEM;
        }
        cJSON_AddStringToObject(error, "code", code);
        cJSON_AddStringToObject(error, "message", code);
        cJSON_AddItemToObject(params, "error", error);
    }
    memset(&frame, 0, sizeof(frame));
    frame.type = CADDONS_NODE_FRAME_REQUEST;
    strcpy(frame.id, request_id);
    strcpy(frame.name, "node.invoke.result");
    if (!cJSON_PrintPreallocated(params, frame.body_json,
                                (int)sizeof(frame.body_json), 0)) {
        cJSON_Delete(params);
        return CADDONS_ERR_LIMIT;
    }
    cJSON_Delete(params);
    return caddons_node_encode(&frame, client->wire_json,
                               sizeof(client->wire_json), wire_length);
}

static void execute_job(caddons_node_client_t *client,
                        const invoke_job_t *job)
{
    caddons_node_command_fn handler;
    void *user_data = NULL;
    size_t wire_length;
    int rc;
    handler = find_handler(client, job->command, &user_data);
    client->result_json[0] = '\0';
    client->result_json[sizeof(client->result_json) - 1] = '\0';
    rc = handler ? handler(job->arguments_json, client->result_json,
                           sizeof(client->result_json), user_data)
                 : CADDONS_ERR_NOT_FOUND;
    client->result_json[sizeof(client->result_json) - 1] = '\0';
    if (encode_result(client, job, rc, &wire_length) == CADDONS_OK) {
        send_wire(client, client->wire_json, wire_length, job->peer_generation);
    } else {
        client->result_json[0] = '\0';
        if (encode_result(client, job, CADDONS_ERR_LIMIT, &wire_length)
            == CADDONS_OK)
            send_wire(client, client->wire_json, wire_length,
                      job->peer_generation);
    }
}

static void *command_worker(void *argument)
{
    caddons_node_client_t *client = argument;
    for (;;) {
        bool send_registration = false;
        bool have_job = false;
        pthread_mutex_lock(&client->mutex);
        while (client->running && !client->connect_pending
               && client->queue_count == 0)
            pthread_cond_wait(&client->condition, &client->mutex);
        if (!client->running) {
            pthread_mutex_unlock(&client->mutex);
            return NULL;
        }
        if (client->connect_pending) {
            client->connect_pending = false;
            send_registration = true;
        } else if (client->queue_count > 0) {
            client->active_job = client->queue[client->queue_head];
            client->queue_head = (client->queue_head + 1)
                               % CAGENT_ADDONS_NODE_QUEUE_SIZE;
            client->queue_count--;
            have_job = true;
        }
        pthread_mutex_unlock(&client->mutex);
        if (send_registration) {
            if (send_connect(client) != CADDONS_OK) {
                caddons_transport_peer_t *peer;
                pthread_mutex_lock(&client->mutex);
                peer = client->peer;
                pthread_mutex_unlock(&client->mutex);
                if (peer && client->transport->ops->close_peer)
                    client->transport->ops->close_peer(client->transport, peer);
            }
        } else if (have_job) {
            execute_job(client, &client->active_job);
        }
    }
}

static int json_to_buffer(cJSON *item, char *output, size_t output_size)
{
    if (!item) {
        if (output_size < 3) return CADDONS_ERR_LIMIT;
        strcpy(output, "{}");
        return CADDONS_OK;
    }
    if (!cJSON_PrintPreallocated(item, output, (int)output_size, 0))
        return CADDONS_ERR_LIMIT;
    return CADDONS_OK;
}

static int parse_invoke(const caddons_node_frame_t *frame,
                        uint64_t peer_generation, invoke_job_t *job)
{
    cJSON *params = cJSON_Parse(frame->body_json);
    cJSON *arguments;
    const char *value;
    int rc = CADDONS_ERR_PARSE;
    if (!params || !cJSON_IsObject(params)) goto out;
    memset(job, 0, sizeof(*job));
    value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params, "id"));
    if (!value || copy_string(job->invoke_id, sizeof(job->invoke_id), value, true)
                  != CADDONS_OK)
        goto out;
    value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params, "nodeId"));
    if (value && copy_string(job->node_id, sizeof(job->node_id), value, false)
                 != CADDONS_OK)
        goto out;
    value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params, "command"));
    if (!value || copy_string(job->command, sizeof(job->command), value, true)
                  != CADDONS_OK)
        goto out;
    arguments = cJSON_GetObjectItemCaseSensitive(params, "paramsJSON");
    if (cJSON_IsString(arguments)) {
        value = cJSON_GetStringValue(arguments);
        if (copy_string(job->arguments_json, sizeof(job->arguments_json),
                        value, false) != CADDONS_OK
            || !exact_json(job->arguments_json))
            goto out;
    } else {
        if (!arguments) arguments = cJSON_GetObjectItemCaseSensitive(params, "params");
        if (arguments && !cJSON_IsObject(arguments)) goto out;
        if (json_to_buffer(arguments, job->arguments_json,
                           sizeof(job->arguments_json)) != CADDONS_OK)
            goto out;
    }
    job->peer_generation = peer_generation;
    rc = CADDONS_OK;
out:
    cJSON_Delete(params);
    return rc;
}

static void queue_connect(caddons_node_client_t *client)
{
    pthread_mutex_lock(&client->mutex);
    if (client->running) {
        client->connect_pending = true;
        pthread_cond_broadcast(&client->condition);
    }
    pthread_mutex_unlock(&client->mutex);
}

static int queue_invoke(caddons_node_client_t *client,
                        const caddons_node_frame_t *frame,
                        uint64_t peer_generation)
{
    size_t tail;
    int rc = parse_invoke(frame, peer_generation, &client->incoming_job);
    if (rc != CADDONS_OK) return rc;
    pthread_mutex_lock(&client->mutex);
    if (!client->running || client->peer_generation != peer_generation) {
        pthread_mutex_unlock(&client->mutex);
        return CADDONS_ERR_OFFLINE;
    }
    if (client->queue_count >= CAGENT_ADDONS_NODE_QUEUE_SIZE) {
        pthread_mutex_unlock(&client->mutex);
        return CADDONS_ERR_BUSY;
    }
    tail = (client->queue_head + client->queue_count)
         % CAGENT_ADDONS_NODE_QUEUE_SIZE;
    client->queue[tail] = client->incoming_job;
    client->queue_count++;
    pthread_cond_broadcast(&client->condition);
    pthread_mutex_unlock(&client->mutex);
    return CADDONS_OK;
}

static bool hello_ok(caddons_node_client_t *client,
                     const caddons_node_frame_t *frame)
{
    cJSON *payload;
    const char *type;
    bool ok;
    char request_id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    if (!frame->ok) return false;
    pthread_mutex_lock(&client->mutex);
    strcpy(request_id, client->connect_request_id);
    pthread_mutex_unlock(&client->mutex);
    if (!request_id[0] || strcmp(frame->id, request_id) != 0) return false;
    payload = cJSON_Parse(frame->body_json);
    if (!payload) return false;
    type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "type"));
    ok = type && strcmp(type, "hello-ok") == 0;
    cJSON_Delete(payload);
    return ok;
}

static void transport_event(caddons_transport_t *transport,
                            const caddons_transport_event_t *event,
                            void *user_data)
{
    caddons_node_client_t *client = user_data;
    caddons_node_frame_t *frame;
    (void)transport;
    if (!client || !event || !event->peer) return;
    frame = &client->incoming_frame;
    if (event->type == CADDONS_TRANSPORT_CONNECTED) {
        pthread_mutex_lock(&client->mutex);
        client->peer = event->peer;
        client->peer_generation = event->peer_generation;
        pthread_mutex_unlock(&client->mutex);
        transition_state(client, CADDONS_NODE_WS_UP, CADDONS_OK);
    } else if (event->type == CADDONS_TRANSPORT_DISCONNECTED) {
        bool current;
        pthread_mutex_lock(&client->mutex);
        current = client->peer == event->peer
               && client->peer_generation == event->peer_generation;
        if (current) {
            client->peer = NULL;
            client->peer_generation = 0;
            client->connect_pending = false;
            client->connect_request_id[0] = '\0';
            client->queue_head = 0;
            client->queue_count = 0;
        }
        pthread_mutex_unlock(&client->mutex);
        if (current)
            transition_state(client, CADDONS_NODE_DISCONNECTED,
                             event->error ? event->error : CADDONS_ERR_NETWORK);
    } else if (event->type == CADDONS_TRANSPORT_ERROR) {
        transition_state(client, caddons_node_client_state(client), event->error);
    } else if (event->type == CADDONS_TRANSPORT_DATA
               && event->payload_len <= CAGENT_ADDONS_RECV_BUF_SIZE
               && caddons_node_decode((const char *)event->payload,
                                      event->payload_len, frame) == CADDONS_OK) {
        if (frame->type == CADDONS_NODE_FRAME_EVENT
            && strcmp(frame->name, "connect.challenge") == 0) {
            transition_state(client, CADDONS_NODE_CHALLENGED, CADDONS_OK);
            queue_connect(client);
        } else if ((frame->type == CADDONS_NODE_FRAME_EVENT
                    && strcmp(frame->name, "node.invoke.request") == 0)
                   || (frame->type == CADDONS_NODE_FRAME_REQUEST
                       && strcmp(frame->name, "node.invoke") == 0)) {
            queue_invoke(client, frame, event->peer_generation);
        } else if (frame->type == CADDONS_NODE_FRAME_RESPONSE) {
            if (hello_ok(client, frame))
                transition_state(client, CADDONS_NODE_REGISTERED, CADDONS_OK);
            else if (!frame->ok && client->transport->ops->close_peer)
                client->transport->ops->close_peer(client->transport, event->peer);
        }
    }
}

static void wait_interruptible(caddons_node_client_t *client, uint32_t delay_ms)
{
    struct timespec deadline;
    deadline_timespec(monotonic_ms() + delay_ms, &deadline);
    pthread_mutex_lock(&client->mutex);
    if (client->running)
        pthread_cond_timedwait(&client->condition, &client->mutex, &deadline);
    pthread_mutex_unlock(&client->mutex);
}

static uint32_t reconnect_delay(caddons_node_client_t *client, uint32_t base)
{
    uint32_t random_value = (uint32_t)monotonic_ms();
    uint32_t jitter_max = base / 4u;
    random_u32(&random_value);
    if (base >= client->reconnect_max_ms) return client->reconnect_max_ms;
    return base + (jitter_max ? random_value % (jitter_max + 1u) : 0);
}

static void *supervisor_worker(void *argument)
{
    caddons_node_client_t *client = argument;
    uint32_t backoff = client->reconnect_min_ms;
    while (client_running(client)) {
        int rc;
        transition_state(client, CADDONS_NODE_CONNECTING, CADDONS_OK);
        rc = client->transport->ops->start_client(client->transport,
                                                  client->endpoint);
        if (rc != CADDONS_OK) {
            transition_state(client, CADDONS_NODE_DISCONNECTED, rc);
        } else {
            backoff = client->reconnect_min_ms;
            while (client_running(client)) {
                struct timespec deadline;
                caddons_transport_peer_t *peer;
                uint64_t generation;
                int wait_rc;
                deadline_timespec(monotonic_ms() + client->keepalive_ms, &deadline);
                pthread_mutex_lock(&client->mutex);
                while (client->running
                       && client->state != CADDONS_NODE_DISCONNECTED) {
                    wait_rc = pthread_cond_timedwait(&client->condition,
                                                     &client->mutex, &deadline);
                    if (wait_rc == ETIMEDOUT) break;
                }
                if (!client->running) {
                    pthread_mutex_unlock(&client->mutex);
                    break;
                }
                if (client->state == CADDONS_NODE_DISCONNECTED) {
                    pthread_mutex_unlock(&client->mutex);
                    break;
                }
                peer = client->peer;
                generation = client->peer_generation;
                pthread_mutex_unlock(&client->mutex);
                if (peer && client->transport->ops->keepalive
                    && client->transport->ops->peer_generation(client->transport, peer)
                       == generation
                    && client->transport->ops->keepalive(
                           client->transport, peer,
                           monotonic_ms() + CAGENT_ADDONS_WS_IO_TIMEOUT_MS)
                       != CADDONS_OK
                    && client->transport->ops->close_peer)
                    client->transport->ops->close_peer(client->transport, peer);
            }
            client->transport->ops->stop(client->transport);
        }
        if (!client_running(client)) break;
        wait_interruptible(client, reconnect_delay(client, backoff));
        if (backoff < client->reconnect_max_ms) {
            uint64_t doubled = (uint64_t)backoff * 2u;
            backoff = doubled > client->reconnect_max_ms
                ? client->reconnect_max_ms : (uint32_t)doubled;
        }
    }
    client->transport->ops->stop(client->transport);
    return NULL;
}

static int create_thread(pthread_t *thread, void *(*entry)(void *), void *argument)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return CADDONS_ERR_INTERNAL;
#ifdef __NuttX__
    if (pthread_attr_setstacksize(&attributes,
                                  CAGENT_ADDONS_NODE_CLIENT_STACK_SIZE) != 0) {
        pthread_attr_destroy(&attributes);
        return CADDONS_ERR_INVALID;
    }
#endif
    if (pthread_create(thread, &attributes, entry, argument) != 0) {
        pthread_attr_destroy(&attributes);
        return CADDONS_ERR_INTERNAL;
    }
    pthread_attr_destroy(&attributes);
    return CADDONS_OK;
}

caddons_node_client_t *caddons_node_client_create(
    const caddons_node_client_config_t *config)
{
    caddons_node_client_t *client;
    pthread_condattr_t condition_attributes;
    size_t preflight_length;
    size_t i;
    if (!config || !config->transport || !config->transport->ops
        || !config->transport->ops->start_client
        || !config->transport->ops->send || !config->transport->ops->stop
        || !config->transport->ops->peer_generation
        || !config->transport->ops->close_peer
        || !config->endpoint || !config->auth_token || !config->auth_token[0]
        || !config->node_id || !config->display_name || !config->platform
        || !config->device_family || !config->version || !config->commands
        || config->command_count == 0
        || config->command_count > CAGENT_ADDONS_MAX_NODE_COMMANDS)
        return NULL;
    client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->transport = config->transport;
    if (copy_string(client->endpoint, sizeof(client->endpoint),
                    config->endpoint, true) != CADDONS_OK
        || copy_string(client->connect_metadata.auth_token,
                       sizeof(client->connect_metadata.auth_token),
                       config->auth_token, true) != CADDONS_OK
        || copy_string(client->connect_metadata.node_id,
                       sizeof(client->connect_metadata.node_id),
                       config->node_id, true) != CADDONS_OK
        || copy_string(client->connect_metadata.display_name,
                       sizeof(client->connect_metadata.display_name),
                       config->display_name, false) != CADDONS_OK
        || copy_string(client->connect_metadata.platform,
                       sizeof(client->connect_metadata.platform),
                       config->platform, false) != CADDONS_OK
        || copy_string(client->connect_metadata.device_family,
                       sizeof(client->connect_metadata.device_family),
                       config->device_family, false) != CADDONS_OK
        || copy_string(client->connect_metadata.version,
                       sizeof(client->connect_metadata.version),
                       config->version, false) != CADDONS_OK)
        goto create_error;
    strcpy(client->connect_metadata.mode, "node");
    strcpy(client->connect_metadata.role, "node");
    client->connect_metadata.min_protocol = CAGENT_ADDONS_NODE_PROTOCOL_VERSION;
    client->connect_metadata.max_protocol = CAGENT_ADDONS_NODE_PROTOCOL_VERSION;
    client->connect_metadata.command_count = config->command_count;
    for (i = 0; i < config->command_count; i++) {
        size_t j;
        const caddons_node_command_entry_t *entry = &config->commands[i];
        if (!entry->execute
            || !bounded_string(entry->metadata.command,
                               sizeof(entry->metadata.command))
            || !bounded_string(entry->metadata.description,
                               sizeof(entry->metadata.description))
            || !bounded_string(entry->metadata.input_schema_json,
                               sizeof(entry->metadata.input_schema_json))
            || !entry->metadata.command[0] || !entry->metadata.description[0]
            || !entry->metadata.input_schema_json[0]
            || entry->metadata.risk == CADDONS_TOOL_RISK_UNKNOWN
            || entry->metadata.timeout_ms < 100
            || entry->metadata.timeout_ms > 60000)
            goto create_error;
        for (j = 0; j < i; j++)
            if (strcmp(config->commands[j].metadata.command,
                       entry->metadata.command) == 0)
                goto create_error;
        client->connect_metadata.commands[i] = entry->metadata;
        client->handlers[i] = entry->execute;
        client->handler_data[i] = entry->user_data;
    }
    if (caddons_node_encode_connect("preflight", &client->connect_metadata,
                                    client->wire_json,
                                    sizeof(client->wire_json),
                                    &preflight_length) != CADDONS_OK)
        goto create_error;
    client->reconnect_min_ms = config->reconnect_min_ms
        ? config->reconnect_min_ms : NODE_RECONNECT_MIN_MS;
    client->reconnect_max_ms = config->reconnect_max_ms
        ? config->reconnect_max_ms : NODE_RECONNECT_MAX_MS;
    if (client->reconnect_max_ms < client->reconnect_min_ms)
        goto create_error;
    client->keepalive_ms = config->keepalive_ms
        ? config->keepalive_ms : NODE_KEEPALIVE_MS;
    client->state_fn = config->state_fn;
    client->state_user_data = config->state_user_data;
    client->state = CADDONS_NODE_DISCONNECTED;
    if (pthread_mutex_init(&client->mutex, NULL) != 0) goto create_error;
    if (pthread_condattr_init(&condition_attributes) != 0) goto mutex_error;
    if (pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC) != 0
        || pthread_cond_init(&client->condition, &condition_attributes) != 0) {
        pthread_condattr_destroy(&condition_attributes);
        goto mutex_error;
    }
    pthread_condattr_destroy(&condition_attributes);
    caddons_transport_set_event_fn(client->transport, transport_event, client);
    return client;

mutex_error:
    pthread_mutex_destroy(&client->mutex);
create_error:
    memset(client, 0, sizeof(*client));
    free(client);
    return NULL;
}

int caddons_node_client_start(caddons_node_client_t *client)
{
    int rc;
    if (!client) return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&client->mutex);
    if (client->running) {
        pthread_mutex_unlock(&client->mutex);
        return CADDONS_OK;
    }
    client->running = true;
    client->queue_head = 0;
    client->queue_count = 0;
    client->connect_pending = false;
    pthread_mutex_unlock(&client->mutex);
    rc = create_thread(&client->command_thread, command_worker, client);
    if (rc != CADDONS_OK) goto start_error;
    client->command_started = true;
    rc = create_thread(&client->supervisor_thread, supervisor_worker, client);
    if (rc != CADDONS_OK) {
        pthread_mutex_lock(&client->mutex);
        client->running = false;
        pthread_cond_broadcast(&client->condition);
        pthread_mutex_unlock(&client->mutex);
        pthread_join(client->command_thread, NULL);
        client->command_started = false;
        goto start_error;
    }
    client->supervisor_started = true;
    return CADDONS_OK;

start_error:
    pthread_mutex_lock(&client->mutex);
    client->running = false;
    pthread_cond_broadcast(&client->condition);
    pthread_mutex_unlock(&client->mutex);
    transition_state(client, CADDONS_NODE_DISCONNECTED, rc);
    return rc;
}

int caddons_node_client_stop(caddons_node_client_t *client)
{
    if (!client) return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&client->mutex);
    if (!client->running && !client->supervisor_started
        && !client->command_started) {
        pthread_mutex_unlock(&client->mutex);
        return CADDONS_OK;
    }
    client->running = false;
    pthread_cond_broadcast(&client->condition);
    pthread_mutex_unlock(&client->mutex);
    transition_state(client, CADDONS_NODE_STOPPING, CADDONS_OK);
    if (client->supervisor_started) {
        pthread_join(client->supervisor_thread, NULL);
        client->supervisor_started = false;
    } else {
        client->transport->ops->stop(client->transport);
    }
    if (client->command_started) {
        pthread_join(client->command_thread, NULL);
        client->command_started = false;
    }
    pthread_mutex_lock(&client->mutex);
    client->peer = NULL;
    client->peer_generation = 0;
    client->queue_head = 0;
    client->queue_count = 0;
    client->connect_pending = false;
    pthread_mutex_unlock(&client->mutex);
    transition_state(client, CADDONS_NODE_DISCONNECTED, CADDONS_OK);
    return CADDONS_OK;
}

void caddons_node_client_destroy(caddons_node_client_t *client)
{
    if (!client) return;
    caddons_node_client_stop(client);
    caddons_transport_set_event_fn(client->transport, NULL, NULL);
    pthread_cond_destroy(&client->condition);
    pthread_mutex_destroy(&client->mutex);
    memset(client->connect_metadata.auth_token, 0,
           sizeof(client->connect_metadata.auth_token));
    free(client);
}

caddons_node_client_state_t caddons_node_client_state(
    const caddons_node_client_t *client)
{
    caddons_node_client_t *mutable_client = (caddons_node_client_t *)client;
    caddons_node_client_state_t state;
    if (!client) return CADDONS_NODE_DISCONNECTED;
    pthread_mutex_lock(&mutable_client->mutex);
    state = client->state;
    pthread_mutex_unlock(&mutable_client->mutex);
    return state;
}
