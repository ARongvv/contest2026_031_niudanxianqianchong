/**
 * smart_home embedded LVGL icon fonts.
 */

#pragma once

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ac_20;
extern const lv_font_t bed_20;
extern const lv_font_t chat_20;
extern const lv_font_t couch_20;
extern const lv_font_t device_20;
extern const lv_font_t droplet_20;
extern const lv_font_t fan_20;
extern const lv_font_t light_20;
extern const lv_font_t sun_20;
extern const lv_font_t temperature_20;
extern const lv_font_t tool_20;

#define SMART_HOME_ICON_AC          "\xEF\x95\xA6" /* U+F566 */
#define SMART_HOME_ICON_BED         "\xEF\x88\xB6" /* U+F236 */
#define SMART_HOME_ICON_CHAT        "\xEF\x81\xB5" /* U+F075 */
#define SMART_HOME_ICON_COUCH       "\xEF\x92\xB8" /* U+F4B8 */
#define SMART_HOME_ICON_DEVICE      "\xEF\x80\x9C" /* U+F01C */
#define SMART_HOME_ICON_DROPLET     "\xEF\x81\x83" /* U+F043 */
#define SMART_HOME_ICON_FAN         "\xEF\xA1\xA3" /* U+F863 */
#define SMART_HOME_ICON_LIGHT       "\xEF\x83\xAB" /* U+F0EB */
#define SMART_HOME_ICON_SUN         "\xEF\x86\x85" /* U+F185 */
#define SMART_HOME_ICON_TEMPERATURE "\xEF\x8B\x8A" /* U+F2CA */
#define SMART_HOME_ICON_TOOL        "\xEF\x82\xAD" /* U+F0AD */

const lv_font_t *smart_home_lvgl_embedded_icon_font(const char *icon);

#ifdef __cplusplus
}
#endif
