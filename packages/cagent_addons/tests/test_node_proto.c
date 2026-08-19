/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/node_proto.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t load_fixture(const char *name, char *buffer, size_t buffer_size)
{
    char path[512];
    FILE *file;
    size_t length;
    assert(snprintf(path, sizeof(path), "%s/%s", CADDONS_GOLDEN_DIR, name)
           < (int)sizeof(path));
    file = fopen(path, "rb");
    assert(file != NULL);
    length = fread(buffer, 1, buffer_size, file);
    assert(!ferror(file));
    assert(feof(file));
    fclose(file);
    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) length--;
    return length;
}

static void test_golden_frames(void)
{
    static const char *fixtures[] = {
        "connect_challenge.json", "connect_legacy.json", "connect_metadata.json",
        "hello_ok.json", "invoke_request.json", "invoke_result.json"
    };
    char json[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    char encoded[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    size_t i;
    for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
        caddons_node_frame_t frame;
        caddons_node_frame_t roundtrip;
        size_t length = load_fixture(fixtures[i], json, sizeof(json));
        size_t encoded_len;
        json[length] = '\0';
        assert(caddons_node_decode(json, length, &frame) == CADDONS_OK);
        assert(caddons_node_encode(&frame, encoded, sizeof(encoded), &encoded_len) == CADDONS_OK);
        assert(caddons_node_decode(encoded, encoded_len, &roundtrip) == CADDONS_OK);
        assert(roundtrip.type == frame.type);
        assert(strcmp(roundtrip.id, frame.id) == 0);
        assert(strcmp(roundtrip.name, frame.name) == 0);
        assert(roundtrip.ok == frame.ok);
    }
}

static void test_connect_compat_and_metadata(void)
{
    char json[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    char encoded[CAGENT_ADDONS_RECV_BUF_SIZE + 1];
    size_t length;
    size_t encoded_len;
    caddons_node_frame_t frame;
    caddons_node_frame_t encoded_frame;
    caddons_node_connect_t connect;
    caddons_node_connect_t roundtrip;
    length = load_fixture("connect_legacy.json", json, sizeof(json));
    assert(caddons_node_decode(json, length, &frame) == CADDONS_OK);
    assert(caddons_node_parse_connect(&frame, &connect) == CADDONS_OK);
    assert(connect.command_count == 2);
    assert(strcmp(connect.commands[0].input_schema_json,
                  "{\"type\":\"object\",\"properties\":{}}") == 0);
    assert(connect.commands[0].risk == CADDONS_TOOL_RISK_UNKNOWN);
    length = load_fixture("connect_metadata.json", json, sizeof(json));
    assert(caddons_node_decode(json, length, &frame) == CADDONS_OK);
    assert(caddons_node_parse_connect(&frame, &connect) == CADDONS_OK);
    assert(connect.commands[0].risk == CADDONS_TOOL_RISK_READ_ONLY);
    assert(connect.commands[0].timeout_ms == 3000);
    assert(connect.commands[1].risk == CADDONS_TOOL_RISK_SIDE_EFFECT);
    assert(strstr(connect.commands[1].input_schema_json, "required") != NULL);
    assert(caddons_node_encode_connect("roundtrip-1", &connect, encoded,
                                       sizeof(encoded), &encoded_len) == CADDONS_OK);
    assert(caddons_node_decode(encoded, encoded_len, &encoded_frame) == CADDONS_OK);
    assert(caddons_node_parse_connect(&encoded_frame, &roundtrip) == CADDONS_OK);
    assert(roundtrip.command_count == connect.command_count);
    assert(strcmp(roundtrip.commands[1].description,
                  connect.commands[1].description) == 0);
}

static void test_validation_and_names(void)
{
    static const char unknown[] =
        "{\"type\":\"evt\",\"event\":\"connect.challenge\","
        "\"payload\":{\"nonce\":\"n\"},\"future\":true}";
    static const char wrong_version[] =
        "{\"type\":\"req\",\"id\":\"1\",\"method\":\"connect\","
        "\"params\":{\"minProtocol\":4,\"maxProtocol\":4,"
        "\"client\":{\"id\":\"n\"},\"commands\":[],\"role\":\"node\"}}";
    char name[65];
    caddons_node_frame_t frame;
    caddons_node_connect_t connect;
    assert(caddons_node_decode(unknown, strlen(unknown), &frame) == CADDONS_OK);
    assert(caddons_node_decode("{} trailing", strlen("{} trailing"), &frame)
           == CADDONS_ERR_PARSE);
    assert(caddons_node_decode("{\"type\":\"req\",\"method\":\"connect\"}",
                               strlen("{\"type\":\"req\",\"method\":\"connect\"}"),
                               &frame) == CADDONS_ERR_PARSE);
    assert(caddons_node_decode(wrong_version, strlen(wrong_version), &frame) == CADDONS_OK);
    assert(caddons_node_parse_connect(&frame, &connect) == CADDONS_ERR_VERSION);
    assert(caddons_node_public_name("node", "temp.01", "get/value", name,
                                    sizeof(name)) == CADDONS_OK);
    assert(strcmp(name, "node_temp_01_get_value") == 0);
    assert(caddons_node_public_name("node", "12345678901234567890123456789012",
                                    "1234567890123456789012345678901234567890123456789012345678901234",
                                    name, sizeof(name)) == CADDONS_ERR_LIMIT);
}

int main(void)
{
    test_golden_frames();
    test_connect_compat_and_metadata();
    test_validation_and_names();
    puts("test_node_proto: OK");
    return 0;
}
