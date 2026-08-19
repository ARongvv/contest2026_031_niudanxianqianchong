/* SPDX-License-Identifier: Apache-2.0 */
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
    CADDONS_WS_CLIENT = 0,
    CADDONS_WS_SERVER,
} caddons_ws_role_t;

typedef enum {
    CADDONS_WS_TEXT = 0x1,
    CADDONS_WS_CLOSE = 0x8,
    CADDONS_WS_PING = 0x9,
    CADDONS_WS_PONG = 0xa,
} caddons_ws_opcode_t;

typedef struct {
    bool fin;
    caddons_ws_opcode_t opcode;
    const uint8_t *payload;
    size_t payload_len;
} caddons_ws_frame_t;

typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t used;
    size_t frame_size;
    size_t payload_offset;
    size_t payload_len;
    caddons_ws_role_t role;
    caddons_ws_opcode_t opcode;
    bool frame_ready;
} caddons_ws_parser_t;

void caddons_ws_parser_init(caddons_ws_parser_t *parser,
                            caddons_ws_role_t receiving_role,
                            uint8_t *storage,
                            size_t storage_size);
void caddons_ws_parser_reset(caddons_ws_parser_t *parser);
int caddons_ws_parser_feed(caddons_ws_parser_t *parser,
                           const void *data,
                           size_t data_len);
int caddons_ws_parser_next(caddons_ws_parser_t *parser,
                           caddons_ws_frame_t *frame);
void caddons_ws_parser_consume(caddons_ws_parser_t *parser);

/* Client frames require a four-byte mask key; server frames pass NULL. */
int caddons_ws_write_frame(caddons_ws_role_t sending_role,
                           caddons_ws_opcode_t opcode,
                           const void *payload,
                           size_t payload_len,
                           const uint8_t mask_key[4],
                           uint8_t *output,
                           size_t output_size,
                           size_t *written);

int caddons_ws_accept_key(const char *client_key,
                          char *output,
                          size_t output_size);
int caddons_ws_build_client_upgrade(const char *host,
                                    uint16_t port,
                                    const char *path,
                                    const char *client_key,
                                    char *output,
                                    size_t output_size,
                                    size_t *written);
int caddons_ws_validate_server_upgrade(const char *response,
                                       size_t response_len,
                                       const char *client_key);
int caddons_ws_parse_client_upgrade(const char *request,
                                    size_t request_len,
                                    const char *expected_path,
                                    char *client_key,
                                    size_t client_key_size);
int caddons_ws_build_server_upgrade(const char *client_key,
                                    char *output,
                                    size_t output_size,
                                    size_t *written);

#ifdef __cplusplus
}
#endif
