/** Product shells for Scenes, Security and More. */

#include "smart_home_lvgl_internal.h"

#include <stdint.h>

enum page_action_e {
    PAGE_ACTION_AGENT = 1,
    PAGE_ACTION_SETTINGS,
};

static lv_obj_t *page_card(lv_obj_t *screen, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(screen);

    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    smart_home_lvgl_card_style(card);
    return card;
}

static void page_title(lv_obj_t *card, const char *title, const char *body)
{
    lv_obj_t *label = smart_home_lvgl_label_create(card, title,
                                                    SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                                    20);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    label = smart_home_lvgl_label_create(card, body,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY, 14);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void page_click_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    int action = (int)(intptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !ui) {
        return;
    }
    if (action == PAGE_ACTION_AGENT && ui->screen_chat) {
        lv_scr_load_anim(ui->screen_chat, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                         180, 0, false);
    } else if (action == PAGE_ACTION_SETTINGS && ui->screen_settings) {
        lv_scr_load_anim(ui->screen_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                         180, 0, false);
    }
}

static void page_action(lv_obj_t *card, smart_home_lvgl_t *ui,
                        enum page_action_e action)
{
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(card, (void *)(intptr_t)action);
    lv_obj_add_event_cb(card, page_click_cb, LV_EVENT_CLICKED, ui);
}

static lv_obj_t *page_screen(smart_home_lvgl_t *ui, const char *title)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_remove_style_all(screen);
    smart_home_lvgl_set_bg(screen, SMART_HOME_UI_COLOR_BG);
    smart_home_lvgl_build_top_bar(screen, title);
    return screen;
}

void smart_home_lvgl_build_scene_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 20;
    int gap = 14;
    int w = (smart_home_lvgl_content_w() - 2 * gap) / 3;

    if (!ui) return;
    screen = page_screen(ui, "场景");
    ui->screen_scenes = screen;
    card = page_card(screen, x, y, w, 170);
    page_title(card, "回家", "开灯、空调舒适模式");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x + w + gap, y, w, 170);
    page_title(card, "离家", "关闭非必要设备");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x + (w + gap) * 2, y, w, 170);
    page_title(card, "观影", "卧室氛围与勿扰");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x, y + 184, smart_home_lvgl_content_w(), 92);
    page_title(card, "场景执行", "涉及设备控制的场景由 Agent / Skill 审核后执行");
    smart_home_lvgl_build_nav_bar(screen, ui);
}

void smart_home_lvgl_build_security_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 20;
    int content_w = smart_home_lvgl_content_w();

    if (!ui) return;
    screen = page_screen(ui, "安防");
    ui->screen_security = screen;
    card = page_card(screen, x, y, content_w * 2 / 3 - 8, 260);
    page_title(card, "摄像头实时预览", "视频帧与 AI 检测叠加保持在原生显示链路");
    card = page_card(screen, x + content_w * 2 / 3 + 8, y,
                     content_w / 3 - 8, 124);
    page_title(card, "门窗", "状态正常");
    card = page_card(screen, x + content_w * 2 / 3 + 8, y + 136,
                     content_w / 3 - 8, 124);
    page_title(card, "家庭守护", "已启用");
    smart_home_lvgl_build_nav_bar(screen, ui);
}

void smart_home_lvgl_build_more_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 20;
    int gap = 14;
    int w = (smart_home_lvgl_content_w() - gap) / 2;

    if (!ui) return;
    screen = page_screen(ui, "更多");
    ui->screen_more = screen;
    card = page_card(screen, x, y, w, 138);
    page_title(card, "智能管家", "与小乔对话，查看工具执行过程");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x + w + gap, y, w, 138);
    page_title(card, "系统设置", "网络、模型、Skills 与系统健康");
    page_action(card, ui, PAGE_ACTION_SETTINGS);
    card = page_card(screen, x, y + 152, w, 110);
    page_title(card, "家庭成员", "成员与访问权限");
    card = page_card(screen, x + w + gap, y + 152, w, 110);
    page_title(card, "能耗中心", "设备使用情况");
    smart_home_lvgl_build_nav_bar(screen, ui);
}
