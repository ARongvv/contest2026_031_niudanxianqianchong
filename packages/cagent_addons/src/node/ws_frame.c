/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/ws_frame.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_HTTP_MAX 2048

typedef struct {
    uint32_t state[5];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} sha1_ctx_t;

static uint32_t rol32(uint32_t value, unsigned shift)
{
    return (value << shift) | (value >> (32u - shift));
}

static void sha1_transform(sha1_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    unsigned i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24)
             | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] << 8)
             | (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 80; i++)
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4];
    for (i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;
        uint32_t t;
        if (i < 20) {
            f = (b & c) | ((~b) & d); k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d; k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d; k = 0xca62c1d6u;
        }
        t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e;
}

static void sha1_init(sha1_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301u; ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu; ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xc3d2e1f0u;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len)
{
    ctx->bits += (uint64_t)len * 8u;
    while (len > 0) {
        size_t take = sizeof(ctx->block) - ctx->used;
        if (take > len) take = len;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take; data += take; len -= take;
        if (ctx->used == sizeof(ctx->block)) {
            sha1_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sha1_final(sha1_ctx_t *ctx, uint8_t digest[20])
{
    uint64_t bits = ctx->bits;
    unsigned i;
    ctx->block[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        memset(ctx->block + ctx->used, 0, 64 - ctx->used);
        sha1_transform(ctx, ctx->block);
        ctx->used = 0;
    }
    memset(ctx->block + ctx->used, 0, 56 - ctx->used);
    for (i = 0; i < 8; i++)
        ctx->block[63 - i] = (uint8_t)(bits >> (i * 8));
    sha1_transform(ctx, ctx->block);
    for (i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static int base64_encode(const uint8_t *input, size_t input_len,
                         char *output, size_t output_size)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((input_len + 2) / 3) * 4;
    size_t in = 0;
    size_t out = 0;
    if (!output || output_size <= need) return CADDONS_ERR_LIMIT;
    while (in < input_len) {
        size_t remain = input_len - in;
        uint32_t value = (uint32_t)input[in] << 16;
        if (remain > 1) value |= (uint32_t)input[in + 1] << 8;
        if (remain > 2) value |= input[in + 2];
        output[out++] = table[(value >> 18) & 63];
        output[out++] = table[(value >> 12) & 63];
        output[out++] = remain > 1 ? table[(value >> 6) & 63] : '=';
        output[out++] = remain > 2 ? table[value & 63] : '=';
        in += remain >= 3 ? 3 : remain;
    }
    output[out] = '\0';
    return CADDONS_OK;
}

static int ascii_equal_nocase(const char *a, const char *b, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static int header_value(const char *message, const char *name,
                        char *value, size_t value_size)
{
    const char *line = strstr(message, "\r\n");
    size_t name_len = strlen(name);
    if (!line) return CADDONS_ERR_PARSE;
    line += 2;
    while (*line) {
        const char *end = strstr(line, "\r\n");
        const char *start;
        size_t len;
        if (!end || end == line) break;
        if ((size_t)(end - line) > name_len && line[name_len] == ':'
            && ascii_equal_nocase(line, name, name_len)) {
            start = line + name_len + 1;
            while (start < end && (*start == ' ' || *start == '\t')) start++;
            len = (size_t)(end - start);
            while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
            if (len + 1 > value_size) return CADDONS_ERR_LIMIT;
            memcpy(value, start, len); value[len] = '\0';
            return CADDONS_OK;
        }
        line = end + 2;
    }
    return CADDONS_ERR_PROTOCOL;
}

static int token_contains_nocase(const char *value, const char *token)
{
    size_t token_len = strlen(token);
    const char *p = value;
    while (*p) {
        const char *end;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        end = p;
        while (*end && *end != ',') end++;
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if ((size_t)(end - p) == token_len && ascii_equal_nocase(p, token, token_len))
            return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}

void caddons_ws_parser_init(caddons_ws_parser_t *parser,
                            caddons_ws_role_t receiving_role,
                            uint8_t *storage, size_t storage_size)
{
    if (!parser) return;
    memset(parser, 0, sizeof(*parser));
    parser->storage = storage;
    parser->capacity = storage_size;
    parser->role = receiving_role;
}

void caddons_ws_parser_reset(caddons_ws_parser_t *parser)
{
    if (!parser) return;
    parser->used = 0; parser->frame_size = 0; parser->payload_offset = 0;
    parser->payload_len = 0; parser->frame_ready = false;
}

int caddons_ws_parser_feed(caddons_ws_parser_t *parser,
                           const void *data, size_t data_len)
{
    if (!parser || (!data && data_len) || !parser->storage)
        return CADDONS_ERR_INVALID;
    if (parser->frame_ready) return CADDONS_ERR_BUSY;
    if (data_len > parser->capacity - parser->used) return CADDONS_ERR_LIMIT;
    memcpy(parser->storage + parser->used, data, data_len);
    parser->used += data_len;
    return CADDONS_OK;
}

int caddons_ws_parser_next(caddons_ws_parser_t *parser,
                           caddons_ws_frame_t *frame)
{
    uint8_t b0, b1;
    uint64_t payload_len;
    size_t offset = 2;
    bool masked;
    size_t i;
    if (!parser || !frame || !parser->storage) return CADDONS_ERR_INVALID;
    if (parser->frame_ready) {
        frame->fin = true; frame->opcode = parser->opcode;
        frame->payload = parser->storage + parser->payload_offset;
        frame->payload_len = parser->payload_len;
        return CADDONS_OK;
    }
    if (parser->used < 2) return CADDONS_MORE;
    b0 = parser->storage[0]; b1 = parser->storage[1];
    if ((b0 & 0x70) != 0 || (b0 & 0x80) == 0) return CADDONS_ERR_PROTOCOL;
    parser->opcode = (caddons_ws_opcode_t)(b0 & 0x0f);
    if (parser->opcode != CADDONS_WS_TEXT && parser->opcode != CADDONS_WS_CLOSE
        && parser->opcode != CADDONS_WS_PING && parser->opcode != CADDONS_WS_PONG)
        return CADDONS_ERR_PROTOCOL;
    masked = (b1 & 0x80) != 0;
    if ((parser->role == CADDONS_WS_SERVER) != masked)
        return CADDONS_ERR_PROTOCOL;
    payload_len = b1 & 0x7f;
    if (payload_len == 126) {
        if (parser->used < 4) return CADDONS_MORE;
        payload_len = ((uint64_t)parser->storage[2] << 8) | parser->storage[3];
        if (payload_len < 126) return CADDONS_ERR_PROTOCOL;
        offset = 4;
    } else if (payload_len == 127) {
        if (parser->used < 10) return CADDONS_MORE;
        if (parser->storage[2] & 0x80) return CADDONS_ERR_PROTOCOL;
        payload_len = 0;
        for (i = 0; i < 8; i++) payload_len = (payload_len << 8) | parser->storage[2 + i];
        if (payload_len < 65536) return CADDONS_ERR_PROTOCOL;
        offset = 10;
    }
    if (parser->opcode >= CADDONS_WS_CLOSE && payload_len > 125)
        return CADDONS_ERR_PROTOCOL;
    if (payload_len > CAGENT_ADDONS_RECV_BUF_SIZE) return CADDONS_ERR_LIMIT;
    if (masked) offset += 4;
    if (offset > parser->capacity
        || payload_len > (uint64_t)(parser->capacity - offset))
        return CADDONS_ERR_LIMIT;
    if (parser->used < offset + (size_t)payload_len) return CADDONS_MORE;
    if (masked) {
        const uint8_t *mask = parser->storage + offset - 4;
        for (i = 0; i < (size_t)payload_len; i++)
            parser->storage[offset + i] ^= mask[i & 3];
    }
    parser->frame_size = offset + (size_t)payload_len;
    parser->payload_offset = offset;
    parser->payload_len = (size_t)payload_len;
    parser->frame_ready = true;
    frame->fin = true; frame->opcode = parser->opcode;
    frame->payload = parser->storage + offset; frame->payload_len = (size_t)payload_len;
    return CADDONS_OK;
}

void caddons_ws_parser_consume(caddons_ws_parser_t *parser)
{
    if (!parser || !parser->frame_ready) return;
    parser->used -= parser->frame_size;
    if (parser->used)
        memmove(parser->storage, parser->storage + parser->frame_size, parser->used);
    parser->frame_size = 0; parser->payload_offset = 0;
    parser->payload_len = 0; parser->frame_ready = false;
}

int caddons_ws_write_frame(caddons_ws_role_t sending_role,
                           caddons_ws_opcode_t opcode,
                           const void *payload, size_t payload_len,
                           const uint8_t mask_key[4], uint8_t *output,
                           size_t output_size, size_t *written)
{
    size_t header = 2;
    size_t i;
    bool masked = sending_role == CADDONS_WS_CLIENT;
    if (!output || !written || (!payload && payload_len)) return CADDONS_ERR_INVALID;
    if (opcode != CADDONS_WS_TEXT && opcode != CADDONS_WS_CLOSE
        && opcode != CADDONS_WS_PING && opcode != CADDONS_WS_PONG)
        return CADDONS_ERR_INVALID;
    if (opcode >= CADDONS_WS_CLOSE && payload_len > 125) return CADDONS_ERR_LIMIT;
    if (payload_len > CAGENT_ADDONS_RECV_BUF_SIZE) return CADDONS_ERR_LIMIT;
    if (masked && !mask_key) return CADDONS_ERR_INVALID;
    if (payload_len >= 126 && payload_len <= 65535) header += 2;
    else if (payload_len > 65535) header += 8;
    if (masked) header += 4;
    if (payload_len > output_size || header > output_size - payload_len)
        return CADDONS_ERR_LIMIT;
    i = 0; output[i++] = 0x80 | (uint8_t)opcode;
    if (payload_len < 126) output[i++] = (masked ? 0x80 : 0) | (uint8_t)payload_len;
    else if (payload_len <= 65535) {
        output[i++] = (masked ? 0x80 : 0) | 126;
        output[i++] = (uint8_t)(payload_len >> 8); output[i++] = (uint8_t)payload_len;
    } else {
        int shift;
        output[i++] = (masked ? 0x80 : 0) | 127;
        for (shift = 56; shift >= 0; shift -= 8) output[i++] = (uint8_t)(payload_len >> shift);
    }
    if (masked) { memcpy(output + i, mask_key, 4); i += 4; }
    for (header = 0; header < payload_len; header++) {
        uint8_t byte = ((const uint8_t *)payload)[header];
        output[i + header] = masked ? (uint8_t)(byte ^ mask_key[header & 3]) : byte;
    }
    *written = i + payload_len;
    return CADDONS_OK;
}

int caddons_ws_accept_key(const char *client_key, char *output, size_t output_size)
{
    sha1_ctx_t ctx;
    uint8_t digest[20];
    if (!client_key || !output) return CADDONS_ERR_INVALID;
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)client_key, strlen(client_key));
    sha1_update(&ctx, (const uint8_t *)WS_GUID, strlen(WS_GUID));
    sha1_final(&ctx, digest);
    return base64_encode(digest, sizeof(digest), output, output_size);
}

int caddons_ws_build_client_upgrade(const char *host, uint16_t port,
                                    const char *path, const char *client_key,
                                    char *output, size_t output_size, size_t *written)
{
    int len;
    if (!host || !path || path[0] != '/' || !client_key || !output || !written)
        return CADDONS_ERR_INVALID;
    len = snprintf(output, output_size,
                   "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\n"
                   "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
                   "Sec-WebSocket-Version: 13\r\n\r\n",
                   path, host, (unsigned)port, client_key);
    if (len < 0 || (size_t)len >= output_size) return CADDONS_ERR_LIMIT;
    *written = (size_t)len;
    return CADDONS_OK;
}

int caddons_ws_validate_server_upgrade(const char *response, size_t response_len,
                                       const char *client_key)
{
    char copy[WS_HTTP_MAX + 1];
    char actual[64];
    char expected[32];
    char upgrade[32];
    char connection[64];
    if (!response || !client_key || response_len > WS_HTTP_MAX)
        return CADDONS_ERR_INVALID;
    memcpy(copy, response, response_len); copy[response_len] = '\0';
    if (strncmp(copy, "HTTP/1.1 101 ", 13) != 0) return CADDONS_ERR_PROTOCOL;
    if (header_value(copy, "Upgrade", upgrade, sizeof(upgrade)) != CADDONS_OK
        || !token_contains_nocase(upgrade, "websocket")) return CADDONS_ERR_PROTOCOL;
    if (header_value(copy, "Connection", connection, sizeof(connection)) != CADDONS_OK
        || !token_contains_nocase(connection, "Upgrade")) return CADDONS_ERR_PROTOCOL;
    if (header_value(copy, "Sec-WebSocket-Accept", actual, sizeof(actual)) != CADDONS_OK)
        return CADDONS_ERR_PROTOCOL;
    if (caddons_ws_accept_key(client_key, expected, sizeof(expected)) != CADDONS_OK)
        return CADDONS_ERR_INTERNAL;
    return strcmp(actual, expected) == 0 ? CADDONS_OK : CADDONS_ERR_PROTOCOL;
}

int caddons_ws_parse_client_upgrade(const char *request, size_t request_len,
                                    const char *expected_path, char *client_key,
                                    size_t client_key_size)
{
    char copy[WS_HTTP_MAX + 1];
    char first[256];
    char expected[256];
    char upgrade[32];
    char connection[64];
    char version[16];
    const char *line_end;
    size_t first_len;
    if (!request || !expected_path || !client_key || request_len > WS_HTTP_MAX)
        return CADDONS_ERR_INVALID;
    memcpy(copy, request, request_len); copy[request_len] = '\0';
    line_end = strstr(copy, "\r\n");
    if (!line_end) return CADDONS_ERR_PARSE;
    first_len = (size_t)(line_end - copy);
    if (first_len >= sizeof(first)) return CADDONS_ERR_LIMIT;
    memcpy(first, copy, first_len); first[first_len] = '\0';
    if (snprintf(expected, sizeof(expected), "GET %s HTTP/1.1", expected_path)
        >= (int)sizeof(expected) || strcmp(first, expected) != 0)
        return CADDONS_ERR_PROTOCOL;
    if (header_value(copy, "Upgrade", upgrade, sizeof(upgrade)) != CADDONS_OK
        || !token_contains_nocase(upgrade, "websocket")) return CADDONS_ERR_PROTOCOL;
    if (header_value(copy, "Connection", connection, sizeof(connection)) != CADDONS_OK
        || !token_contains_nocase(connection, "Upgrade")) return CADDONS_ERR_PROTOCOL;
    if (header_value(copy, "Sec-WebSocket-Version", version, sizeof(version)) != CADDONS_OK
        || strcmp(version, "13") != 0) return CADDONS_ERR_PROTOCOL;
    return header_value(copy, "Sec-WebSocket-Key", client_key, client_key_size);
}

int caddons_ws_build_server_upgrade(const char *client_key, char *output,
                                    size_t output_size, size_t *written)
{
    char accept[32];
    int len;
    int rc = caddons_ws_accept_key(client_key, accept, sizeof(accept));
    if (rc != CADDONS_OK || !output || !written) return rc;
    len = snprintf(output, output_size,
                   "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                   "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                   accept);
    if (len < 0 || (size_t)len >= output_size) return CADDONS_ERR_LIMIT;
    *written = (size_t)len;
    return CADDONS_OK;
}
