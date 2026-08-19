/* SPDX-License-Identifier: Apache-2.0 */
/** OpenClaw protocol-3 JSON codec. No cAGENT or NuttX dependency. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cagent_addons/errors.h"
#include "cagent_addons/limits.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CADDONS_NODE_FRAME_EVENT = 0,
    CADDONS_NODE_FRAME_REQUEST,
    CADDONS_NODE_FRAME_RESPONSE,
} caddons_node_frame_type_t;

typedef enum {
    CADDONS_TOOL_RISK_UNKNOWN = 0,
    CADDONS_TOOL_RISK_READ_ONLY,
    CADDONS_TOOL_RISK_SIDE_EFFECT,
    CADDONS_TOOL_RISK_DANGEROUS,
} caddons_tool_risk_t;

typedef struct {
    caddons_node_frame_type_t type;
    char id[CAGENT_ADDONS_FRAME_ID_SIZE + 1];
    char name[CAGENT_ADDONS_FRAME_NAME_SIZE + 1]; /* event or method */
    bool ok;
    char body_json[CAGENT_ADDONS_RECV_BUF_SIZE + 1]; /* payload/params/error */
} caddons_node_frame_t;

typedef struct {
    char command[CAGENT_ADDONS_COMMAND_NAME_SIZE + 1];
    char description[CAGENT_ADDONS_MAX_DESCRIPTION_SIZE + 1];
    char input_schema_json[CAGENT_ADDONS_MAX_SCHEMA_SIZE + 1];
    caddons_tool_risk_t risk;
    uint32_t timeout_ms;
} caddons_node_command_t;

typedef struct {
    uint32_t min_protocol;
    uint32_t max_protocol;
    char node_id[CAGENT_ADDONS_NODE_ID_SIZE + 1];
    char display_name[CAGENT_ADDONS_MAX_DESCRIPTION_SIZE + 1];
    char version[32];
    char platform[32];
    char device_family[32];
    char mode[16];
    char role[16];
    char auth_token[CAGENT_ADDONS_AUTH_TOKEN_MAX_LENGTH + 1];
    size_t command_count;
    caddons_node_command_t commands[CAGENT_ADDONS_MAX_NODE_COMMANDS];
} caddons_node_connect_t;

int caddons_node_decode(const char *json,
                        size_t json_len,
                        caddons_node_frame_t *frame);
int caddons_node_encode(const caddons_node_frame_t *frame,
                        char *output,
                        size_t output_size,
                        size_t *written);
int caddons_node_parse_connect(const caddons_node_frame_t *frame,
                               caddons_node_connect_t *connect);
int caddons_node_encode_connect(const char *request_id,
                                const caddons_node_connect_t *connect,
                                char *output,
                                size_t output_size,
                                size_t *written);

/* Replaces invalid bytes with '_'; never truncates and rejects empty names. */
int caddons_node_sanitize_name(const char *input,
                               char *output,
                               size_t output_size);
int caddons_node_public_name(const char *prefix,
                             const char *node_id,
                             const char *command,
                             char *output,
                             size_t output_size);
const char *caddons_tool_risk_name(caddons_tool_risk_t risk);

#ifdef __cplusplus
}
#endif
