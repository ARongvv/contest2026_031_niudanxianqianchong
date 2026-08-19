/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 配置。
 *
 * 传入 agent_create() 后由核心拷贝，调用者可释放原始 config。
 * 运行时修改应使用 agent_set_limits()，不直接改 agent->config。
 */
#pragma once

#include <cagent/runtime.h>
#include <cagent/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** limits 默认值初始化器，与 CAGENT_DEFAULT_* 宏对齐 */
#define AGENT_LIMITS_DEFAULT                                           \
    {                                                                  \
        CAGENT_DEFAULT_MAX_STEPS, CAGENT_DEFAULT_TIMEOUT_MS,           \
        CAGENT_DEFAULT_MODEL_TIMEOUT_MS,                               \
        CAGENT_DEFAULT_TOOL_TIMEOUT_MS, 4u,                            \
        CAGENT_DEFAULT_MAX_OUTPUT_TOKENS                               \
    }

typedef struct {
    const char *name;              /* Agent 名称，用于日志标识 */
    const char *system_prompt;     /* 系统提示词，CRITICAL 级不可裁剪 */
    agent_limits_t limits;         /* 运行限制 */
    agent_runtime_t runtime;       /* 平台注入回调，NULL 字段由 fill_defaults 填充 */
    void *user_data;               /* 应用层用户数据，核心不使用 */
} agent_config_t;

/** 生成默认配置：标准 limits + POSIX runtime fallback */
agent_config_t agent_config_default(void);

/** 生成精简配置：小 MCU / bare-metal 适用，更小的 limits */
agent_config_t agent_config_tiny(void);

/**
 * 配置 key-value 回调。
 *
 * agent_config_load 逐行解析配置源，对每个有效 key=value 调用此回调。
 * 回调返回 0 继续解析，返回负值中止解析并传播错误码。
 */
typedef int (*agent_config_kv_fn)(const char *key,
                                  const char *value,
                                  void *user_data);

/**
 * 从 key-value 文本源加载配置。
 *
 * 解析规则：
 *   - 每行格式: key=value
 *   - 行首/行尾空白被 trim
 *   - 空行和 # 开头的行被忽略
 *   - 对每个有效 key=value 调用 callback
 *
 * 典型用法：
 *   agent_config_load("/data/cagent.conf", "CAGENT_", my_kv_handler, &my_cfg);
 *
 * 当 env_prefix 非 NULL 时，解析完文件后还会尝试读取环境变量：
 *   ${env_prefix}API_KEY, ${env_prefix}HOST, ${env_prefix}MODEL 等，
 *   环境变量名大写，key 中的小写转为大写，非字母数字字符转为下划线。
 *   环境变量值会覆盖文件中的同名 key。
 *
 * @param path       配置文件路径，NULL 则跳过文件读取
 * @param env_prefix 环境变量前缀，如 "CAGENT_"，NULL 则跳过环境变量
 * @param callback   key-value 回调，不可为 NULL
 * @param user_data  透传给 callback 的用户数据
 * @return AGENT_OK 或 agent_error_t
 */
int agent_config_load(const char *path,
                      const char *env_prefix,
                      agent_config_kv_fn callback,
                      void *user_data);

#ifdef __cplusplus
}
#endif
