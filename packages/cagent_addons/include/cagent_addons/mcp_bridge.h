/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Standard MCP Streamable HTTP tools-only client.
 *
 * The bridge uses MCP JSON-RPC directly; it does not use a private sidecar
 * protocol.  The application supplies bounded HTTP I/O so this module stays
 * independent of a particular NuttX/TLS transport implementation.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cagent_addons/remote_tool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct caddons_mcp_bridge caddons_mcp_bridge_t;

#define CAGENT_ADDONS_MCP_DEFAULT_PATH "/mcp"
#ifndef CAGENT_ADDONS_MCP_PROTOCOL_VERSION
#define CAGENT_ADDONS_MCP_PROTOCOL_VERSION "2025-03-26"
#endif
#define CAGENT_ADDONS_MCP_HOST_SIZE 255u
#define CAGENT_ADDONS_MCP_PATH_SIZE 96u
#define CAGENT_ADDONS_MCP_SERVER_ID_SIZE 32u
#define CAGENT_ADDONS_MCP_PROTOCOL_VERSION_SIZE 16u
#define CAGENT_ADDONS_MCP_SESSION_ID_SIZE 128u
#define CAGENT_ADDONS_MCP_AUTHORIZATION_SIZE 320u
#define CAGENT_ADDONS_MCP_EXTRA_HEADERS_SIZE 256u

typedef struct {
    const char *method;
    const char *host;
    uint16_t port;
    const char *path;
    const char *authorization;
    const char *extra_headers; /* 附加原始 HTTP header 行（如 "X-Key: v\r\n"），NULL = 无 */
    const char *mcp_protocol_version;
    const char *mcp_session_id;
    const char *request_body;
    uint64_t deadline_ms;
    bool is_tls; /* true = 使用 TLS(https)，false = 明文 http */
} caddons_http_request_t;

typedef struct {
    int http_status;
    const char *content_type;
    const char *mcp_session_id;
    char *response_body;
    size_t response_size;
    size_t response_len;
} caddons_http_response_t;

/* The callback owns no request strings. response_body is supplied by bridge. */
typedef int (*caddons_http_request_fn)(void *http_context,
                                       const caddons_http_request_t *request,
                                       caddons_http_response_t *response);

/* Catalog 变更通知：工具发现/移除后由 bridge 调用，应用层据此唤醒
 * 自己的 mutation worker（与 node_gateway 的 catalog_changed_fn 对齐）。 */
typedef void (*caddons_mcp_catalog_changed_fn)(void *user_data);

typedef struct {
    const char *name;
    caddons_tool_risk_t risk;
    uint32_t timeout_ms;
} caddons_mcp_tool_policy_t;

typedef struct {
    const char *host;
    uint16_t port;
    const char *path;
    const char *server_id;
    const char *protocol_version;
    const char *authorization;
    const char *extra_headers; /* 附加 header 行，NULL = 无 */
    bool use_tls; /* true = 通过 https 连接，false = 明文 http */
    const caddons_mcp_tool_policy_t *tool_policies;
    size_t tool_policy_count;
    caddons_remote_catalog_t *catalog;
    caddons_http_request_fn http_request;
    void *http_context;
    caddons_mcp_catalog_changed_fn catalog_changed_fn;
    void *catalog_changed_user_data;
    uint32_t request_timeout_ms;
} caddons_mcp_bridge_config_t;

caddons_mcp_bridge_t *caddons_mcp_bridge_create(
    const caddons_mcp_bridge_config_t *config);
int caddons_mcp_bridge_start(caddons_mcp_bridge_t *bridge);
int caddons_mcp_bridge_refresh(caddons_mcp_bridge_t *bridge,
                               uint64_t deadline_ms);
int caddons_mcp_bridge_stop(caddons_mcp_bridge_t *bridge);
void caddons_mcp_bridge_destroy(caddons_mcp_bridge_t *bridge);

#ifdef __cplusplus
}
#endif
