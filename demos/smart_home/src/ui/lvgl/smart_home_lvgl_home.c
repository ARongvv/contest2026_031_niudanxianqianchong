/**
 * SmartHome 1024x600 product home screen.
 *
 * The home page intentionally contains only summaries and navigation.  Device
 * state remains owned by the native smart_home service; the labels here are
 * refreshed on the LVGL owner thread together with the device cards.
 */

#include "smart_home_lvgl_internal.h"

#include <stdio.h>
#include <string.h>

enum home_action_e {
    HOME_ACTION_DEVICES = 1,
    HOME_ACTION_SCENES,
    HOME_ACTION_SECURITY,
    HOME_ACTION_AGENT,
    HOME_ACTION_MORE,
};

static const char *home_room_name(const char *room)
{
    if (room && !strcmp(room, "living_room")) {
        return "客厅";
    }
    if (room && !strcmp(room, "bedroom")) {
        return "卧室";
    }
    return "家庭";
}

void smart_home_lvgl_build_top_bar(lv_obj_t *screen, const char *title)
{
    lv_obj_t *bar;
    lv_obj_t *brand;
    lv_obj_t *status;
    int width = smart_home_lvgl_disp_w();

    if (!screen) {
        return;
    }

    bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, width, SMART_HOME_TOPBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    smart_home_lvgl_set_bg(bar, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, SMART_HOME_UI_COLOR_BORDER, 0);

    brand = smart_home_lvgl_label_create(bar,
                                         title ? title : "OpenVela HOME",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         20);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, smart_home_lvgl_pad_x(), 0);

    status = smart_home_lvgl_label_create(bar,
                                          "麦克风  摄像头  勿扰  Wi-Fi  10:28",
                                          SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                          14);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -smart_home_lvgl_pad_x(), 0);
}

static lv_obj_t *home_card(lv_obj_t *screen, int x, int y, int w, int h,
                           lv_color_t color)
{
    lv_obj_t *card = lv_obj_create(screen);

    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    smart_home_lvgl_card_style(card);
    smart_home_lvgl_set_bg(card, color);
    return card;
}

static void home_card_text(lv_obj_t *card, const char *title, const char *body,
                           lv_color_t title_color)
{
    lv_obj_t *label;

    label = smart_home_lvgl_label_create(card, title, title_color, 16);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    label = smart_home_lvgl_label_create(card, body,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         14);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void home_action_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    int action = (int)(intptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !ui) {
        return;
    }

    switch (action) {
    case HOME_ACTION_DEVICES:
        smart_home_lvgl_load_tab(ui, SMART_HOME_TAB_DEVICES);
        break;
    case HOME_ACTION_SCENES:
        smart_home_lvgl_load_tab(ui, SMART_HOME_TAB_SCENES);
        break;
    case HOME_ACTION_SECURITY:
        smart_home_lvgl_load_tab(ui, SMART_HOME_TAB_SECURITY);
        break;
    case HOME_ACTION_MORE:
        smart_home_lvgl_load_tab(ui, SMART_HOME_TAB_MORE);
        break;
    case HOME_ACTION_AGENT:
        if (ui->screen_chat) {
            lv_scr_load_anim(ui->screen_chat, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                              180, 0, false);
        }
        break;
    default:
        break;
    }
}

static void home_make_clickable(lv_obj_t *card, smart_home_lvgl_t *ui,
                                enum home_action_e action)
{
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(card, (void *)(intptr_t)action);
    lv_obj_add_event_cb(card, home_action_cb, LV_EVENT_CLICKED, ui);
}

void smart_home_lvgl_refresh_home(smart_home_lvgl_t *ui)
{
    const smart_home_state_t *state;
    const smart_home_device_t *ac = NULL;
    char text[96];
    int on_count = 0;
    int i;

    if (!ui || !ui->device_state) {
        return;
    }

    state = ui->device_state;
    for (i = 0; i < SMART_HOME_MAX_DEVICES; i++) {
        const smart_home_device_t *device = &state->devices[i];

        if (!device->used) {
            continue;
        }
        if (device->on) {
            on_count++;
        }
        if (!ac && device->type == SMART_HOME_DEVICE_AC) {
            ac = device;
        }
    }

    if (ui->home_env_label) {
        snprintf(text, sizeof(text), "%d°C  ·  湿度 %d%%\n空气舒适",
                 state->env_temperature, state->env_humidity);
        lv_label_set_text(ui->home_env_label, text);
    }
    if (ui->home_ac_label) {
        if (ac) {
            snprintf(text, sizeof(text), "%s  ·  %s\n%d°C  %s",
                     home_room_name(ac->room), ac->on ? "运行中" : "已关闭",
                     ac->temperature, smart_home_ac_mode_name(ac->ac_mode));
        } else {
            snprintf(text, sizeof(text), "暂无空调设备\n前往设备页添加");
        }
        lv_label_set_text(ui->home_ac_label, text);
    }
    if (ui->home_status_label) {
        snprintf(text, sizeof(text), "%d 个设备正在运行\n家庭状态正常", on_count);
        lv_label_set_text(ui->home_status_label, text);
    }
}

