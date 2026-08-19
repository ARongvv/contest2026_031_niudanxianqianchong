/* SPDX-License-Identifier: Apache-2.0 */
/** Minimal RFC 6455 transport over plain TCP. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cagent_addons/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t max_peers;          /* Server peers; clamped to the compiled maximum. */
    uint32_t connect_timeout_ms;
    uint32_t io_timeout_ms;    /* Handshake/control-frame deadline. */
    /* Optional provider for accept and reader task stacks. Both callbacks
     * must be supplied; otherwise the transport uses its default stacks. */
    caddons_thread_stack_provider_t stack_provider;
} caddons_ws_transport_config_t;

/*
 * A transport instance runs as either a client or a server until stop().
 * It may be started again after stop(). The caller owns the returned object.
 */
caddons_transport_t *caddons_ws_transport_create(
    const caddons_ws_transport_config_t *config);
void caddons_ws_transport_destroy(caddons_transport_t *transport);

#ifdef __cplusplus
}
#endif
