/** SmartHome five-item product navigation. */

#include "smart_home_lvgl_internal.h"
#include "images/smart_home_icons.h"

#include <stdint.h>

static const char *const g_nav_titles[SMART_HOME_TAB_COUNT] = {
    "首页", "设备", "场景", "安防", "更多"
};

static const char *const g_nav_icons[SMART_HOME_TAB_COUNT] = {
    ICON_NAV_HOME, ICON_NAV_DEVICES, ICON_NAV_SCENES, ICON_NAV_SECURITY,
    ICON_NAV_MORE
};

static int tab_for_screen(const smart_home_lvgl_t *ui, const lv_obj_t *screen)
{
    if (!ui || !screen) return SMART_HOME_TAB_HOME;
    if (screen == ui->screen_panel) return SMART_HOME_TAB_DEVICES;
    if (screen == ui->screen_scenes) return SMART_HOME_TAB_SCENES;
    if (screen == ui->screen_security) return SMART_HOME_TAB_SECURITY;
    if (screen == ui->screen_more || screen == ui->screen_settings ||
        screen == ui->screen_network) {
        return SMART_HOME_TAB_MORE;
    }
    return SMART_HOME_TAB_HOME;
}

void smart_home_lvgl_load_tab(smart_home_lvgl_t *ui, int tab)
{
    lv_obj_t *screen = NULL;

    if (!ui || tab < 0 || tab >= SMART_HOME_TAB_COUNT) {
        return;
    }
    switch (tab) {
    case SMART_HOME_TAB_HOME:     screen = ui->screen_home; break;
    case SMART_HOME_TAB_DEVICES:  screen = ui->screen_panel; break;
    case SMART_HOME_TAB_SCENES:   screen = ui->screen_scenes; break;
    case SMART_HOME_TAB_SECURITY: screen = ui->screen_security; break;
    case SMART_HOME_TAB_MORE:     screen = ui->screen_more; break;
    default: break;
    }
    if (!screen) return;
    ui->active_tab = tab;
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 180, 0, false);
}

static void nav_btn_click(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    int tab = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(event));
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        if (ui && tab == SMART_HOME_TAB_DEVICES && ui->panel_title) {
            lv_label_set_text(ui->panel_title, "我的设备");
        }
        smart_home_lvgl_load_tab(ui, tab);
    }
}

lv_obj_t *smart_home_lvgl_build_nav_bar(lv_obj_t *screen,
                                        smart_home_lvgl_t *ui)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_t *content;
    int gap = smart_home_lvgl_compact() ? 4 : 12;
    int button_w = smart_home_lvgl_compact() ? 70 : 96;
    int content_w = button_w * SMART_HOME_TAB_COUNT +
                    gap * (SMART_HOME_TAB_COUNT - 1);
    int active = tab_for_screen(ui, screen);

    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, smart_home_lvgl_disp_w(), SMART_HOME_NAV_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    smart_home_lvgl_set_bg(bar, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, SMART_HOME_UI_COLOR_BORDER, 0);

    content = lv_obj_create(bar);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, content_w, SMART_HOME_NAV_H - 4);
    lv_obj_center(content);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_column(content, gap, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* The bar owns the full footer; the compact inner row keeps the five
     * touch targets visually calm instead of stretching each one edge-to-edge. */
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    /* Buttons are attached to the centred content row below. */
    bar = content;

    for (int i = 0; i < SMART_HOME_TAB_COUNT; i++) {
        lv_obj_t *button = lv_btn_create(bar);
        lv_obj_t *content;
        lv_color_t color = i == active ? SMART_HOME_UI_COLOR_PRIMARY_DARK :
                                         SMART_HOME_UI_COLOR_TEXT_MUTED;
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, button_w, SMART_HOME_NAV_H - 8);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_user_data(button, (void *)(intptr_t)i);
        lv_obj_add_event_cb(button, nav_btn_click, LV_EVENT_CLICKED, ui);
        if (i == active) smart_home_lvgl_set_bg(button, SMART_HOME_UI_COLOR_SURFACE_ON);
        else lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        content = smart_home_lvgl_icon_with_text(button, g_nav_icons[i],
                                                  20, 20, g_nav_titles[i],
                                                  color, 11);
        lv_obj_center(content);
    }
    return bar;
}
