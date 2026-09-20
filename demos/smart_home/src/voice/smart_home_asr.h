/****************************************************************************
 * demos/smart_home/src/voice/smart_home_asr.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/* MiMo-V2.5-ASR 云端语音识别客户端。
 *
 * 接口：POST api.xiaomimimo.com/v1/chat/completions（OpenAI 兼容），
 * 音频以 WAV base64 内嵌 user content 的 input_audio，语言 zh/en/auto
 * 经顶层 asr_options 指定，转写文本在 choices[0].message.content。
 * 与 LLM/TTS 的 MiMo 预设同 host，API key 直接复用 secrets.json 的
 * model_api_keys.mimo。配置持久化在 /data/smart_home/asr.json。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMART_HOME_ASR_LANGUAGE_MAX 8

typedef struct
{
  int enabled;                              /* 语音输入开关（持久化） */
  char language[SMART_HOME_ASR_LANGUAGE_MAX]; /* zh / en / auto */
} smart_home_asr_config_t;

/* 载入 asr.json；缺失或损坏时恢复默认（enabled=0, language=zh）。 */
void smart_home_asr_config_load(smart_home_asr_config_t *config);

/* 保存 asr.json（原子写）。仅设置页调用。 */
int smart_home_asr_config_save(const smart_home_asr_config_t *config);

/* 识别一段 PCM16 单声道 16 kHz 录音。
 *
 * pcm/bytes: 录音数据（voice_capture_record 的产物）
 * out_text:  成功时写入 UTF-8 转写文本（截断到 out_size-1）
 *
 * 返回 0 成功；负值为 AGENT_ERROR_* / errno 风格错误码。
 */
int smart_home_asr_recognize(const uint8_t *pcm, size_t bytes,
                             char *out_text, size_t out_size);

#ifdef __cplusplus
}
#endif
