/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Miloco 局域网明文 HTTP/1.0 客户端。
 *
 * 只服务固定端点的小 JSON 请求：请求行使用 HTTP/1.0，响应必然不带
 * chunked 编码，因此解析只需要状态行、Content-Length 与连接关闭。
 * 该客户端不引入 libcurl，也不复用 cAGENT 的 TLS 通道。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[64];
    uint16_t port;
    char token[64];
} smart_home_miloco_client_config_t;

/*
 * GET 请求。响应 body（去掉头部）写入 response 缓冲并 NUL 结尾。
 * http_status_out 非空时输出服务器状态码。缓冲不足返回 -ENOMEM；
 * 连接或读取失败返回负 errno。响应体为空时写入空串。
 */
int smart_home_miloco_http_get(const smart_home_miloco_client_config_t *config,
                               const char *path,
                               char *response,
                               size_t response_size,
                               int *http_status_out);

/* POST 请求，body 为 JSON 字符串。语义与 http_get 一致。 */
int smart_home_miloco_http_post(const smart_home_miloco_client_config_t *config,
                                const char *path,
                                const char *body,
                                char *response,
                                size_t response_size,
                                int *http_status_out);

#ifdef __cplusplus
}
#endif
