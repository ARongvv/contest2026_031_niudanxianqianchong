/**
 * SmartHome 1024x600 product home screen.
 *
 * The home page intentionally contains only summaries and navigation.  Device
 * state remains owned by the native smart_home service; the labels here are
 * refreshed on the LVGL owner thread together with the device cards.
 */

#include "smart_home_lvgl_internal.h"
#include "images/smart_home_icons.h"

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
    static const char *const status_icons[] = {
        ICON_STATUS_MICROPHONE,
        ICON_STATUS_CAMERA,
        ICON_STATUS_DND,
        ICON_STATUS_WIFI,
    };
    lv_obj_t *bar;
    lv_obj_t *brand;
    lv_obj_t *brand_mark;
    lv_obj_t *time;
    lv_obj_t *icon;
    int width = smart_home_lvgl_disp_w();
    int time_width;
    int status_x;
    int i;

    if (!screen) {
        return;
    }
    /* Product pages keep one stable brand bar; page identity belongs in the
     * content heading so the shell never jumps while navigating. */
    (void)title;

    bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, width, SMART_HOME_TOPBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    smart_home_lvgl_set_bg(bar, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, SMART_HOME_UI_COLOR_BORDER, 0);

    brand_mark = lv_obj_create(bar);
    lv_obj_remove_style_all(brand_mark);
    lv_obj_set_size(brand_mark, 36, 36);
    lv_obj_align(brand_mark, LV_ALIGN_LEFT_MID, smart_home_lvgl_pad_x(), 0);
    lv_obj_set_style_radius(brand_mark, 11, 0);
    smart_home_lvgl_set_bg(brand_mark, lv_color_hex(0x79AFBA));
    icon = smart_home_lvgl_icon_create(brand_mark, ICON_NAV_HOME, 21, 21);
    if (icon) {
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_obj_set_style_image_recolor(icon, lv_color_white(), 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
        lv_obj_center(icon);
    }

    brand = smart_home_lvgl_label_create(bar,
                                         "OpenVela HOME",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         20);
    lv_obj_align_to(brand, brand_mark, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    time = smart_home_lvgl_label_create(bar, "11:37",
                                        SMART_HOME_UI_COLOR_TEXT_PRIMARY, 20);
    lv_obj_align(time, LV_ALIGN_RIGHT_MID, -smart_home_lvgl_pad_x(), 0);
    time_width = lv_obj_get_width(time);
    status_x = width - smart_home_lvgl_pad_x() - time_width - 14 -
               (int)(sizeof(status_icons) / sizeof(status_icons[0])) * 26;

    for (i = 0; i < (int)(sizeof(status_icons) / sizeof(status_icons[0])); i++) {
        icon = smart_home_lvgl_icon_create(bar, status_icons[i], 20, 20);
        if (!icon) {
            continue;
        }

        lv_obj_set_style_text_color(icon, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor(icon, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
        lv_obj_set_pos(icon, status_x + i * 26,
                       (SMART_HOME_TOPBAR_H - 20) / 2);
    }
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

static void home_feature_text(lv_obj_t *card, const char *title,
                              const char *body, lv_color_t title_color)
{
    lv_obj_t *label = smart_home_lvgl_label_create(card, title, title_color, 16);

    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, -24);
    label = smart_home_lvgl_label_create(card, body,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY, 13);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static lv_obj_t *home_icon_badge(lv_obj_t *card, const char *icon,
                                 lv_color_t color, int size)
{
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_t *glyph;

    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, size, size);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(badge, size / 3, 0);
    smart_home_lvgl_set_bg(badge, color);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    glyph = smart_home_lvgl_icon_create(badge, icon, size - 16, size - 16);
    if (glyph) {
        lv_obj_set_style_text_color(glyph, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor(glyph, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
        lv_obj_center(glyph);
    }
    return badge;
}

static void home_round_action(lv_obj_t *card, const char *text,
                              lv_align_t align, int x_ofs)
{
    lv_obj_t *button = lv_obj_create(card);
    lv_obj_t *label;

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 48, 48);
    lv_obj_align(button, align, x_ofs, -2);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    smart_home_lvgl_set_bg(button, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
    label = smart_home_lvgl_label_create(button, text,
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY, 24);
    lv_obj_center(label);
}

static void home_toggle(lv_obj_t *card, int on)
{
    lv_obj_t *toggle = lv_obj_create(card);
    lv_obj_t *dot = lv_obj_create(toggle);

    lv_obj_remove_style_all(toggle);
    lv_obj_set_size(toggle, 48, 30);
    lv_obj_align(toggle, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, 0);
    smart_home_lvgl_set_bg(toggle, on ? SMART_HOME_UI_COLOR_PRIMARY :
                            SMART_HOME_UI_COLOR_TEXT_MUTED);
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 20, 20);
    lv_obj_align(dot, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 on ? -5 : 5, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    smart_home_lvgl_set_bg(dot, lv_color_white());
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
    lv_obj_t *badge;
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
    home_card_text(card, "9月18日 星期五", "深圳市南山区", SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    label = smart_home_lvgl_label_create(card, "11:37", SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         32);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 28);
    badge = home_icon_badge(card, ICON_HUMIDITY, lv_color_hex(0xEAF4F5), 76);
    /* Make the weather pictogram the visual anchor of the tall card. */
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -10);
    label = smart_home_lvgl_label_create(card, "32°", SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         32);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 42);
    ui->home_env_label = smart_home_lvgl_label_create(card, "",
                                                      SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                      14);
    lv_obj_align(ui->home_env_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    card = home_card(screen, x + weather_w + gap, y, scene_w, top_h,
                     SMART_HOME_UI_COLOR_SURFACE_ON);
    home_icon_badge(card, ICON_NAV_HOME, lv_color_hex(0xFFF8F1), 48);
    home_feature_text(card, "回家模式", "温暖灯光 · 新风开启",
                      SMART_HOME_UI_COLOR_PRIMARY_DARK);
    home_make_clickable(card, ui, HOME_ACTION_SCENES);

    card = home_card(screen, x + weather_w + gap + scene_w + gap, y,
                     primary_w, top_h, SMART_HOME_UI_COLOR_SURFACE);
    home_icon_badge(card, ICON_DEVICE_AC, lv_color_hex(0xFFF8F4), 48);
    home_toggle(card, 1);
    label = smart_home_lvgl_label_create(card, "空调伴侣",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY, 18);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 58, 0);
    label = smart_home_lvgl_label_create(card, "客厅",
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY, 13);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 58, 25);
    home_round_action(card, "−", LV_ALIGN_LEFT_MID, 0);
    home_round_action(card, "+", LV_ALIGN_RIGHT_MID, 0);
    ui->home_ac_label = smart_home_lvgl_label_create(card, "",
                                                     SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                     16);
    lv_obj_align(ui->home_ac_label, LV_ALIGN_CENTER, 0, 16);
    home_make_clickable(card, ui, HOME_ACTION_DEVICES);

    card = home_card(screen, x + weather_w + gap + scene_w + gap + primary_w + gap,
                     y, monitor_w, top_h, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    badge = home_icon_badge(card, ICON_STATUS_CAMERA, lv_color_hex(0xE9F4F1), 64);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -6);
    home_feature_text(card, "摄像头 G3", "客厅 · 实时预览与安防 ›",
                      SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    home_make_clickable(card, ui, HOME_ACTION_SECURITY);

    card = home_card(screen, x + weather_w + gap, y + top_h + gap, scene_w,
                     bottom_h, SMART_HOME_UI_COLOR_SURFACE);
    home_icon_badge(card, ICON_NAV_CHAT, lv_color_hex(0xF2F0FF), 54);
    home_feature_text(card, "Hi，小乔", "问问家庭状态",
                      SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    home_make_clickable(card, ui, HOME_ACTION_AGENT);

    card = home_card(screen, x + weather_w + gap + scene_w + gap,
                     y + top_h + gap, primary_w, bottom_h,
                     SMART_HOME_UI_COLOR_SURFACE_ON);
    home_icon_badge(card, ICON_MEDIA_AUDIO, lv_color_hex(0xFFF8F2), 48);
    home_feature_text(card, "Last Dance", "卧室 · 家庭音响",
                      SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    label = smart_home_lvgl_label_create(card, "◁◁    ▷    ▷▷",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY, 18);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    home_make_clickable(card, ui, HOME_ACTION_MORE);

    card = home_card(screen, x + weather_w + gap + scene_w + gap + primary_w + gap,
                     y + top_h + gap, monitor_w, bottom_h,
                     SMART_HOME_UI_COLOR_SURFACE);
    home_icon_badge(card, ICON_NAV_SECURITY, lv_color_hex(0xEDF8F3), 54);
    home_feature_text(card, "家庭状态", "门窗全部关闭",
                      SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    ui->home_status_label = smart_home_lvgl_label_create(card, "",
                                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                         14);
    lv_obj_align(ui->home_status_label, LV_ALIGN_CENTER, 0, 10);
    home_make_clickable(card, ui, HOME_ACTION_DEVICES);

    smart_home_lvgl_build_nav_bar(screen, ui);
    smart_home_lvgl_refresh_home(ui);
}
