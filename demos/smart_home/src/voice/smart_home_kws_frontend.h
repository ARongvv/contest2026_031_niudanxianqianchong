/****************************************************************************
 * demos/smart_home/src/voice/smart_home_kws_frontend.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/* wake_large 特征前端（临时实现，待与训练管线对拍）。
 *
 * 模型输入契约：int8[124 × 40 × 3]，NHWC（时间 × mel × 堆叠）。
 * 本前端按语音识别最常见约定实现：
 *   帧 30 ms（480 采样）Hann 窗 / 帧移 20 ms（320 采样）
 *   512 点 FFT → 功率谱 257 bin → 40 个三角 mel 滤波器（20–7990 Hz）
 *   ln(能量 + 1e-6)，相邻 3 帧堆叠进通道维
 *
 * ⚠️ 训练管线的 FFT 长度、mel 频带、对数底与堆叠顺序未随模型发布
 * （wake_large_deploy.tar.gz 仅含模型资产）。上述常数是行业标准默认，
 * 未经金标准 PCM 对拍验证前，真机唤醒率数据不可解释；
 * 常数全部集中在 kws_frontend.c 顶部宏，拿到训练前端源码后逐项对齐。
 *
 * 量化参数（scale/zero_point）从模型张量实时读取，非本文件职责。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 覆盖 124 个 3 帧堆叠所需的帧数（t 帧用到 t+2，t 最大 123）。 */
#define KWS_FRONTEND_FRAME_SAMPLES   480
#define KWS_FRONTEND_HOP_SAMPLES     320
#define KWS_FRONTEND_TIME_STEPS      124
#define KWS_FRONTEND_MEL_BINS        40
#define KWS_FRONTEND_STACK           3
#define KWS_FRONTEND_FRAMES          (KWS_FRONTEND_TIME_STEPS + \
                                      KWS_FRONTEND_STACK - 1)   /* 126 */
/* 喂入 compute 的 PCM 采样数：(126-1)*320 + 480 = 40480（约 2.53 s）。 */
#define KWS_FRONTEND_PCM_SAMPLES     ((KWS_FRONTEND_FRAMES - 1) * \
                                      KWS_FRONTEND_HOP_SAMPLES + \
                                      KWS_FRONTEND_FRAME_SAMPLES)

/* 构建 mel 滤波器组与 FFT 位反转表。返回 0 成功（幂等）。 */
int kws_frontend_init(void);

/* pcm 为 KWS_FRONTEND_PCM_SAMPLES 个 PCM16 单声道采样；
 * out 为 124*40*3 个 int8（已按传入的量化参数量化）。 */
int kws_frontend_compute(const int16_t *pcm, float quant_scale,
                         int quant_zero_point, int8_t *out);

#ifdef __cplusplus
}
#endif
