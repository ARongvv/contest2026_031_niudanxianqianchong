/**
 * smart_home icon constants.
 *
 * Source:  packages/demos/smart_home/res/icons/ or embedded LVGL icon fonts
 * Deploy:  /data/res/icons/  (PNG icons only, via ROMFS or data partition)
 *
 * Icons can be embedded LVGL font glyphs, LVGL built-in symbols, or PNG
 * filenames loaded at runtime via lv_image_set_src() with file paths.
 *
 * HOW TO ADD A NEW PNG ICON:
 *   1. Place the .png in res/icons/
 *   2. Add an ICON_xxx define below matching the filename (without .png)
 *   3. Deploy the .png to /data/res/icons/ on the target
 *   4. Use: smart_home_lvgl_icon_create(parent, ICON_xxx, w, h)
 *
 * HOW TO ADD A NEW EMBEDDED FONT ICON:
 *   1. Place the generated font .c in src/ui/lvgl/icons/
 *   2. Declare it in icons/smart_home_lvgl_icons.h
 *   3. Add it to smart_home_lvgl_embedded_icon_font()
 *   4. Add the font .c to Makefile and CMakeLists.txt
 */

#pragma once

#include <lvgl/lvgl.h>
#include "../icons/smart_home_lvgl_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Navigation bar ────────────────────────────────────────── */
#define ICON_NAV_HOME        LV_SYMBOL_HOME
#define ICON_NAV_CHAT        SMART_HOME_ICON_CHAT
#define ICON_NAV_SETTINGS    LV_SYMBOL_SETTINGS

/* ── Device type indicators ────────────────────────────────── */
#define ICON_DEVICE_LIGHT    SMART_HOME_ICON_LIGHT
#define ICON_DEVICE_AC       SMART_HOME_ICON_AC
#define ICON_DEVICE_FAN      SMART_HOME_ICON_FAN
#define ICON_DEVICE_GENERIC  SMART_HOME_ICON_DEVICE

/* ── Room markers ──────────────────────────────────────────── */
#define ICON_ROOM_LIVING     SMART_HOME_ICON_COUCH
#define ICON_ROOM_BEDROOM    SMART_HOME_ICON_BED

/* ── Action buttons ────────────────────────────────────────── */
#define ICON_ADD             LV_SYMBOL_PLUS
#define ICON_DELETE          "icon_delete"
#define ICON_BACK            "icon_back"
#define ICON_CANCEL          "icon_cancel"
#define ICON_TOGGLE          "icon_toggle"

/* ── Status badges ─────────────────────────────────────────── */
#define ICON_STATUS_OK       LV_SYMBOL_OK
#define ICON_STATUS_FAIL     LV_SYMBOL_CLOSE
#define ICON_LOADING         LV_SYMBOL_REFRESH
#define ICON_TOOL            SMART_HOME_ICON_TOOL

/* ── Sensor strip ──────────────────────────────────────────── */
#define ICON_TEMP            SMART_HOME_ICON_TEMPERATURE
#define ICON_HUMIDITY        SMART_HOME_ICON_DROPLET
#define ICON_SUN             SMART_HOME_ICON_SUN
#define ICON_DROP            SMART_HOME_ICON_DROPLET

/* ── Settings groups ───────────────────────────────────────── */
#define ICON_SETTING_AI      LV_SYMBOL_SETTINGS
#define ICON_WIFI            "icon_wifi"
#define ICON_LANGUAGE        "icon_language"

#ifdef __cplusplus
}
#endif
