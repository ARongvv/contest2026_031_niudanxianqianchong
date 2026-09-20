/**
 * smart_home LVGL voice session orchestration.
 *
 * KWS 唤醒 → 跳转聊天页并自动开始 PTT 录音 → ASR 文本自动发送 →
 * agent 回复 →（若 TTS 开启）自动播报。本文件只在
 * CONFIG_SMART_HOME_KWS 开启时编译；ASR/TTS 各自缺席时自动降级
 * （唤醒仅跳页 / 不播报）。
 */

#include "smart_home_lvgl_internal.h"
#include "images/smart_home_icons.h"

#include "../../smart_home_memory.h"
#include "../../voice/smart_home_kws_service.h"
#ifdef CONFIG_SMART_HOME_VOICE_TTS
#include "../../voice/smart_home_voice_play.h"
#endif
#ifdef CONFIG_SMART_HOME_VOICE_ASR
#include "../../voice/smart_home_asr.h"
#endif

#include <stdio.h>

/* TTS 播报期间暂停 KWS 推理：防止喇叭声进入麦克风造成自唤醒。 */
#ifdef CONFIG_SMART_HOME_VOICE_TTS
static void voice_session_poll(lv_timer_t *timer)
{
    smart_home_lvgl_t *ui = lv_timer_get_user_data(timer);

    if (!ui) {
        return;
    }

    kws_service_set_paused(voice_play_state() != VOICE_PLAY_IDLE);
}
#endif

/* KWS worker 线程回调：仅投递事件，不做任何 UI 操作。 */
static void voice_session_wake_cb(float wake_score, void *user_data)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)user_data;

    (void)wake_score;
    if (ui) {
        smart_home_lvgl_post_kws_wake(ui, wake_score);
    }
}

/* LVGL 线程：执行唤醒动作。 */
void smart_home_voice_session_on_wake(smart_home_lvgl_t *ui, float score)
{
    smart_home_kws_config_t config;

    (void)score;
    if (!ui) {
        return;
    }

#ifdef CONFIG_SMART_HOME_VOICE_ASR
    /* 已在语音会话中（录音进行或请求在途）：忽略重复唤醒。 */
    if (ui->asr_active || ui->request_inflight) {
        return;
    }
#endif

    smart_home_kws_config_load(&config);
    if (!config.enabled || !kws_service_running()) {
        return;
    }

    smart_home_lvgl_load_tab(ui, SMART_HOME_TAB_CHAT);
    smart_home_lvgl_append_msg_bubble(ui, "（已唤醒，请说话）", 0);

#ifdef CONFIG_SMART_HOME_VOICE_ASR
    smart_home_lvgl_chat_asr_begin(ui);
#else
    /* 无 ASR 时仅跳转聊天页等待文字输入。 */
#endif
}

void smart_home_lvgl_voice_session_start(smart_home_lvgl_t *ui)
{
    smart_home_kws_config_t config;

    if (!ui) {
        return;
    }

    smart_home_kws_config_load(&config);
    if (!config.enabled) {
        printf("[voice] kws disabled in kws.json; session idle\n");
        return;
    }

    if (kws_service_start(voice_session_wake_cb, ui) != 0) {
        fprintf(stderr, "[voice] kws service start failed\n");
        return;
    }

#ifdef CONFIG_SMART_HOME_VOICE_TTS
    lv_timer_create(voice_session_poll, 500, ui);
#endif
    printf("[voice] session ready (kws listening)\n");
}
