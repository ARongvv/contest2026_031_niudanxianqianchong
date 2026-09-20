/****************************************************************************
 * demos/smart_home/src/voice/smart_home_kws_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/* KWS 常驻唤醒服务。
 *
 * voice_capture 的消费者：采集 worker 只投递帧拷贝，滑窗、特征前端
 * 与 TFLite 推理在本服务自己的 worker 线程执行（低优先级，不打扰
 * 录音分发）。唤醒命中经回调上报（运行于本服务线程，调用方必须
 * 快速返回或转投自己的线程，严禁直接调用 LVGL）。
 *
 * ASR 录音期间由 voice_capture 的消费者让位机制自动静默；
 * TTS 播报期间调用方应 kws_service_set_paused(1) 防止自唤醒。
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kws_wake_cb_t)(float wake_score, void *user_data);

/* 唤醒开关配置（kws.json）。boot 时 enabled=0 则服务不启动；运行期
 * 开关仅做暂停/恢复（避免反复重建模型与 arena）。 */
typedef struct
{
  int enabled;
} smart_home_kws_config_t;

void smart_home_kws_config_load(smart_home_kws_config_t *config);
int smart_home_kws_config_save(const smart_home_kws_config_t *config);

/* 启动常驻监听（初始化模型与前端、注册采集消费者）。幂等。
 * 失败返回负值（模型/内存/采集服务不可用）。 */
int kws_service_start(kws_wake_cb_t cb, void *user_data);

/* 停止监听并释放线程与模型资源（保留采集服务本身）。 */
void kws_service_stop(void);

/* 暂停/恢复推理（1=暂停：采集继续但不推理不触发）。 */
void kws_service_set_paused(int paused);

int kws_service_running(void);

#ifdef __cplusplus
}
#endif
