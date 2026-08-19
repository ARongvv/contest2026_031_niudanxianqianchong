/**
 * smart_home LVGL bottom navigation.
 */

#include "smart_home_lvgl_internal.h"
#include "images/smart_home_icons.h"

#include <stdint.h>
#include <stdio.h>

static const char *g_nav_titles[] = {
    "Panel",
    "Chat",
    "Settings",
};

static const char *g_nav_icons[] = {
    ICON_NAV_HOME,
    ICON_NAV_CHAT,
    ICON_NAV_SETTINGS,
};

static void nav_btn_click(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    int tab =
        (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(event));
    lv_obj_t *screens[3];
    int anim;

    if (!ui || tab == ui->active_tab) {
        return;
    }

    screens[SMART_HOME_TAB_PANEL] = ui->screen_panel;
    screens[SMART_HOME_TAB_CHAT] = ui->screen_chat;
    screens[SMART_HOME_TAB_SETTINGS] = ui->screen_settings;

    anim = (tab > ui->active_tab) ? LV_SCR_LOAD_ANIM_MOVE_LEFT :
                                    LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    if (tab == SMART_HOME_TAB_SETTINGS) {
        smart_home_lvgl_refresh_tool_directory(ui);
    }
    ui->active_tab = tab;
    lv_scr_load_anim(screens[tab], anim, 220, 0, false);
}

lv_obj_t *smart_home_lvgl_build_nav_bar(lv_obj_t *screen,
                                        smart_home_lvgl_t *ui)
{
    lv_obj_t *bar = lv_obj_create(screen);
    int bar_w = smart_home_lvgl_content_w();
    int gap = smart_home_lvgl_compact() ? 8 : 16;
    int btn_w = (bar_w - gap) / 3;
    int icon_size = smart_home_lvgl_compact() ? 18 : 24;
    int font_size = smart_home_lvgl_compact() ? 10 : 12;
    int screen_tab = SMART_HOME_TAB_PANEL;

    if (ui) {
        if (screen == ui->screen_chat) {
            screen_tab = SMART_HOME_TAB_CHAT;
        } else if (screen == ui->screen_settings) {
            screen_tab = SMART_HOME_TAB_SETTINGS;
        }
    }

    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, bar_w, SMART_HOME_NAV_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -SMART_HOME_NAV_BOTTOM_PAD);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar,
                          LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 3; i++) {
        lv_color_t text_color;
        lv_obj_t *btn;
        lv_obj_t *combo;

        btn = lv_btn_create(bar);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, btn_w, SMART_HOME_NAV_H - 8);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, nav_btn_click, LV_EVENT_CLICKED, ui);

        if (i == screen_tab) {
            smart_home_lvgl_set_bg(btn, SMART_HOME_UI_COLOR_NAV_ACTIVE_BG);
            lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
            text_color = SMART_HOME_UI_COLOR_PRIMARY;
        } else {
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            text_color = SMART_HOME_UI_COLOR_TEXT_MUTED;
        }

        combo = smart_home_lvgl_icon_with_text(btn,
                                                g_nav_icons[i],
                                                icon_size, icon_size,
                                                g_nav_titles[i],
                                                text_color,
                                                font_size);
        lv_obj_center(combo);
    }

    return bar;
}
