/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Transport-neutral peer/event contract.
 *
 * Threading and ownership rules:
 * - A WebSocket server uses one accept thread and one reader thread per peer.
 * - event_fn runs on a transport reader thread and MUST NOT block or invoke a
 *   device command. Slow work must be copied into an upper-layer worker queue.
 * - A DATA payload is borrowed and valid only for the duration of event_fn.
 * - send() is serialized per peer by the transport.
 * - stop() first interrupts blocking I/O, then joins every transport thread;
 *   no event is delivered after stop() returns.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cagent_addons/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct caddons_transport caddons_transport_t;
typedef struct caddons_transport_peer caddons_transport_peer_t;

/* Optional caller-owned storage for a pthread stack. allocation is the value
 * passed back to free_fn; stack is the aligned address passed to pthread. */
typedef struct {
    void *allocation;
    void *stack;
    size_t stack_size;
} caddons_thread_stack_t;

typedef int (*caddons_thread_stack_alloc_fn)(
    size_t stack_size,
    caddons_thread_stack_t *stack,
    void *user_data);
typedef void (*caddons_thread_stack_free_fn)(
    caddons_thread_stack_t *stack,
    void *user_data);

typedef struct {
    caddons_thread_stack_alloc_fn alloc_fn;
    caddons_thread_stack_free_fn free_fn;
    void *user_data;
} caddons_thread_stack_provider_t;

typedef enum {
    CADDONS_TRANSPORT_CONNECTED = 0,
    CADDONS_TRANSPORT_DATA,
    CADDONS_TRANSPORT_DISCONNECTED,
    CADDONS_TRANSPORT_ERROR,
} caddons_transport_event_type_t;

typedef struct {
    caddons_transport_event_type_t type;
    caddons_transport_peer_t *peer;
    uint64_t peer_generation; /* Transport-assigned; changes when a slot is reused. */
    const uint8_t *payload;
    size_t payload_len;
    int error;
} caddons_transport_event_t;

typedef void (*caddons_transport_event_fn)(
    caddons_transport_t *transport,
    const caddons_transport_event_t *event,
    void *user_data);

typedef struct {
    int (*start_client)(caddons_transport_t *transport, const char *endpoint);
    int (*start_server)(caddons_transport_t *transport,
                        const char *bind_host,
                        uint16_t port,
                        const char *path);
    int (*send)(caddons_transport_t *transport,
                caddons_transport_peer_t *peer,
                const void *payload,
                size_t payload_len,
                uint64_t deadline_ms);
    int (*close_peer)(caddons_transport_t *transport,
                      caddons_transport_peer_t *peer);
    /* Optional transport-native heartbeat (WS ping, MQTT keepalive, etc.). */
    int (*keepalive)(caddons_transport_t *transport,
                     caddons_transport_peer_t *peer,
                     uint64_t deadline_ms);
    uint64_t (*peer_generation)(caddons_transport_t *transport,
                                const caddons_transport_peer_t *peer);
    int (*stop)(caddons_transport_t *transport);
    void (*deinit)(caddons_transport_t *transport);
} caddons_transport_ops_t;

struct caddons_transport {
    const caddons_transport_ops_t *ops;
    void *context;
    caddons_transport_event_fn event_fn;
    void *event_user_data;
};

static inline void caddons_transport_set_event_fn(
    caddons_transport_t *transport,
    caddons_transport_event_fn event_fn,
    void *user_data)
{
    if (transport) {
        transport->event_fn = event_fn;
        transport->event_user_data = user_data;
    }
}

#ifdef __cplusplus
}
#endif
