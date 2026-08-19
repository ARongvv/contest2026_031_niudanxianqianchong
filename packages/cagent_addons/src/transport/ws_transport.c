/* SPDX-License-Identifier: Apache-2.0 */
/** Plain-TCP WebSocket transport for the bounded OpenClaw payload profile. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cagent_addons/ws_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "cagent_addons/limits.h"
#include "cagent_addons/ws_frame.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define WS_HTTP_MAX 2048
#define WS_HOST_MAX 127
#define WS_PATH_MAX 127
#define WS_ACCEPT_POLL_MS 250
#define WS_READ_POLL_MS 500
#define WS_IO_CHUNK 512

typedef struct ws_context ws_context_t;

struct caddons_transport_peer {
    ws_context_t *owner;
    pthread_mutex_t send_mutex;
    pthread_t reader_thread;
    caddons_thread_stack_t reader_stack;
    bool thread_started;
    bool joining;
    bool used;
    bool connected;
    int fd;
    uint64_t generation;
    caddons_ws_role_t role;
    uint8_t storage[CAGENT_ADDONS_WS_STORAGE_SIZE];
    caddons_ws_parser_t parser;
};

struct ws_context {
    caddons_transport_t transport;
    pthread_mutex_t mutex;
    pthread_t accept_thread;
    caddons_thread_stack_t accept_stack;
    caddons_thread_stack_provider_t stack_provider;
    bool accept_started;
    bool running;
    bool server_mode;
    int listen_fd;
    uint64_t next_generation;
    size_t max_peers;
    uint32_t connect_timeout_ms;
    uint32_t io_timeout_ms;
    char server_path[WS_PATH_MAX + 1];
    caddons_transport_peer_t peers[CAGENT_ADDONS_MAX_NODES];
};

static bool stack_provider_enabled(
    const caddons_thread_stack_provider_t *provider)
{
    return provider && provider->alloc_fn && provider->free_fn;
}

static void release_thread_stack(
    const caddons_thread_stack_provider_t *provider,
    caddons_thread_stack_t *stack)
{
    if (!stack) return;
    if (stack->allocation && stack_provider_enabled(provider))
        provider->free_fn(stack, provider->user_data);
    else
        memset(stack, 0, sizeof(*stack));
}

static int configure_thread_stack(
    pthread_attr_t *attributes,
    const caddons_thread_stack_provider_t *provider,
    caddons_thread_stack_t *stack,
    size_t stack_size)
{
    int rc;

    if (stack_provider_enabled(provider)) {
        memset(stack, 0, sizeof(*stack));
        rc = provider->alloc_fn(stack_size, stack, provider->user_data);
        if (rc != CADDONS_OK) return rc;
        if (!stack->allocation || !stack->stack || stack->stack_size < stack_size) {
            release_thread_stack(provider, stack);
            return CADDONS_ERR_INVALID;
        }
        if (pthread_attr_setstack(attributes, stack->stack, stack->stack_size) != 0) {
            release_thread_stack(provider, stack);
            return CADDONS_ERR_INVALID;
        }
        return CADDONS_OK;
    }

#ifdef __NuttX__
    if (pthread_attr_setstacksize(attributes, stack_size) != 0)
        return CADDONS_ERR_INVALID;
#else
    (void)stack;
    (void)stack_size;
#endif
    return CADDONS_OK;
}

static uint64_t monotonic_ms(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static int wait_fd(int fd, short events, uint64_t deadline_ms)
{
    struct pollfd descriptor;
    for (;;) {
        uint64_t now = monotonic_ms();
        uint64_t remaining;
        int timeout;
        int rc;
        if (deadline_ms && now >= deadline_ms) return CADDONS_ERR_TIMEOUT;
        remaining = deadline_ms ? deadline_ms - now : (uint64_t)INT_MAX;
        timeout = remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
        descriptor.fd = fd;
        descriptor.events = events;
        descriptor.revents = 0;
        rc = poll(&descriptor, 1, timeout);
        if (rc > 0) {
            if (descriptor.revents & events) return CADDONS_OK;
            return CADDONS_ERR_NETWORK;
        }
        if (rc == 0) return CADDONS_ERR_TIMEOUT;
        if (errno != EINTR) return CADDONS_ERR_NETWORK;
    }
}

static int random_bytes(uint8_t *output, size_t length)
{
    size_t offset = 0;
    int fd;
    if (!output) return CADDONS_ERR_INVALID;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return CADDONS_ERR_INTERNAL;
    while (offset < length) {
        ssize_t count = read(fd, output + offset, length - offset);
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

static int client_key(char output[25])
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t bytes[16];
    size_t in = 0;
    size_t out = 0;
    if (random_bytes(bytes, sizeof(bytes)) != CADDONS_OK)
        return CADDONS_ERR_INTERNAL;
    while (in < sizeof(bytes)) {
        size_t remain = sizeof(bytes) - in;
        uint32_t value = (uint32_t)bytes[in] << 16;
        if (remain > 1) value |= (uint32_t)bytes[in + 1] << 8;
        if (remain > 2) value |= bytes[in + 2];
        output[out++] = table[(value >> 18) & 63];
        output[out++] = table[(value >> 12) & 63];
        output[out++] = remain > 1 ? table[(value >> 6) & 63] : '=';
        output[out++] = remain > 2 ? table[value & 63] : '=';
        in += remain >= 3 ? 3 : remain;
    }
    output[out] = '\0';
    return CADDONS_OK;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return CADDONS_ERR_NETWORK;
    return CADDONS_OK;
}

static int raw_send_all(int fd, const void *payload, size_t length,
                        uint64_t deadline_ms)
{
    const uint8_t *bytes = payload;
    size_t offset = 0;
    while (offset < length) {
        ssize_t count;
        int rc = wait_fd(fd, POLLOUT, deadline_ms);
        if (rc != CADDONS_OK) return rc;
        count = send(fd, bytes + offset, length - offset, MSG_NOSIGNAL);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        else return CADDONS_ERR_NETWORK;
    }
    return CADDONS_OK;
}

static int read_http_headers(int fd, char *output, size_t output_size,
                             uint64_t deadline_ms, size_t *written)
{
    size_t used = 0;
    if (!output || output_size < 5 || !written) return CADDONS_ERR_INVALID;
    while (used + 1 < output_size) {
        ssize_t count;
        int rc = wait_fd(fd, POLLIN, deadline_ms);
        if (rc != CADDONS_OK) return rc;
        count = recv(fd, output + used, 1, 0);
        if (count == 1) used++;
        else if (count < 0 && errno == EINTR) continue;
        else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        else return CADDONS_ERR_NETWORK;
        if (used >= 4 && memcmp(output + used - 4, "\r\n\r\n", 4) == 0) {
            output[used] = '\0';
            *written = used;
            return CADDONS_OK;
        }
    }
    return CADDONS_ERR_LIMIT;
}

static int parse_endpoint(const char *endpoint, char *host, size_t host_size,
                          uint16_t *port, char *path, size_t path_size)
{
    const char *authority;
    const char *authority_end;
    const char *port_start = NULL;
    const char *host_start;
    const char *host_end;
    char port_text[8];
    char *end = NULL;
    unsigned long parsed_port = CAGENT_ADDONS_NODE_DEFAULT_PORT;
    size_t length;
    if (!endpoint || !host || !port || !path
        || strncmp(endpoint, "ws://", 5) != 0)
        return CADDONS_ERR_INVALID;
    authority = endpoint + 5;
    authority_end = strchr(authority, '/');
    if (!authority_end) authority_end = endpoint + strlen(endpoint);
    if (authority == authority_end) return CADDONS_ERR_PARSE;
    host_start = authority;
    host_end = authority_end;
    if (*authority == '[') {
        host_start++;
        host_end = memchr(host_start, ']', (size_t)(authority_end - host_start));
        if (!host_end || host_end == host_start) return CADDONS_ERR_PARSE;
        if (host_end + 1 < authority_end) {
            if (host_end[1] != ':') return CADDONS_ERR_PARSE;
            port_start = host_end + 2;
        }
    } else {
        const char *colon = memchr(authority, ':', (size_t)(authority_end - authority));
        if (colon) {
            if (memchr(colon + 1, ':', (size_t)(authority_end - colon - 1)))
                return CADDONS_ERR_PARSE;
            host_end = colon;
            port_start = colon + 1;
        }
    }
    length = (size_t)(host_end - host_start);
    if (length == 0 || length >= host_size) return CADDONS_ERR_LIMIT;
    memcpy(host, host_start, length);
    host[length] = '\0';
    if (port_start) {
        length = (size_t)(authority_end - port_start);
        if (length == 0 || length >= sizeof(port_text)) return CADDONS_ERR_PARSE;
        memcpy(port_text, port_start, length);
        port_text[length] = '\0';
        errno = 0;
        parsed_port = strtoul(port_text, &end, 10);
        if (errno || !end || *end || parsed_port == 0 || parsed_port > 65535)
            return CADDONS_ERR_PARSE;
    }
    *port = (uint16_t)parsed_port;
    if (*authority_end) {
        length = strlen(authority_end);
        if (length >= path_size) return CADDONS_ERR_LIMIT;
        memcpy(path, authority_end, length + 1);
    } else {
        if (path_size < 2) return CADDONS_ERR_LIMIT;
        strcpy(path, "/");
    }
    return CADDONS_OK;
}

static int connect_socket(const char *host, uint16_t port, uint64_t deadline_ms)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[8];
    int fd = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0 || !addresses)
        return -1;
    for (address = addresses; address; address = address->ai_next) {
        int error = 0;
        socklen_t error_size = sizeof(error);
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        if (set_nonblocking(fd) != CADDONS_OK) {
            close(fd);
            fd = -1;
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        if (errno != EINPROGRESS
            || wait_fd(fd, POLLOUT, deadline_ms) != CADDONS_OK
            || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0
            || error != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(addresses);
    return fd;
}

static int listen_socket(const char *bind_host, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[8];
    int fd = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    if (getaddrinfo(bind_host && bind_host[0] ? bind_host : NULL,
                    port_text, &hints, &addresses) != 0 || !addresses)
        return -1;
    for (address = addresses; address; address = address->ai_next) {
        int enabled = 1;
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (bind(fd, address->ai_addr, address->ai_addrlen) == 0
            && listen(fd, (int)CAGENT_ADDONS_MAX_NODES) == 0
            && set_nonblocking(fd) == CADDONS_OK)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}

static void emit_event(caddons_transport_peer_t *peer,
                       caddons_transport_event_type_t type,
                       const void *payload, size_t payload_len, int error)
{
    ws_context_t *context = peer->owner;
    caddons_transport_event_fn event_fn = context->transport.event_fn;
    caddons_transport_event_t event;
    if (!event_fn) return;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.peer = peer;
    event.peer_generation = peer->generation;
    event.payload = payload;
    event.payload_len = payload_len;
    event.error = error;
    event_fn(&context->transport, &event, context->transport.event_user_data);
}

static int ws_send_frame(caddons_transport_peer_t *peer,
                         caddons_ws_opcode_t opcode,
                         const void *payload, size_t payload_len,
                         uint64_t deadline_ms)
{
    uint8_t header[14];
    uint8_t mask[4];
    uint8_t chunk[WS_IO_CHUNK];
    size_t header_len = 0;
    size_t offset = 0;
    bool masked = peer->role == CADDONS_WS_CLIENT;
    int rc;
    if ((!payload && payload_len) || payload_len > CAGENT_ADDONS_RECV_BUF_SIZE
        || (opcode >= CADDONS_WS_CLOSE && payload_len > 125))
        return CADDONS_ERR_LIMIT;
    header[header_len++] = 0x80u | (uint8_t)opcode;
    if (payload_len < 126) {
        header[header_len++] = (masked ? 0x80u : 0) | (uint8_t)payload_len;
    } else {
        header[header_len++] = (masked ? 0x80u : 0) | 126u;
        header[header_len++] = (uint8_t)(payload_len >> 8);
        header[header_len++] = (uint8_t)payload_len;
    }
    if (masked) {
        if (random_bytes(mask, sizeof(mask)) != CADDONS_OK)
            return CADDONS_ERR_INTERNAL;
        memcpy(header + header_len, mask, sizeof(mask));
        header_len += sizeof(mask);
    }
    if (!deadline_ms) deadline_ms = monotonic_ms() + peer->owner->io_timeout_ms;
    pthread_mutex_lock(&peer->send_mutex);
    if (peer->fd < 0) {
        pthread_mutex_unlock(&peer->send_mutex);
        return CADDONS_ERR_OFFLINE;
    }
    rc = raw_send_all(peer->fd, header, header_len, deadline_ms);
    while (rc == CADDONS_OK && offset < payload_len) {
        size_t count = payload_len - offset;
        if (count > sizeof(chunk)) count = sizeof(chunk);
        if (masked) {
            size_t i;
            for (i = 0; i < count; i++)
                chunk[i] = ((const uint8_t *)payload)[offset + i]
                         ^ mask[(offset + i) & 3];
            rc = raw_send_all(peer->fd, chunk, count, deadline_ms);
        } else {
            rc = raw_send_all(peer->fd,
                              (const uint8_t *)payload + offset,
                              count, deadline_ms);
        }
        offset += count;
    }
    pthread_mutex_unlock(&peer->send_mutex);
    return rc;
}

static void shutdown_peer(caddons_transport_peer_t *peer)
{
    pthread_mutex_lock(&peer->send_mutex);
    if (peer->fd >= 0) shutdown(peer->fd, SHUT_RDWR);
    pthread_mutex_unlock(&peer->send_mutex);
}

static void close_peer_fd(caddons_transport_peer_t *peer)
{
    pthread_mutex_lock(&peer->send_mutex);
    if (peer->fd >= 0) {
        shutdown(peer->fd, SHUT_RDWR);
        close(peer->fd);
        peer->fd = -1;
    }
    pthread_mutex_unlock(&peer->send_mutex);
}

static void protocol_close(caddons_transport_peer_t *peer, int error)
{
    uint16_t code = error == CADDONS_ERR_LIMIT ? 1009u : 1002u;
    uint8_t payload[2] = {(uint8_t)(code >> 8), (uint8_t)code};
    ws_send_frame(peer, CADDONS_WS_CLOSE, payload, sizeof(payload),
                  monotonic_ms() + peer->owner->io_timeout_ms);
}

static bool context_running(ws_context_t *context)
{
    bool running;
    pthread_mutex_lock(&context->mutex);
    running = context->running;
    pthread_mutex_unlock(&context->mutex);
    return running;
}

static void *reader_worker(void *argument)
{
    caddons_transport_peer_t *peer = argument;
    ws_context_t *context = peer->owner;
    int disconnect_error = CADDONS_OK;
    emit_event(peer, CADDONS_TRANSPORT_CONNECTED, NULL, 0, CADDONS_OK);
    while (context_running(context)) {
        caddons_ws_frame_t frame;
        int rc = caddons_ws_parser_next(&peer->parser, &frame);
        if (rc == CADDONS_MORE) {
            uint8_t input[WS_IO_CHUNK];
            size_t available = peer->parser.capacity - peer->parser.used;
            ssize_t count;
            if (available == 0) {
                disconnect_error = CADDONS_ERR_LIMIT;
                break;
            }
            if (available > sizeof(input)) available = sizeof(input);
            rc = wait_fd(peer->fd, POLLIN, monotonic_ms() + WS_READ_POLL_MS);
            if (rc == CADDONS_ERR_TIMEOUT) continue;
            if (rc != CADDONS_OK) {
                disconnect_error = CADDONS_ERR_NETWORK;
                break;
            }
            count = recv(peer->fd, input, available, 0);
            if (count > 0) {
                rc = caddons_ws_parser_feed(&peer->parser, input, (size_t)count);
                if (rc != CADDONS_OK) {
                    disconnect_error = rc;
                    break;
                }
                continue;
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN
                              || errno == EWOULDBLOCK))
                continue;
            disconnect_error = CADDONS_ERR_NETWORK;
            break;
        }
        if (rc != CADDONS_OK) {
            disconnect_error = rc;
            protocol_close(peer, rc);
            break;
        }
        if (frame.opcode == CADDONS_WS_TEXT) {
            emit_event(peer, CADDONS_TRANSPORT_DATA,
                       frame.payload, frame.payload_len, CADDONS_OK);
        } else if (frame.opcode == CADDONS_WS_PING) {
            rc = ws_send_frame(peer, CADDONS_WS_PONG,
                               frame.payload, frame.payload_len,
                               monotonic_ms() + context->io_timeout_ms);
            if (rc != CADDONS_OK) {
                disconnect_error = rc;
                break;
            }
        } else if (frame.opcode == CADDONS_WS_CLOSE) {
            ws_send_frame(peer, CADDONS_WS_CLOSE,
                          frame.payload, frame.payload_len,
                          monotonic_ms() + context->io_timeout_ms);
            caddons_ws_parser_consume(&peer->parser);
            break;
        }
        caddons_ws_parser_consume(&peer->parser);
    }
    close_peer_fd(peer);
    pthread_mutex_lock(&context->mutex);
    peer->connected = false;
    pthread_mutex_unlock(&context->mutex);
    if (disconnect_error != CADDONS_OK
        && disconnect_error != CADDONS_ERR_NETWORK)
        emit_event(peer, CADDONS_TRANSPORT_ERROR, NULL, 0, disconnect_error);
    emit_event(peer, CADDONS_TRANSPORT_DISCONNECTED, NULL, 0,
               disconnect_error);
    return NULL;
}

static int create_reader(caddons_transport_peer_t *peer)
{
    pthread_attr_t attributes;
    int rc;

    if (pthread_attr_init(&attributes) != 0) return CADDONS_ERR_INTERNAL;
    rc = configure_thread_stack(&attributes, &peer->owner->stack_provider,
                                &peer->reader_stack,
                                CAGENT_ADDONS_TRANSPORT_STACK_SIZE);
    if (rc != CADDONS_OK) {
        pthread_attr_destroy(&attributes);
        return rc;
    }
    if (pthread_create(&peer->reader_thread, &attributes,
                       reader_worker, peer) != 0) {
        pthread_attr_destroy(&attributes);
        release_thread_stack(&peer->owner->stack_provider, &peer->reader_stack);
        return CADDONS_ERR_INTERNAL;
    }
    pthread_attr_destroy(&attributes);
    peer->thread_started = true;
    return CADDONS_OK;
}

static caddons_transport_peer_t *reserve_peer(ws_context_t *context,
                                              int fd,
                                              caddons_ws_role_t role)
{
    size_t i;
    caddons_transport_peer_t *peer = NULL;
    pthread_mutex_lock(&context->mutex);
    if (!context->running) {
        pthread_mutex_unlock(&context->mutex);
        return NULL;
    }
    for (i = 0; i < context->max_peers; i++) {
        if (!context->peers[i].used) {
            peer = &context->peers[i];
            peer->used = true;
            peer->connected = false;
            peer->fd = fd;
            peer->role = role;
            peer->generation = ++context->next_generation;
            caddons_ws_parser_init(&peer->parser, role,
                                   peer->storage, sizeof(peer->storage));
            break;
        }
    }
    pthread_mutex_unlock(&context->mutex);
    return peer;
}

static void release_unstarted_peer(caddons_transport_peer_t *peer)
{
    ws_context_t *context = peer->owner;
    close_peer_fd(peer);
    if (!peer->thread_started)
        release_thread_stack(&context->stack_provider, &peer->reader_stack);
    pthread_mutex_lock(&context->mutex);
    peer->connected = false;
    peer->used = false;
    pthread_mutex_unlock(&context->mutex);
}

static int server_handshake(caddons_transport_peer_t *peer, const char *path)
{
    char request[WS_HTTP_MAX + 1];
    char response[256];
    char key[64];
    size_t request_len;
    size_t response_len;
    uint64_t deadline = monotonic_ms() + peer->owner->connect_timeout_ms;
    int rc = read_http_headers(peer->fd, request, sizeof(request),
                               deadline, &request_len);
    if (rc != CADDONS_OK) {
        printf("[caddons_ws] read upgrade failed fd=%d rc=%d\n", peer->fd, rc);
        return rc;
    }
    rc = caddons_ws_parse_client_upgrade(request, request_len, path,
                                         key, sizeof(key));
    if (rc != CADDONS_OK) {
        printf("[caddons_ws] parse upgrade failed fd=%d rc=%d length=%u\n",
               peer->fd, rc, (unsigned int)request_len);
        return rc;
    }
    rc = caddons_ws_build_server_upgrade(key, response, sizeof(response),
                                         &response_len);
    if (rc != CADDONS_OK) {
        printf("[caddons_ws] build upgrade failed fd=%d rc=%d\n", peer->fd, rc);
        return rc;
    }
    rc = raw_send_all(peer->fd, response, response_len, deadline);
    if (rc != CADDONS_OK) {
        printf("[caddons_ws] send upgrade failed fd=%d rc=%d\n", peer->fd, rc);
    }
    return rc;
}

static int client_handshake(caddons_transport_peer_t *peer,
                            const char *host, uint16_t port, const char *path)
{
    char key[25];
    char request[512];
    char response[WS_HTTP_MAX + 1];
    size_t request_len;
    size_t response_len;
    uint64_t deadline = monotonic_ms() + peer->owner->connect_timeout_ms;
    int rc = client_key(key);
    if (rc != CADDONS_OK) return rc;
    rc = caddons_ws_build_client_upgrade(host, port, path, key,
                                         request, sizeof(request), &request_len);
    if (rc != CADDONS_OK) return rc;
    rc = raw_send_all(peer->fd, request, request_len, deadline);
    if (rc != CADDONS_OK) return rc;
    rc = read_http_headers(peer->fd, response, sizeof(response),
                           deadline, &response_len);
    if (rc != CADDONS_OK) return rc;
    return caddons_ws_validate_server_upgrade(response, response_len, key);
}

static void reap_finished_peers(ws_context_t *context)
{
    for (;;) {
        caddons_transport_peer_t *peer = NULL;
        pthread_t thread;
        size_t i;
        pthread_mutex_lock(&context->mutex);
        for (i = 0; i < context->max_peers; i++) {
            if (context->peers[i].used && context->peers[i].thread_started
                && !context->peers[i].connected && !context->peers[i].joining) {
                peer = &context->peers[i];
                peer->joining = true;
                thread = peer->reader_thread;
                break;
            }
        }
        pthread_mutex_unlock(&context->mutex);
        if (!peer) return;
        pthread_join(thread, NULL);
        release_thread_stack(&context->stack_provider, &peer->reader_stack);
        pthread_mutex_lock(&context->mutex);
        peer->thread_started = false;
        peer->joining = false;
        peer->used = false;
        pthread_mutex_unlock(&context->mutex);
    }
}

static void *accept_worker(void *argument)
{
    ws_context_t *context = argument;

    printf("[caddons_ws] accept worker started\n");
    while (context_running(context)) {
        struct pollfd descriptor;
        int listen_fd;
        int rc;
        reap_finished_peers(context);
        pthread_mutex_lock(&context->mutex);
        listen_fd = context->listen_fd;
        pthread_mutex_unlock(&context->mutex);
        if (listen_fd < 0) break;
        descriptor.fd = listen_fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        rc = poll(&descriptor, 1, WS_ACCEPT_POLL_MS);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) {
            printf("[caddons_ws] accept poll failed errno=%d\n", errno);
            break;
        }
        if (rc == 0) continue;
        if (!(descriptor.revents & POLLIN)) {
            printf("[caddons_ws] accept poll revents=0x%x\n",
                   (unsigned int)descriptor.revents);
            break;
        }
        for (;;) {
            caddons_transport_peer_t *peer;
            int fd = accept(listen_fd, NULL, NULL);
            if (fd < 0) {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("[caddons_ws] accept failed errno=%d\n", errno);
                }
                break;
            }
            if (set_nonblocking(fd) != CADDONS_OK) {
                printf("[caddons_ws] peer nonblocking setup failed fd=%d\n", fd);
                close(fd);
                break;
            }
            peer = reserve_peer(context, fd, CADDONS_WS_SERVER);
            if (!peer) {
                printf("[caddons_ws] peer capacity reached fd=%d\n", fd);
                close(fd);
                break;
            }
            rc = server_handshake(peer, context->server_path);
            if (rc != CADDONS_OK || !context_running(context)) {
                printf("[caddons_ws] server handshake failed fd=%d rc=%d\n",
                       fd, rc);
                release_unstarted_peer(peer);
                break;
            }
            pthread_mutex_lock(&context->mutex);
            peer->connected = true;
            pthread_mutex_unlock(&context->mutex);
            rc = create_reader(peer);
            if (rc != CADDONS_OK) {
                printf("[caddons_ws] peer reader creation failed fd=%d rc=%d\n",
                       fd, rc);
                release_unstarted_peer(peer);
                break;
            }
        }
    }
    return NULL;
}

static int transport_start_client(caddons_transport_t *transport,
                                  const char *endpoint)
{
    ws_context_t *context = transport ? transport->context : NULL;
    caddons_transport_peer_t *peer;
    char host[WS_HOST_MAX + 1];
    char path[WS_PATH_MAX + 1];
    uint16_t port;
    uint64_t deadline;
    int fd;
    int rc;
    if (!context) return CADDONS_ERR_INVALID;
    rc = parse_endpoint(endpoint, host, sizeof(host), &port, path, sizeof(path));
    if (rc != CADDONS_OK) return rc;
    pthread_mutex_lock(&context->mutex);
    if (context->running) {
        pthread_mutex_unlock(&context->mutex);
        return CADDONS_ERR_BUSY;
    }
    context->running = true;
    context->server_mode = false;
    pthread_mutex_unlock(&context->mutex);
    deadline = monotonic_ms() + context->connect_timeout_ms;
    fd = connect_socket(host, port, deadline);
    if (fd < 0) {
        rc = CADDONS_ERR_NETWORK;
        goto fail;
    }
    peer = reserve_peer(context, fd, CADDONS_WS_CLIENT);
    if (!peer) {
        close(fd);
        rc = CADDONS_ERR_LIMIT;
        goto fail;
    }
    rc = client_handshake(peer, host, port, path);
    if (rc != CADDONS_OK) {
        release_unstarted_peer(peer);
        goto fail;
    }
    pthread_mutex_lock(&context->mutex);
    peer->connected = true;
    pthread_mutex_unlock(&context->mutex);
    rc = create_reader(peer);
    if (rc != CADDONS_OK) {
        release_unstarted_peer(peer);
        goto fail;
    }
    return CADDONS_OK;

fail:
    pthread_mutex_lock(&context->mutex);
    context->running = false;
    pthread_mutex_unlock(&context->mutex);
    return rc;
}

static int create_accept_thread(ws_context_t *context)
{
    pthread_attr_t attributes;
    int rc;

    if (pthread_attr_init(&attributes) != 0) return CADDONS_ERR_INTERNAL;
    rc = configure_thread_stack(&attributes, &context->stack_provider,
                                &context->accept_stack,
                                CAGENT_ADDONS_TRANSPORT_STACK_SIZE);
    if (rc != CADDONS_OK) {
        pthread_attr_destroy(&attributes);
        return rc;
    }
    if (pthread_create(&context->accept_thread, &attributes,
                       accept_worker, context) != 0) {
        pthread_attr_destroy(&attributes);
        release_thread_stack(&context->stack_provider, &context->accept_stack);
        return CADDONS_ERR_INTERNAL;
    }
    pthread_attr_destroy(&attributes);
    context->accept_started = true;
    return CADDONS_OK;
}

static int transport_start_server(caddons_transport_t *transport,
                                  const char *bind_host, uint16_t port,
                                  const char *path)
{
    ws_context_t *context = transport ? transport->context : NULL;
    int fd;
    int rc;
    if (!context || !path || path[0] != '/' || strlen(path) > WS_PATH_MAX)
        return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&context->mutex);
    if (context->running) {
        pthread_mutex_unlock(&context->mutex);
        return CADDONS_ERR_BUSY;
    }
    pthread_mutex_unlock(&context->mutex);
    fd = listen_socket(bind_host, port);
    if (fd < 0) {
        printf("[caddons_ws] listen failed host=%s port=%u errno=%d\n",
               bind_host ? bind_host : "*", (unsigned int)port, errno);
        return CADDONS_ERR_NETWORK;
    }
    pthread_mutex_lock(&context->mutex);
    context->listen_fd = fd;
    context->running = true;
    context->server_mode = true;
    strcpy(context->server_path, path);
    pthread_mutex_unlock(&context->mutex);
    rc = create_accept_thread(context);
    if (rc != CADDONS_OK) {
        printf("[caddons_ws] accept worker creation failed rc=%d\n", rc);
        close(fd);
        pthread_mutex_lock(&context->mutex);
        context->listen_fd = -1;
        context->running = false;
        pthread_mutex_unlock(&context->mutex);
    }
    else {
        printf("[caddons_ws] server listening host=%s port=%u path=%s\n",
               bind_host ? bind_host : "*", (unsigned int)port, path);
    }
    return rc;
}

static int transport_send(caddons_transport_t *transport,
                          caddons_transport_peer_t *peer,
                          const void *payload, size_t payload_len,
                          uint64_t deadline_ms)
{
    ws_context_t *context = transport ? transport->context : NULL;
    if (!context || !peer || peer->owner != context)
        return CADDONS_ERR_INVALID;
    return ws_send_frame(peer, CADDONS_WS_TEXT,
                         payload, payload_len, deadline_ms);
}

static int transport_close_peer(caddons_transport_t *transport,
                                caddons_transport_peer_t *peer)
{
    ws_context_t *context = transport ? transport->context : NULL;
    if (!context || !peer || peer->owner != context)
        return CADDONS_ERR_INVALID;
    ws_send_frame(peer, CADDONS_WS_CLOSE, NULL, 0,
                  monotonic_ms() + context->io_timeout_ms);
    shutdown_peer(peer);
    return CADDONS_OK;
}

static int transport_keepalive(caddons_transport_t *transport,
                               caddons_transport_peer_t *peer,
                               uint64_t deadline_ms)
{
    ws_context_t *context = transport ? transport->context : NULL;
    if (!context || !peer || peer->owner != context)
        return CADDONS_ERR_INVALID;
    return ws_send_frame(peer, CADDONS_WS_PING, NULL, 0, deadline_ms);
}

static uint64_t transport_peer_generation(
    caddons_transport_t *transport, const caddons_transport_peer_t *peer)
{
    ws_context_t *context = transport ? transport->context : NULL;
    uint64_t generation;
    if (!context || !peer || peer->owner != context) return 0;
    pthread_mutex_lock(&context->mutex);
    generation = peer->generation;
    pthread_mutex_unlock(&context->mutex);
    return generation;
}

static int transport_stop(caddons_transport_t *transport)
{
    ws_context_t *context = transport ? transport->context : NULL;
    int listen_fd;
    size_t i;
    if (!context) return CADDONS_ERR_INVALID;
    pthread_mutex_lock(&context->mutex);
    context->running = false;
    listen_fd = context->listen_fd;
    context->listen_fd = -1;
    pthread_mutex_unlock(&context->mutex);
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
    }
    /* Interrupt a peer that is still inside the server-side HTTP Upgrade. */
    for (i = 0; i < context->max_peers; i++)
        if (context->peers[i].used) shutdown_peer(&context->peers[i]);
    if (context->accept_started) {
        pthread_join(context->accept_thread, NULL);
        context->accept_started = false;
        release_thread_stack(&context->stack_provider, &context->accept_stack);
    }
    for (i = 0; i < context->max_peers; i++)
        if (context->peers[i].used) shutdown_peer(&context->peers[i]);
    for (i = 0; i < context->max_peers; i++) {
        caddons_transport_peer_t *peer = &context->peers[i];
        bool joined = false;
        if (peer->thread_started && !peer->joining) {
            pthread_join(peer->reader_thread, NULL);
            joined = true;
        }
        if (joined)
            release_thread_stack(&context->stack_provider, &peer->reader_stack);
        close_peer_fd(peer);
        pthread_mutex_lock(&context->mutex);
        peer->thread_started = false;
        peer->joining = false;
        peer->connected = false;
        peer->used = false;
        pthread_mutex_unlock(&context->mutex);
    }
    return CADDONS_OK;
}

