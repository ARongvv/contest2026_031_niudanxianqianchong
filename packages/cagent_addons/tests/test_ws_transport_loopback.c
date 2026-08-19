/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/ws_transport.h"

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
    caddons_transport_peer_t *peer;
    uint64_t generation;
    size_t connected;
    size_t disconnected;
    size_t errors;
    size_t messages;
    char last_message[128];
} endpoint_state_t;

static uint64_t monotonic_ms(void)
{
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static void on_event(caddons_transport_t *transport,
                     const caddons_transport_event_t *event,
                     void *user_data)
{
    endpoint_state_t *state = user_data;
    (void)transport;
    pthread_mutex_lock(&state->mutex);
    if (event->type == CADDONS_TRANSPORT_CONNECTED) {
        state->peer = event->peer;
        state->generation = event->peer_generation;
        state->connected++;
    } else if (event->type == CADDONS_TRANSPORT_DATA) {
        assert(event->payload_len < sizeof(state->last_message));
        memcpy(state->last_message, event->payload, event->payload_len);
        state->last_message[event->payload_len] = '\0';
        state->messages++;
    } else if (event->type == CADDONS_TRANSPORT_DISCONNECTED) {
        state->disconnected++;
    } else if (event->type == CADDONS_TRANSPORT_ERROR) {
        state->errors++;
    }
    pthread_cond_broadcast(&state->condition);
    pthread_mutex_unlock(&state->mutex);
}

static void state_init(endpoint_state_t *state)
{
    memset(state, 0, sizeof(*state));
    assert(pthread_mutex_init(&state->mutex, NULL) == 0);
    assert(pthread_cond_init(&state->condition, NULL) == 0);
}

static void state_destroy(endpoint_state_t *state)
{
    pthread_cond_destroy(&state->condition);
    pthread_mutex_destroy(&state->mutex);
}

static void wait_counter(endpoint_state_t *state, size_t *counter,
                         size_t expected)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&state->mutex);
    while (*counter < expected)
        assert(pthread_cond_timedwait(&state->condition, &state->mutex,
                                      &deadline) == 0);
    pthread_mutex_unlock(&state->mutex);
}

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

static void test_bidirectional_and_reconnect(void)
{
    caddons_ws_transport_config_t server_config = {2, 1000, 1000};
    caddons_ws_transport_config_t client_config = {1, 1000, 1000};
    caddons_transport_t *server = caddons_ws_transport_create(&server_config);
    caddons_transport_t *client = caddons_ws_transport_create(&client_config);
    endpoint_state_t server_state;
    endpoint_state_t client_state;
    caddons_transport_peer_t *server_peer;
    caddons_transport_peer_t *client_peer;
    uint64_t first_server_generation;
    char endpoint[128];
    uint16_t port = unused_loopback_port();
    assert(server != NULL && client != NULL);
    state_init(&server_state);
    state_init(&client_state);
    caddons_transport_set_event_fn(server, on_event, &server_state);
    caddons_transport_set_event_fn(client, on_event, &client_state);
    assert(server->ops->start_server(server, "127.0.0.1", port, "/node")
           == CADDONS_OK);
    snprintf(endpoint, sizeof(endpoint), "ws://127.0.0.1:%u/node",
             (unsigned)port);
    assert(client->ops->start_client(client, endpoint) == CADDONS_OK);
    wait_counter(&server_state, &server_state.connected, 1);
    wait_counter(&client_state, &client_state.connected, 1);

    pthread_mutex_lock(&server_state.mutex);
    server_peer = server_state.peer;
    first_server_generation = server_state.generation;
    pthread_mutex_unlock(&server_state.mutex);
    pthread_mutex_lock(&client_state.mutex);
    client_peer = client_state.peer;
    pthread_mutex_unlock(&client_state.mutex);
    assert(server->ops->peer_generation(server, server_peer)
           == first_server_generation);
    assert(client->ops->send(client, client_peer, "masked-client", 13,
                             monotonic_ms() + 1000) == CADDONS_OK);
    wait_counter(&server_state, &server_state.messages, 1);
    assert(strcmp(server_state.last_message, "masked-client") == 0);
    assert(server->ops->send(server, server_peer, "plain-server", 12,
                             monotonic_ms() + 1000) == CADDONS_OK);
    wait_counter(&client_state, &client_state.messages, 1);
    assert(strcmp(client_state.last_message, "plain-server") == 0);
    assert(client->ops->keepalive(client, client_peer,
                                  monotonic_ms() + 1000) == CADDONS_OK);
    assert(client->ops->send(client, client_peer, "after-ping", 10,
                             monotonic_ms() + 1000) == CADDONS_OK);
    wait_counter(&server_state, &server_state.messages, 2);
    assert(strcmp(server_state.last_message, "after-ping") == 0);

    assert(client->ops->stop(client) == CADDONS_OK);
    wait_counter(&server_state, &server_state.disconnected, 1);
    assert(client->ops->start_client(client, endpoint) == CADDONS_OK);
    wait_counter(&server_state, &server_state.connected, 2);
    wait_counter(&client_state, &client_state.connected, 2);
    assert(server_state.generation > first_server_generation);
    assert(server_state.errors == 0 && client_state.errors == 0);

    assert(client->ops->stop(client) == CADDONS_OK);
    assert(server->ops->stop(server) == CADDONS_OK);
    caddons_ws_transport_destroy(client);
    caddons_ws_transport_destroy(server);
    state_destroy(&client_state);
    state_destroy(&server_state);
}

int main(void)
{
    test_bidirectional_and_reconnect();
    puts("test_ws_transport_loopback: OK");
    return 0;
}
