/** Product shells for Scenes, Security and More. */

#include "smart_home_lvgl_internal.h"
#include "images/smart_home_icons.h"

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
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 54);
    label = smart_home_lvgl_label_create(card, body,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY, 14);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void page_heading(lv_obj_t *screen, const char *text)
{
    lv_obj_t *label = smart_home_lvgl_label_create(
        screen, text, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 28);

    lv_obj_align(label, LV_ALIGN_TOP_LEFT, smart_home_lvgl_pad_x(),
                 SMART_HOME_TOPBAR_H + 26);
}

static void page_icon_badge(lv_obj_t *card, const char *icon,
                            lv_color_t background)
{
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_t *glyph;

    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 42, 42);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(badge, 12, 0);
    smart_home_lvgl_set_bg(badge, background);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    glyph = smart_home_lvgl_icon_create(badge, icon, 25, 25);
    if (glyph) {
        lv_obj_set_style_text_color(glyph, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor(glyph, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
        lv_obj_center(glyph);
    }
}

static lv_obj_t *page_outline_button(lv_obj_t *parent, const char *text,
                                     int width)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, 38);
    lv_obj_set_style_radius(button, 12, 0);
    smart_home_lvgl_set_bg(button, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, SMART_HOME_UI_COLOR_BORDER, 0);
    label = smart_home_lvgl_label_create(button, text,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         13);
    lv_obj_center(label);
    return button;
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

static lv_obj_t *page_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_remove_style_all(screen);
    smart_home_lvgl_set_bg(screen, SMART_HOME_UI_COLOR_BG);
    (void)ui;
    smart_home_lvgl_build_top_bar(screen, "OpenVela HOME");
    return screen;
}

void smart_home_lvgl_build_scene_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 78;
    int gap = 14;
    int w = (smart_home_lvgl_content_w() - 2 * gap) / 3;

    if (!ui) return;
    screen = page_screen(ui);
    ui->screen_scenes = screen;
    page_heading(screen, "我的场景");
    card = page_card(screen, x, y, w, 148);
    page_icon_badge(card, ICON_NAV_HOME, lv_color_hex(0xFFF5EC));
    page_title(card, "回家模式", "温暖灯光 · 新风开启");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x + w + gap, y, w, 148);
    page_icon_badge(card, ICON_MEDIA_VIDEO, lv_color_hex(0xF1F4FF));
    page_title(card, "观影模式", "调暗灯光 · 合上窗帘");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x + (w + gap) * 2, y, w, 148);
    page_icon_badge(card, ICON_STATUS_DND, lv_color_hex(0xF4F2FF));
    page_title(card, "睡眠模式", "关闭照明 · 安静守护");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x, y + 162, w, 148);
    page_icon_badge(card, ICON_NAV_SECURITY, lv_color_hex(0xEDF8F3));
    page_title(card, "离家模式", "关闭设备 · 安防布防");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_outline_button(screen, "+ 新建场景", 112);
    lv_obj_align(card, LV_ALIGN_TOP_RIGHT, -x,
                 SMART_HOME_TOPBAR_H + 26);
    smart_home_lvgl_build_nav_bar(screen, ui);
}

void smart_home_lvgl_build_security_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 78;
    int content_w = smart_home_lvgl_content_w();
    int preview_w = content_w * 2 / 3 - 10;
    int alert_w = content_w - preview_w - 14;
    lv_obj_t *label;
    lv_obj_t *footer;
    lv_obj_t *button;

    if (!ui) return;
    screen = page_screen(ui);
    ui->screen_security = screen;
    page_heading(screen, "安防");
    label = smart_home_lvgl_label_create(screen, "●  已布防",
                                         SMART_HOME_UI_COLOR_PRIMARY,
                                         14);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -x, SMART_HOME_TOPBAR_H + 34);

    card = page_card(screen, x, y, preview_w, 286);
    smart_home_lvgl_set_bg(card, lv_color_hex(0x354846));
    lv_obj_set_style_border_color(card, lv_color_hex(0x415654), 0);
    label = smart_home_lvgl_label_create(card, "客厅 · 实时预览占位",
                                         lv_color_hex(0xD9E4E0), 13);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, -52);
    label = smart_home_lvgl_label_create(card, "原生 Monitor / CameraPreview",
                                         lv_color_hex(0xA7BFBA), 12);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, 0, -52);
    label = smart_home_lvgl_label_create(card, "·", lv_color_hex(0xF5D976), 32);
    lv_obj_center(label);

    footer = lv_obj_create(card);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, lv_pct(100), 48);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    smart_home_lvgl_set_bg(footer, SMART_HOME_UI_COLOR_SURFACE);
    label = smart_home_lvgl_label_create(footer, "● 摄像头在线",
                                         SMART_HOME_UI_COLOR_PRIMARY, 13);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    button = page_outline_button(footer, "进入监控", 92);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);

    card = page_card(screen, x + preview_w + 14, y, alert_w, 286);
    label = smart_home_lvgl_label_create(card, "最近动态",
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         14);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    label = smart_home_lvgl_label_create(card, "当前没有需要处理的提醒",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY, 18);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 44);
    label = smart_home_lvgl_label_create(card,
                                         "AI 事件、门窗异常和设备离线\n会在这里出现。",
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         14);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 88);
    label = smart_home_lvgl_label_create(card, "模拟一条 AI 提醒 ›",
                                         SMART_HOME_UI_COLOR_WARNING, 14);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    smart_home_lvgl_build_nav_bar(screen, ui);
}

void smart_home_lvgl_build_more_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 78;
    int gap = 14;
    int w = (smart_home_lvgl_content_w() - gap * 3) / 4;

    if (!ui) return;
    screen = page_screen(ui);
    ui->screen_more = screen;
    page_heading(screen, "更多");
    card = page_card(screen, x, y, w, 140);
    page_icon_badge(card, ICON_NAV_CHAT, lv_color_hex(0xF1F4FF));
    page_title(card, "智能管家", "家庭问答与受控执行");
    page_action(card, ui, PAGE_ACTION_AGENT);
    card = page_card(screen, x + w + gap, y, w, 140);
    page_icon_badge(card, ICON_SUN, lv_color_hex(0xFFF6EA));
    page_title(card, "能耗中心", "本周用电概览");
    card = page_card(screen, x + (w + gap) * 2, y, w, 140);
    page_icon_badge(card, ICON_ROOM_LIVING, lv_color_hex(0xF2F5FF));
    page_title(card, "家庭成员", "2 人在家");
    card = page_card(screen, x + (w + gap) * 3, y, w, 140);
    page_icon_badge(card, ICON_NAV_SETTINGS, lv_color_hex(0xEDF8F3));
    page_title(card, "系统设置", "网络、智能服务与系统状态");
    page_action(card, ui, PAGE_ACTION_SETTINGS);
    smart_home_lvgl_build_nav_bar(screen, ui);
}
