/* SPDX-License-Identifier: Apache-2.0 */
/**
 * openvela/NuttX runtime adapter 公共声明。
 *
 * 当构建启用 CAGENT_RUNTIME_OPENVELA 时，应用代码可调用
 * agent_runtime_openvela_fill() 获取预填充的 agent_runtime_t，
 * 所有 openvela 特定的 include 隔离在 runtime_openvela.c 中。
 *
 * 可移植核心在未编译此文件时仍可正常工作（使用 POSIX fallback）。
 */

#pragma once

#include <cagent/runtime.h>
#include <cagent/runtime_openvela.h>

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * openvela/NuttX mbedTLS TLS 连接上下文（不透明句柄）。
 *
 * 定义在 runtime_openvela.c。应用层通过 ov_tls_connect() 建立连接、
 * ov_tls_read()/ov_tls_write() 读写（内部处理 WANT_READ/WANT_WRITE）、
 * ov_tls_ctx_free() 释放。避免依赖 mbedtls 头，保持头文件可移植。
 */
typedef struct ov_tls_ctx ov_tls_ctx_t;

/** 创建 TLS 连接上下文（分配 + 初始化）。失败返回 NULL。 */
ov_tls_ctx_t *ov_tls_create(void);

/**
 * 建立 TLS 连接（非阻塞 connect + 超时 + mbedTLS 握手）。
 * 失败返回 AGENT_ERROR_*，成功返回 AGENT_OK（ctx 已就绪）。
 */
int ov_tls_connect(ov_tls_ctx_t *ctx,
                   const char *host,
                   const char *port,
                   uint32_t timeout_ms);

/** 释放 TLS 连接上下文。 */
void ov_tls_ctx_free(ov_tls_ctx_t *ctx);

/** 返回 TLS 连接底层 socket fd（供 poll/select 等待用），未连接返回 -1。 */
int ov_tls_get_fd(const ov_tls_ctx_t *ctx);

/**
 * TLS 读（内部处理 WANT_READ/WANT_WRITE）。
 * 返回读取字节数，失败返回 -1（errno 设置）。
 * @param ctx       连接上下文
 * @param buffer    输出缓冲
 * @param length    缓冲大小
 * @param timeout_ms 单次等待超时（0 = 阻塞）
 */
ssize_t ov_tls_read(ov_tls_ctx_t *ctx, void *buffer, size_t length,
                    uint32_t timeout_ms);

/**
 * TLS 写（内部处理 WANT_READ/WANT_WRITE）。
 * 返回写入字节数，失败返回 -1（errno 设置）。
 */
ssize_t ov_tls_write(ov_tls_ctx_t *ctx, const void *buffer, size_t length,
                     uint32_t timeout_ms);

/**
 * 用 openvela/NuttX 平台回调填充 agent_runtime_t。
 *
 * 已设置的回调不被覆盖（应用层可先设置部分回调再调用此函数）。
 * 未使用的字段保持 NULL（由 agent_runtime_fill_defaults 补 POSIX fallback）。
 *
 * 实现的回调：
 *   - malloc_fn / free_fn: 标准 C 库
 *   - now_ms: clock_gettime(CLOCK_MONOTONIC)
 *   - sleep_ms: usleep
 *   - log: syslog
 *   - http_post: mbedTLS HTTPS（需 CAGENT_RUNTIME_OPENVELA_TLS）
 *   - mutex_*: pthread_mutex
 *   - enter/exit_critical: pthread_mutex（非 ISR 安全，适用于单核场景）
 */
void agent_runtime_openvela_fill(agent_runtime_t *runtime);

#ifdef __cplusplus
}
#endif
