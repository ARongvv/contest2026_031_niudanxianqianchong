/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/ws_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_accept_key_and_upgrade(void)
{
    static const char key[] = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept[32];
    char request[512];
    char response[512];
    char parsed_key[64];
    size_t request_len;
    size_t response_len;
    assert(caddons_ws_accept_key(key, accept, sizeof(accept)) == CADDONS_OK);
    assert(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
    assert(caddons_ws_build_client_upgrade("127.0.0.1", 18790, "/", key,
                                           request, sizeof(request), &request_len) == CADDONS_OK);
    assert(caddons_ws_parse_client_upgrade(request, request_len, "/", parsed_key,
                                           sizeof(parsed_key)) == CADDONS_OK);
    assert(strcmp(parsed_key, key) == 0);
    assert(caddons_ws_build_server_upgrade(key, response, sizeof(response),
                                           &response_len) == CADDONS_OK);
    assert(caddons_ws_validate_server_upgrade(response, response_len, key) == CADDONS_OK);
    response[response_len - 4] = 'X';
    assert(caddons_ws_validate_server_upgrade(response, response_len, key)
           == CADDONS_ERR_PROTOCOL);
}

static void test_masked_partial_and_coalesced(void)
{
    static const uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    static const char first[] = "hello";
    static const char second[] = "world";
    uint8_t wire[128];
    uint8_t storage[128];
    size_t first_len;
    size_t second_len;
    size_t i;
    caddons_ws_parser_t parser;
    caddons_ws_frame_t frame;
    assert(caddons_ws_write_frame(CADDONS_WS_CLIENT, CADDONS_WS_TEXT,
                                  first, strlen(first), mask, wire,
                                  sizeof(wire), &first_len) == CADDONS_OK);
    assert(caddons_ws_write_frame(CADDONS_WS_CLIENT, CADDONS_WS_TEXT,
                                  second, strlen(second), mask, wire + first_len,
                                  sizeof(wire) - first_len, &second_len) == CADDONS_OK);
    caddons_ws_parser_init(&parser, CADDONS_WS_SERVER, storage, sizeof(storage));
    for (i = 0; i < first_len - 1; i++) {
        assert(caddons_ws_parser_feed(&parser, wire + i, 1) == CADDONS_OK);
        assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_MORE);
    }
    assert(caddons_ws_parser_feed(&parser, wire + i, first_len + second_len - i)
           == CADDONS_OK);
    assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_OK);
    assert(frame.opcode == CADDONS_WS_TEXT && frame.payload_len == strlen(first));
    assert(memcmp(frame.payload, first, frame.payload_len) == 0);
    assert(caddons_ws_parser_feed(&parser, "x", 1) == CADDONS_ERR_BUSY);
    caddons_ws_parser_consume(&parser);
    assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_OK);
    assert(frame.payload_len == strlen(second));
    assert(memcmp(frame.payload, second, frame.payload_len) == 0);
    caddons_ws_parser_consume(&parser);
    assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_MORE);
}

static void test_protocol_rejections(void)
{
    uint8_t storage[32];
    uint8_t unmasked[] = {0x81, 0x01, 'x'};
    uint8_t fragmented[] = {0x01, 0x00};
    uint8_t oversized[] = {0x81, 126, 0x10, 0x01};
    uint8_t output[256];
    uint8_t payload[126] = {0};
    size_t written;
    caddons_ws_parser_t parser;
    caddons_ws_frame_t frame;
    caddons_ws_parser_init(&parser, CADDONS_WS_SERVER, storage, sizeof(storage));
    assert(caddons_ws_parser_feed(&parser, unmasked, sizeof(unmasked)) == CADDONS_OK);
    assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_ERR_PROTOCOL);
    caddons_ws_parser_reset(&parser);
    assert(caddons_ws_parser_feed(&parser, fragmented, sizeof(fragmented)) == CADDONS_OK);
    assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_ERR_PROTOCOL);
    caddons_ws_parser_init(&parser, CADDONS_WS_CLIENT, storage, sizeof(storage));
    assert(caddons_ws_parser_feed(&parser, oversized, sizeof(oversized)) == CADDONS_OK);
    assert(caddons_ws_parser_next(&parser, &frame) == CADDONS_ERR_LIMIT);
    assert(caddons_ws_write_frame(CADDONS_WS_SERVER, CADDONS_WS_PING,
                                  payload, sizeof(payload), NULL, output,
                                  sizeof(output), &written) == CADDONS_ERR_LIMIT);
}

int main(void)
{
    test_accept_key_and_upgrade();
    test_masked_partial_and_coalesced();
    test_protocol_rejections();
    puts("test_ws_frame: OK");
    return 0;
}
