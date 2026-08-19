/**
 * smart_home LVGL UI internal declarations.
 */

#pragma once

#include "smart_home_lvgl.h"
#include "smart_home_lvgl_style.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMART_HOME_SCR_W 320
#define SMART_HOME_SCR_H 240
#define SMART_HOME_NAV_H 52
#define SMART_HOME_NAV_BOTTOM_PAD 4
#define SMART_HOME_PAD_X 8
#define SMART_HOME_PAD_Y 6

static inline int smart_home_lvgl_disp_w(void)
{
    lv_display_t *disp = lv_display_get_default();
    int width = disp ? (int)lv_display_get_horizontal_resolution(disp) :
                       SMART_HOME_SCR_W;

    return width > 0 ? width : SMART_HOME_SCR_W;
}

static inline int smart_home_lvgl_disp_h(void)
{
    lv_display_t *disp = lv_display_get_default();
    int height = disp ? (int)lv_display_get_vertical_resolution(disp) :
                        SMART_HOME_SCR_H;

    return height > 0 ? height : SMART_HOME_SCR_H;
}

static inline int smart_home_lvgl_square_size(void)
{
    int width = smart_home_lvgl_disp_w();
    int height = smart_home_lvgl_disp_h();

    return width < height ? width : height;
}

static inline int smart_home_lvgl_compact(void)
{
    return smart_home_lvgl_disp_w() <= 340 || smart_home_lvgl_disp_h() <= 260;
}

static inline int smart_home_lvgl_content_w(void)
{
    int width = smart_home_lvgl_disp_w();
    int pad = width >= 720 ? 28 : SMART_HOME_PAD_X;

    return width - 2 * pad;
}

static inline int smart_home_lvgl_content_h(void)
{
    int height = smart_home_lvgl_disp_h();
    int pad = height >= 720 ? 28 : SMART_HOME_PAD_Y;

    return height - 2 * pad;
}

static inline int smart_home_lvgl_keyboard_h(void)
{
    return smart_home_lvgl_compact() ? 88 : 150;
}

static inline int smart_home_lvgl_pad_x(void)
{
    return smart_home_lvgl_disp_w() >= 720 ? 28 : SMART_HOME_PAD_X;
}

enum {
    SMART_HOME_TAB_PANEL = 0,
    SMART_HOME_TAB_CHAT,
    SMART_HOME_TAB_SETTINGS,
};

lv_obj_t *smart_home_lvgl_build_nav_bar(lv_obj_t *screen,
                                        smart_home_lvgl_t *ui);

void smart_home_lvgl_build_panel_screen(smart_home_lvgl_t *ui);
void smart_home_lvgl_build_chat_screen(smart_home_lvgl_t *ui);
void smart_home_lvgl_build_settings_screen(smart_home_lvgl_t *ui);
void smart_home_lvgl_refresh_tool_directory(smart_home_lvgl_t *ui);
void smart_home_lvgl_settings_deinit(void);

void smart_home_lvgl_chat_send_text(smart_home_lvgl_t *ui, const char *text);
void smart_home_lvgl_append_msg_bubble(smart_home_lvgl_t *ui,
                                       const char *text,
                                       int is_user);
void smart_home_lvgl_append_tool_card(smart_home_lvgl_t *ui,
                                      const char *name,
                                      const char *call_id,
                                      int ok);
void smart_home_lvgl_append_error_bubble(smart_home_lvgl_t *ui,
                                         const char *msg);
void smart_home_lvgl_show_thinking(smart_home_lvgl_t *ui);
void smart_home_lvgl_finish_thinking(smart_home_lvgl_t *ui);
void smart_home_lvgl_trace_tool(smart_home_lvgl_t *ui,
                                const char *name,
                                const char *call_id,
                                int ok);

int smart_home_lvgl_submit_agent_job(smart_home_lvgl_t *ui, const char *text);

#ifdef __cplusplus
}
#endif
