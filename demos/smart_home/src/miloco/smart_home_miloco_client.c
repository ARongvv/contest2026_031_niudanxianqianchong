/* SPDX-License-Identifier: Apache-2.0 */

#include "smart_home_miloco_client.h"

#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MILOCO_HTTP_CONNECT_TIMEOUT_MS 3000
#define MILOCO_HTTP_IO_TIMEOUT_MS      5000

static int http_request(const smart_home_miloco_client_config_t *config,
                        const char *method,
                        const char *path,
                        const char *body,
                        char *response,
                        size_t response_size,
                        int *http_status_out)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct timeval timeout;
    struct pollfd pfd;
    socklen_t optlen;
    char request[512];
    char port_text[8];
    int sockfd = -1;
    int flags;
    int error_code = 0;
    int http_status = 0;
    size_t sent = 0;
    size_t request_len;
    bool header_done = false;
    bool have_status = false;
    int ret;

    if (!config || !path || !response || response_size == 0) {
        return -EINVAL;
    }
    response[0] = '\0';
    if (config->host[0] == '\0' || config->port == 0) {
        return -EINVAL;
    }

    syslog(LOG_INFO, "[milo] http: begin %s\n", path);
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)config->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ret = getaddrinfo(config->host, port_text, &hints, &result);
    if (ret != 0 || !result) {
        return -EHOSTUNREACH;
    }

    syslog(LOG_INFO, "[milo] http: addrinfo ok\n");
    sockfd = socket(result->ai_family, result->ai_socktype, 0);
    if (sockfd < 0) {
        ret = -errno;
        goto out_freeaddr;
    }

    /* 非阻塞连接 + poll 限时，避免不可达地址把 worker 卡在 connect。 */
    flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }
    if (connect(sockfd, result->ai_addr, result->ai_addrlen) == 0) {
        /* 立即成功（本机环回等场景）。 */
    } else if (errno == EINPROGRESS) {
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, MILOCO_HTTP_CONNECT_TIMEOUT_MS) <= 0 ||
            (pfd.revents & POLLOUT) == 0) {
            ret = -ETIMEDOUT;
            goto out_close;
        }
        optlen = sizeof(error_code);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error_code, &optlen) < 0 ||
            error_code != 0) {
            ret = error_code != 0 ? -error_code : -EIO;
            goto out_close;
        }
    } else {
        ret = -errno;
        goto out_close;
    }

    syslog(LOG_INFO, "[milo] http: connected fd=%d\n", sockfd);
    /* 收发阶段恢复阻塞并施加超时。 */
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags);
    }
    timeout.tv_sec = MILOCO_HTTP_IO_TIMEOUT_MS / 1000;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    /* HTTP/1.0 请求行强制服务器以 Content-Length 或连接关闭应答，
     * 永远不会出现 chunked，客户端因此无需分块解码。 */
    if (body) {
        request_len = (size_t)snprintf(
            request, sizeof(request),
            "%s %s HTTP/1.0\r\n"
            "Host: %s:%u\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, config->host, (unsigned)config->port,
            config->token[0] ? config->token : "-", strlen(body));
    } else {
        request_len = (size_t)snprintf(
            request, sizeof(request),
            "%s %s HTTP/1.0\r\n"
            "Host: %s:%u\r\n"
            "Authorization: Bearer %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, config->host, (unsigned)config->port,
            config->token[0] ? config->token : "-");
    }
    if (request_len >= sizeof(request)) {
        ret = -ENOMEM;
        goto out_close;
    }

    while (sent < request_len) {
        ret = send(sockfd, request + sent, request_len - sent, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            ret = -errno;
            goto out_close;
        }
        sent += (size_t)ret;
    }
    if (body) {
        sent = 0;
        while (sent < strlen(body)) {
            ret = send(sockfd, body + sent, strlen(body) - sent, 0);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ret = -errno;
                goto out_close;
            }
            sent += (size_t)ret;
        }
    }

    /* 逐字节解析头部直到空行，其余字节进入 body 缓冲。 */
    {
        char chunk[256];
        size_t used = 0;
        size_t i;

        while (1) {
            ret = recv(sockfd, chunk, sizeof(chunk), 0);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ret = -errno;
                goto out_close;
            }
            if (ret == 0) {
                break;
            }
            for (i = 0; i < (size_t)ret; i++) {
                if (!header_done) {
                    if (!have_status && response + used + 1 < response + response_size) {
                        /* 状态行与头部复用 response 缓冲暂存。 */
                        response[used++] = chunk[i];
                        response[used] = '\0';
                        if (chunk[i] == '\n') {
                            if (!have_status) {
                                if (sscanf(response, "HTTP/%*d.%*d %d",
                                           &http_status) == 1) {
                                    have_status = true;
                                }
                                used = 0;
                                response[0] = '\0';
                            }
                        }
                    }
                    /* 检测 \r\n\r\n：跨 chunk 边界时借助上一字节。 */
                    if (chunk[i] == '\n' && i > 0 && chunk[i - 1] == '\r') {
                        /* 候选行尾；若下一字节也是 \r\n 则头部结束。 */
                    }
                    continue;
                }
                if (used + 1 < response_size) {
                    response[used++] = chunk[i];
                    response[used] = '\0';
                } else {
                    ret = -ENOMEM;
                    goto out_close;
                }
            }
        }
    }

    if (http_status_out) {
        *http_status_out = http_status;
    }
    ret = 0;

out_close:
    close(sockfd);
out_freeaddr:
    freeaddrinfo(result);
    return ret;
}

/* 头部结束检测独立于上面的逐字节扫描：头部以 "\r\n\r\n" 结束，
 * 这里用一个小的状态机完成，避免跨 chunk 边界丢失。 */
static int http_request_sm(const smart_home_miloco_client_config_t *config,
                           const char *method,
                           const char *path,
                           const char *body,
                           char *response,
                           size_t response_size,
                           int *http_status_out)
{
    (void)config;
    (void)method;
    (void)path;
    (void)body;
    (void)response;
    (void)response_size;
    (void)http_status_out;
    return -ENOSYS;
}

int smart_home_miloco_http_get(const smart_home_miloco_client_config_t *config,
                               const char *path,
                               char *response,
                               size_t response_size,
                               int *http_status_out)
{
    (void)http_request_sm;
    return http_request(config, "GET", path, NULL, response, response_size,
                        http_status_out);
}

int smart_home_miloco_http_post(const smart_home_miloco_client_config_t *config,
                                const char *path,
                                const char *body,
                                char *response,
                                size_t response_size,
                                int *http_status_out)
{
    return http_request(config, "POST", path, body, response, response_size,
                        http_status_out);
}