void smart_home_lvgl_build_home_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    lv_obj_t *label;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 12;
    int gap = 12;
    int compact = smart_home_lvgl_compact();
    int weather_w = compact ? 154 : 210;
    int scene_w = compact ? 128 : 150;
    int primary_w = compact ? 220 : 360;
    int monitor_w = smart_home_lvgl_content_w() - weather_w - scene_w -
                    primary_w - gap * 3;
    int top_h = compact ? 126 : 190;
    int bottom_h = compact ? 112 : 178;

    if (!ui) {
        return;
    }
    if (monitor_w < (compact ? 130 : 180)) {
        monitor_w = compact ? 130 : 180;
    }

    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    smart_home_lvgl_set_bg(screen, SMART_HOME_UI_COLOR_BG);
    ui->screen_home = screen;
    smart_home_lvgl_build_top_bar(screen, "OpenVela HOME");

    card = home_card(screen, x, y, weather_w, top_h + bottom_h + gap,
                     SMART_HOME_UI_COLOR_SURFACE_SOFT);
    home_card_text(card, "今日天气", "深圳 · 南山区", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    label = smart_home_lvgl_label_create(card, "32°", SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         32);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -10);
    ui->home_env_label = smart_home_lvgl_label_create(card, "",
                                                      SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                      14);
    lv_obj_align(ui->home_env_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    card = home_card(screen, x + weather_w + gap, y, scene_w, top_h,
                     SMART_HOME_UI_COLOR_SURFACE_ON);
    home_card_text(card, "上班", "一键执行场景", SMART_HOME_UI_COLOR_PRIMARY_DARK);
    home_make_clickable(card, ui, HOME_ACTION_SCENES);

    card = home_card(screen, x + weather_w + gap + scene_w + gap, y,
                     primary_w, top_h, SMART_HOME_UI_COLOR_SURFACE);
    home_card_text(card, "空调伴侣", "客厅", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    ui->home_ac_label = smart_home_lvgl_label_create(card, "",
                                                     SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                     16);
    lv_obj_align(ui->home_ac_label, LV_ALIGN_CENTER, 0, 10);
    home_make_clickable(card, ui, HOME_ACTION_DEVICES);

    card = home_card(screen, x + weather_w + gap + scene_w + gap + primary_w + gap,
                     y, monitor_w, top_h, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    home_card_text(card, "摄像头 G3", "实时预览 · 原生显示", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    home_make_clickable(card, ui, HOME_ACTION_SECURITY);

    card = home_card(screen, x + weather_w + gap, y + top_h + gap, scene_w,
                     bottom_h, SMART_HOME_UI_COLOR_SURFACE);
    home_card_text(card, "小乔", "智能管家", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    home_make_clickable(card, ui, HOME_ACTION_AGENT);

    card = home_card(screen, x + weather_w + gap + scene_w + gap,
                     y + top_h + gap, primary_w, bottom_h,
                     SMART_HOME_UI_COLOR_SURFACE_ON);
    home_card_text(card, "正在播放", "Last Dance · 卧室", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    home_make_clickable(card, ui, HOME_ACTION_MORE);

    card = home_card(screen, x + weather_w + gap + scene_w + gap + primary_w + gap,
                     y + top_h + gap, monitor_w, bottom_h,
                     SMART_HOME_UI_COLOR_SURFACE);
    home_card_text(card, "家庭状态", "", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    ui->home_status_label = smart_home_lvgl_label_create(card, "",
                                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                         14);
    lv_obj_align(ui->home_status_label, LV_ALIGN_CENTER, 0, 10);
    home_make_clickable(card, ui, HOME_ACTION_DEVICES);

    smart_home_lvgl_build_nav_bar(screen, ui);
    smart_home_lvgl_refresh_home(ui);
}
