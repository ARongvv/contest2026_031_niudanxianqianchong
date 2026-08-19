/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 平台运行时抽象。
 *
 * 所有平台访问必须通过 agent_runtime_t 回调，核心代码不直接 include
 * openvela、ESP-IDF、STM32 HAL 或网络栈头文件。
 *
 * 生命周期：runtime 随 config 传入 agent_create()，核心拷贝后持有至 destroy。
 * 可选回调设为 NULL 时，fill_defaults 会补 POSIX fallback 或返回 NOTSUP。
 *
 * 同步原语使用场景：
 *   - mutex_*：多线程环境下保护 agent->busy
 *   - enter/exit_critical：ISR 中设置 cancel_requested（不可阻塞）
 *   - 单线程/bare-metal：两者均可为 NULL
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** HTTP 请求描述，用于 OpenAI-compatible adapter */
typedef struct {
    const char *method;            /* "POST" */
    const char *url;               /* 完整 URL */
    const char *host;              /* 主机名 */
    const char *path;              /* 路径 */
    const char *port;              /* 端口字符串，如 "443" */
    const char *headers;           /* 额外 HTTP 头，NULL = 无 */
    const void *body;              /* 请求体 */
    size_t body_size;              /* 请求体长度 */
    uint32_t timeout_ms;           /* 请求超时 */
} agent_http_request_t;

/** HTTP 响应描述，body 缓冲区由调用者分配 */
typedef struct {
    int status_code;               /* HTTP 状态码 */
    char *body;                    /* 响应体缓冲区（调用者分配） */
    size_t body_size;              /* 缓冲区大小 */
    size_t bytes_written;          /* 实际写入字节数 */
} agent_http_response_t;

/**
 * 平台回调集合。
 *
 * 必须实现：malloc_fn, free_fn, now_ms, log
 * 可选实现：其余字段，NULL = 功能不可用
 */
typedef struct {
    void *(*malloc_fn)(size_t size, void *user_data);
    void (*free_fn)(void *ptr, void *user_data);
    uint64_t (*now_ms)(void *user_data);    /* 单调时钟，毫秒 */
    void (*sleep_ms)(uint32_t ms, void *user_data);
    void (*log)(int level, const char *tag, const char *message, void *user_data);
    int (*http_post)(const agent_http_request_t *request,
                     agent_http_response_t *response,
                     void *user_data);
    void *(*mutex_create)(void *user_data);
    void (*mutex_destroy)(void *mutex, void *user_data);
    int (*mutex_lock)(void *mutex, uint32_t timeout_ms, void *user_data);
    void (*mutex_unlock)(void *mutex, void *user_data);
    uint32_t (*enter_critical)(void *user_data);   /* 进临界区，返回状态 key */
    void (*exit_critical)(uint32_t state, void *user_data); /* 恢复临界区 */
    void *user_data;               /* 所有回调共享的上下文 */
} agent_runtime_t;

#ifdef __cplusplus
}
#endif
