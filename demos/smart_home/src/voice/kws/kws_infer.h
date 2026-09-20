/****************************************************************************
 * demos/smart_home/src/voice/kws/kws_infer.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/* wake_large int8 模型的 TFLite Micro 推理封装（C API）。
 *
 * 模型契约：输入 int8[124×40×3]，输出 int8 五分类
 * （wake / hard_neg / other_speech / background / silence）。
 * P4 无 ESP-NN 后端，固定 reference kernel + 固定 9 算子 resolver。
 *
 * 非线程安全：单例接口，调用方（kws_service / kws_smoke）自行串行化。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KWS_INFER_FEATURE_SIZE (124 * 40 * 3)
#define KWS_INFER_CLASS_COUNT  5
#define KWS_INFER_CLASS_WAKE   0

/* 初始化：加载模型、注册算子、AllocateTensors 并校验张量契约。
 * arena 由调用方分配（建议 PSRAM、16 字节对齐）；
 * 返回 0 成功，负值为 errno 风格错误码。 */
int kws_infer_init(void *arena, size_t arena_size);

/* AllocateTensors 实际使用的 arena 字节数（init 成功后有效）。 */
size_t kws_infer_arena_used(void);

/* 输入张量量化参数（供前端量化特征使用）。 */
void kws_infer_input_params(float *scale, int *zero_point);

/* 单次推理：features 为 KWS_INFER_FEATURE_SIZE 个 int8；
 * scores 输出 KWS_INFER_CLASS_COUNT 个去量化浮点分数。
 * 返回 0 成功。 */
int kws_infer_run(const int8_t *features, float *scores);

#ifdef __cplusplus
}
#endif
