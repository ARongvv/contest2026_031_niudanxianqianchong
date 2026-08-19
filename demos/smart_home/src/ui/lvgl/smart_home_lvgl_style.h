/**
 * smart_home LVGL UI style resources.
 */

#pragma once

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Light Premium Color Tokens ──────────────────────────── */

/* Primary / Brand */
#define SMART_HOME_UI_COLOR_PRIMARY        lv_color_hex(0x4A90D9)
#define SMART_HOME_UI_COLOR_PRIMARY_DARK   lv_color_hex(0x3A7BD5)

/* Semantic */
#define SMART_HOME_UI_COLOR_SUCCESS        lv_color_hex(0x4CD964)
#define SMART_HOME_UI_COLOR_WARNING        lv_color_hex(0xD98A14)
#define SMART_HOME_UI_COLOR_DANGER         lv_color_hex(0xD64545)

/* Backgrounds */
#define SMART_HOME_UI_COLOR_BG             lv_color_hex(0xF6F8FB)
#define SMART_HOME_UI_COLOR_SURFACE        lv_color_hex(0xFFFFFF)
#define SMART_HOME_UI_COLOR_SURFACE_SOFT   lv_color_hex(0xEEF3F8)
#define SMART_HOME_UI_COLOR_SURFACE_ON     lv_color_hex(0xE7F4F1)

/* Text */
#define SMART_HOME_UI_COLOR_TEXT_PRIMARY   lv_color_hex(0x172033)
#define SMART_HOME_UI_COLOR_TEXT_SECONDARY lv_color_hex(0x526070)
#define SMART_HOME_UI_COLOR_TEXT_MUTED     lv_color_hex(0x8A96A8)

/* Border */
#define SMART_HOME_UI_COLOR_BORDER         lv_color_hex(0xDCE3EC)

/* ── Light Premium extended tokens ──────────────────────── */
#define SMART_HOME_UI_COLOR_DEVICE_ON_BORDER lv_color_hex(0xB8DED5)
#define SMART_HOME_UI_COLOR_USER_BUBBLE      lv_color_hex(0xE8F1FE)
#define SMART_HOME_UI_COLOR_TOOL_CARD_BG     lv_color_hex(0xFFF8EA)
#define SMART_HOME_UI_COLOR_ERROR_BUBBLE_BG  lv_color_hex(0xFFF6E5)

/* Nav active indicator (brand blue @ 20% opacity) */
#define SMART_HOME_UI_COLOR_NAV_ACTIVE_BG    lv_color_hex(0x4A90D9)

/* Cancel / secondary button */
#define SMART_HOME_UI_COLOR_BTN_SECONDARY    lv_color_hex(0x8A96A8)

typedef struct {
    lv_font_t *font_12;
    lv_font_t *font_14;
    lv_font_t *font_16;
    lv_font_t *font_20;
} smart_home_lvgl_style_t;

int smart_home_lvgl_style_init(void);
void smart_home_lvgl_style_deinit(void);

const lv_font_t *smart_home_lvgl_font(int size);

void smart_home_lvgl_set_bg(lv_obj_t *obj, lv_color_t color);
void smart_home_lvgl_card_style(lv_obj_t *obj);
void smart_home_lvgl_soft_card_style(lv_obj_t *obj);

lv_obj_t *smart_home_lvgl_label_create(lv_obj_t *parent,
                                       const char *text,
                                       lv_color_t color,
                                       int size);

/* ── Icon helpers ─────────────────────────────────────── */

/** Create an icon widget from an LVGL symbol or a PNG filename. */
lv_obj_t *smart_home_lvgl_icon_create(lv_obj_t *parent,
                                      const char *filename,
                                      int width,
                                      int height);

/** Create an icon + text label combo (icon above text, for nav bar / cards). */
lv_obj_t *smart_home_lvgl_icon_with_text(lv_obj_t *parent,
                                          const char *filename,
                                          int icon_w,
                                          int icon_h,
                                          const char *text,
                                          lv_color_t text_color,
                                          int text_size);

#ifdef __cplusplus
}
#endif
