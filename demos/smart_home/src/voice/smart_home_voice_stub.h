#pragma once

/**
 * 语音模块桩 — 真实 ASR/TTS 接入时替换此文件。
 *
 * 当前在 QEMU 模拟器上运行，无音频硬件，所有接口返回桩值。
 * 真实硬件团队可按相同签名接入 Doubao / iFlytek / 本地 ASR + TTS。
 *
 * 线程安全：voice_capture_* 在独立线程中运行，voice_play_* 可在
 * 任何线程调用。LVGL 更新通过 lv_async_call 投递。
 *
 * 启用桩模式的语音：UI 中 PTT 按钮按下 1.5 秒后自动填入预置文本。
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SMART_HOME_VOICE_STUB 1

/** 开始录音。返回 0 成功，当前为桩实现 */
static inline int voice_capture_start(void) { return -1; }

/** 停止录音。填充 PCM 数据到 buf，返回实际字节数。当前为桩实现 */
static inline int voice_capture_stop(char *buf, size_t size)
{
    (void)buf;
    (void)size;
    return -1;
}

/** 取消录音（上滑手势触发），丢弃已录制数据 */
static inline void voice_capture_cancel(void) {}

/** TTS 将文本转为语音并开始播放。返回 0 成功，当前为桩实现 */
static inline int voice_play_text(const char *text)
{
    (void)text;
    return -1;
}

/** 停止当前 TTS 播放 */
static inline void voice_play_stop(void) {}

#ifdef __cplusplus
}
#endif
