/* SPDX-License-Identifier: Apache-2.0 */
/**
 * 只读 secrets.json 加载器。
 *
 * 本模块只读取模型凭据，绝不创建、恢复或写回 secrets 文件。调用者按
 * backend_id 请求 API key，避免切换模型时复用其他后端的凭据。
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_SMART_HOME_MODEL_SECRETS_PATH
#define CONFIG_SMART_HOME_MODEL_SECRETS_PATH "/data/smart_home/secrets.json"
#endif

#ifndef CONFIG_SMART_HOME_MODEL_SECRETS_MAX_FILE_SIZE
#define CONFIG_SMART_HOME_MODEL_SECRETS_MAX_FILE_SIZE 4096u
#endif

/**
 * 从 secrets.json 的 model_api_keys 对象中读取 backend_id 对应的密钥。
 *
 * 成功返回 AGENT_OK。文件/条目不存在或值为空返回 AGENT_ERROR_NOTFOUND；
 * 非法 JSON、版本或值返回 AGENT_ERROR_PARSE；过大输入/输出缓冲不足返回
 * AGENT_ERROR_LIMIT。任何失败都会清空 out，且不会回退到源码默认值。
 */
int smart_home_secrets_get_model_api_key(const char *backend_id,
                                         char *out,
                                         size_t out_size);

/**
 * 检查一个后端是否拥有可用密钥，不向调用者暴露密钥内容。
 * 返回值与 smart_home_secrets_get_model_api_key 一致。
 */
int smart_home_secrets_model_api_key_status(const char *backend_id);

#ifdef __cplusplus
}
#endif
