/* SPDX-License-Identifier: Apache-2.0 */
/** Node-role client. This layer does not depend on cAGENT. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cagent_addons/node_proto.h"
#include "cagent_addons/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct caddons_node_client caddons_node_client_t;

typedef enum {
    CADDONS_NODE_DISCONNECTED = 0,
    CADDONS_NODE_CONNECTING,
    CADDONS_NODE_WS_UP,
    CADDONS_NODE_CHALLENGED,
    CADDONS_NODE_REGISTERED,
    CADDONS_NODE_STOPPING,
} caddons_node_client_state_t;

typedef int (*caddons_node_command_fn)(const char *arguments_json,
                                       char *result_json,
                                       size_t result_size,
                                       void *user_data);

typedef struct {
    caddons_node_command_t metadata;
    caddons_node_command_fn execute;
    void *user_data;
} caddons_node_command_entry_t;

typedef void (*caddons_node_state_fn)(caddons_node_client_state_t state,
                                      int reason,
                                      void *user_data);

typedef struct {
    caddons_transport_t *transport;
    const char *endpoint; /* ws://host:port/path; plain WS in this release. */
    const char *auth_token; /* Runtime secret; never log or store in defaults. */
    const char *node_id;
    const char *display_name;
    const char *platform;
    const char *device_family;
    const char *version;
    /* Metadata is deep-copied; execute/user_data must remain valid until destroy. */
    const caddons_node_command_entry_t *commands;
    size_t command_count;
    uint32_t reconnect_min_ms;
    uint32_t reconnect_max_ms;
    uint32_t keepalive_ms;
    /* Runs on an internal transport/supervisor thread and must not block. */
    caddons_node_state_fn state_fn;
    void *state_user_data;
} caddons_node_client_config_t;

caddons_node_client_t *caddons_node_client_create(
    const caddons_node_client_config_t *config);
int caddons_node_client_start(caddons_node_client_t *client);
/* Joins transport threads and the current command handler, if one is running. */
int caddons_node_client_stop(caddons_node_client_t *client);
void caddons_node_client_destroy(caddons_node_client_t *client);
caddons_node_client_state_t caddons_node_client_state(
    const caddons_node_client_t *client);

#ifdef __cplusplus
}
#endif
