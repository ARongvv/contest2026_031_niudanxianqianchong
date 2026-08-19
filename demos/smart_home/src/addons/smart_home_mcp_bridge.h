/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pthread.h>
#include <stdint.h>

#include <agent.h>

typedef struct smart_home_mcp_bridge smart_home_mcp_bridge_t;

/* 手动发现状态机：start 返回后为 IDLE；应用显式请求发现后，操作 worker
 * 在独立大栈中收敛到 READY 或 FAILED。 */
typedef enum {
    SMART_HOME_MCP_STATE_IDLE = 0,
    SMART_HOME_MCP_STATE_CONNECTING,
    SMART_HOME_MCP_STATE_READY,
    SMART_HOME_MCP_STATE_FAILED
} smart_home_mcp_state_t;

/* Creates the bridge and starts an idle operation worker. No network I/O is
 * performed until smart_home_mcp_bridge_request_discover() is called. */
int smart_home_mcp_bridge_start(smart_home_mcp_bridge_t **bridge_out,
                                agent_t *agent,
                                pthread_mutex_t *agent_mutex);

/* Queue one synchronous MCP discovery operation on the bridge worker. The
 * caller returns immediately; the worker performs initialize/tools/list and
 * then applies catalog mutations. The LVGL thread must use this API instead
 * of performing network I/O directly. */
int smart_home_mcp_bridge_request_discover(smart_home_mcp_bridge_t *bridge);

/* Stops the bridge, drains queued unregister operations, and frees state.
 * Joins an in-flight discovery first (bounded by the network deadline). */
void smart_home_mcp_bridge_stop(smart_home_mcp_bridge_t **bridge_ptr);

/* Reads the current discovery state; last_error receives the CADDONS_*
 * discovery result when state is FAILED (0 otherwise). */
int smart_home_mcp_bridge_get_state(smart_home_mcp_bridge_t *bridge,
                                    smart_home_mcp_state_t *state,
                                    int *last_error);

/* Bounded wait for a requested discovery to reach a terminal state. */
int smart_home_mcp_bridge_wait_ready(smart_home_mcp_bridge_t *bridge,
                                     uint32_t timeout_ms);
