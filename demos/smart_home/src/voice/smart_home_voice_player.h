/****************************************************************************
 * demos/smart_home/src/voice/smart_home_voice_player.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/* ES8311 PCM16 单声道播放封装。
 *
 * 提供内存缓冲与原始 PCM 文件两种播放入口，供语音播报（TTS）服务与
 * tts_smoke 冒烟命令共用。播放调用是同步的：内部完成 /dev/audio/pcm0
 * 的 RESERVE/CONFIGURE/缓冲队列/AUDIOIOC_START 循环与清理，返回后设备
 * 已释放，可被下一次播放或 audio_smoke 重新占用。
 *
 * 采样率由调用方按音频来源指定（TTS WAV 头解析结果或命令行参数），
 * 不固定 16 kHz，播放器不持有全局采样率状态。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 播放中止请求。abort_flag 由其他线程置 1 请求停止；播放返回 -EINTR。 */
typedef struct
{
  volatile int abort_flag;
} smart_home_voice_abort_t;

/* 同步播放一段内存中的 PCM16 单声道数据。
 *
 * samples: PCM16 little-endian 字节缓冲（非 int16 指针，避免对齐假设）
 * bytes:   缓冲字节数
 * rate:    采样率（Hz），如 16000 / 24000
 * abort:   可为 NULL；abort_flag 置 1 时尽快停止并返回 -EINTR
 *
 * 返回 0 成功；负值为 errno 风格错误码。
 */
int smart_home_voice_player_play_mem(const uint8_t *samples, size_t bytes,
                                     uint32_t rate,
                                     smart_home_voice_abort_t *abort);

/* 同步播放一个原始 PCM16 单声道文件（如 audio_smoke record 的产物）。
 * rate 语义同上。返回 0 成功，负值为错误码。 */
int smart_home_voice_player_play_file(const char *path, uint32_t rate,
                                      smart_home_voice_abort_t *abort);

#ifdef __cplusplus
}
#endif