static void transport_deinit(caddons_transport_t *transport)
{
    ws_context_t *context = transport ? transport->context : NULL;
    size_t i;
    if (!context) return;
    transport_stop(transport);
    for (i = 0; i < CAGENT_ADDONS_MAX_NODES; i++)
        pthread_mutex_destroy(&context->peers[i].send_mutex);
    pthread_mutex_destroy(&context->mutex);
    free(context);
}

static const caddons_transport_ops_t g_ws_ops = {
    .start_client = transport_start_client,
    .start_server = transport_start_server,
    .send = transport_send,
    .close_peer = transport_close_peer,
    .keepalive = transport_keepalive,
    .peer_generation = transport_peer_generation,
    .stop = transport_stop,
    .deinit = transport_deinit,
};

caddons_transport_t *caddons_ws_transport_create(
    const caddons_ws_transport_config_t *config)
{
    ws_context_t *context = calloc(1, sizeof(*context));
    size_t i;
    if (!context) return NULL;
    context->listen_fd = -1;
    context->max_peers = config && config->max_peers
        && config->max_peers < CAGENT_ADDONS_MAX_NODES
        ? config->max_peers : CAGENT_ADDONS_MAX_NODES;
    context->connect_timeout_ms = config && config->connect_timeout_ms
        ? config->connect_timeout_ms : CAGENT_ADDONS_WS_CONNECT_TIMEOUT_MS;
    context->io_timeout_ms = config && config->io_timeout_ms
        ? config->io_timeout_ms : CAGENT_ADDONS_WS_IO_TIMEOUT_MS;
    if (config && stack_provider_enabled(&config->stack_provider))
        context->stack_provider = config->stack_provider;
    context->transport.ops = &g_ws_ops;
    context->transport.context = context;
    if (pthread_mutex_init(&context->mutex, NULL) != 0) {
        free(context);
        return NULL;
    }
    for (i = 0; i < CAGENT_ADDONS_MAX_NODES; i++) {
        context->peers[i].owner = context;
        context->peers[i].fd = -1;
        if (pthread_mutex_init(&context->peers[i].send_mutex, NULL) != 0) {
            while (i > 0)
                pthread_mutex_destroy(&context->peers[--i].send_mutex);
            pthread_mutex_destroy(&context->mutex);
            free(context);
            return NULL;
        }
    }
    return &context->transport;
}

void caddons_ws_transport_destroy(caddons_transport_t *transport)
{
    if (transport && transport->ops && transport->ops->deinit)
        transport->ops->deinit(transport);
}
