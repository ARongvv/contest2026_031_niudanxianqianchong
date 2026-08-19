/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cagent_addons/limits.h"
#include "cagent_addons/remote_tool.h"
#include "cagent_addons/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct caddons_node_gateway caddons_node_gateway_t;

typedef struct {
    char node_id[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    char display_name[CAGENT_ADDONS_MAX_DESCRIPTION_SIZE + 1];
    char platform[32];
    char device_family[32];
    uint64_t connection_gen;
    size_t command_count;
    /* Original Node command names, for read-only presentation clients. */
    char commands[CAGENT_ADDONS_MAX_NODE_COMMANDS]
                 [CAGENT_ADDONS_COMMAND_NAME_SIZE + 1];
    bool online;
} caddons_node_info_t;

typedef void (*caddons_node_catalog_fn)(void *user_data);

typedef struct {
    caddons_transport_t *transport;
    caddons_remote_catalog_t *catalog;
    const char *bind_host;
    uint16_t listen_port;
    const char *ws_path;
    const char *auth_token;
    size_t max_nodes;
    size_t max_pending;
    uint32_t invoke_timeout_ms;
    caddons_node_catalog_fn catalog_changed_fn;
    void *catalog_changed_user_data;
    /* Optional provider for the gateway send-worker stack. */
    caddons_thread_stack_provider_t stack_provider;
} caddons_node_gateway_config_t;

caddons_node_gateway_t *caddons_node_gateway_create(
    const caddons_node_gateway_config_t *config);
int caddons_node_gateway_start(caddons_node_gateway_t *gateway);
int caddons_node_gateway_stop(caddons_node_gateway_t *gateway);
/* Call only after the app worker has applied all queued unregister mutations. */
void caddons_node_gateway_destroy(caddons_node_gateway_t *gateway);
size_t caddons_node_gateway_list(const caddons_node_gateway_t *gateway,
                                 caddons_node_info_t *nodes,
                                 size_t capacity);
size_t caddons_node_gateway_active_count(
    const caddons_node_gateway_t *gateway);

#ifdef __cplusplus
}
#endif
