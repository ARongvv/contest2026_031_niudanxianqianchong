/**
 * smart_home LVGL panel screen.
 */

#include "smart_home_lvgl_internal.h"
#ifdef CONFIG_SMART_HOME_MILOCO_BRIDGE
#include "../../miloco/smart_home_miloco.h"
#endif
#include "images/smart_home_icons.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef CONFIG_SMART_HOME_NODE_GATEWAY
#include "../../addons/smart_home_node_gateway.h"
#include "cagent_addons/limits.h"
#endif

enum {
    ENV_TEMP = 0,
    ENV_HUM,
    ENV_LIGHT,
};

static void rebuild_device_cards(smart_home_lvgl_t *ui);
static void update_popup_value(smart_home_lvgl_t *ui);
static void open_device_editor(smart_home_lvgl_t *ui,
                               const smart_home_device_t *device);
static void device_switch_refresh_cb(void *data);

#ifdef CONFIG_SMART_HOME_NODE_GATEWAY
static void remote_node_timer_cb(lv_timer_t *timer);
#endif

static void layout_device_keyboard(smart_home_lvgl_t *ui, int visible)
{
    if (!ui || !ui->device_keyboard) {
        return;
    }

    lv_obj_set_size(ui->device_keyboard,
                    smart_home_lvgl_content_w(),
                    smart_home_lvgl_keyboard_h());
    lv_obj_align(ui->device_keyboard,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 -SMART_HOME_NAV_H - SMART_HOME_NAV_BOTTOM_PAD - 8);

    if (visible) {
        lv_obj_clear_flag(ui->device_keyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui->device_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void device_name_input_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (!ui) {
        return;
    }

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        if (ui->device_keyboard && target) {
            lv_keyboard_set_textarea(ui->device_keyboard, target);
        }
        layout_device_keyboard(ui, 1);
    } else if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY ||
               code == LV_EVENT_DEFOCUSED) {
        layout_device_keyboard(ui, 0);
    }
}

static void device_keyboard_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        layout_device_keyboard(ui, 0);
    }
}

static const char *device_icon(const smart_home_device_t *device)
{
    if (!device) {
        return ICON_DEVICE_GENERIC;
    }

    return device->type == SMART_HOME_DEVICE_AC ?
        ICON_DEVICE_AC : ICON_DEVICE_LIGHT;
}

static const char *device_room_name(const char *room)
{
    if (room && strcmp(room, "living_room") == 0) {
        return "客厅";
    }
    if (room && strcmp(room, "bedroom") == 0) {
        return "卧室";
    }
    if (room && strcmp(room, "kitchen") == 0) {
        return "厨房";
    }
    if (room && strcmp(room, "bathroom") == 0) {
        return "卫生间";
    }
    if (room && strcmp(room, "study") == 0) {
        return "书房";
    }
    if (room && strcmp(room, "balcony") == 0) {
        return "阳台";
    }
    return room && room[0] ? room : "家庭";
}

static void room_dropdown_options(const smart_home_state_t *state,
                                  char *buffer, size_t buffer_size,
                                  int include_all)
{
    size_t used = 0u;
    int i;

    if (!buffer || buffer_size == 0u) {
        return;
    }
    buffer[0] = '\0';
    if (include_all) {
        snprintf(buffer, buffer_size, "全部");
        used = strlen(buffer);
    }
    for (i = 0; state && i < smart_home_room_count(state); i++) {
        const char *room = smart_home_room_get(state, i);
        int written;

        written = snprintf(buffer + used, buffer_size - used,
                           "%s%s", used ? "\n" : "", device_room_name(room));
        if (written < 0 || (size_t)written >= buffer_size - used) {
            break;
        }
        used += (size_t)written;
    }
}

static const char *device_display_name(const smart_home_device_t *device)
{
    if (!device || !device->name[0]) {
        return "未命名设备";
    }
    /* Keep the seeded demo readable in the Chinese product shell while real
     * devices continue to use their service-provided names unchanged. */
    if (!strcmp(device->name, "Living Light")) {
        return "客厅主灯";
    }
    if (!strcmp(device->name, "Bedroom Light")) {
        return "床头灯";
    }
    if (!strcmp(device->name, "Bedroom AC")) {
        return "卧室空调";
    }
    return device->name;
}

#ifndef CONFIG_SMART_HOME_MILOCO_BRIDGE
static int device_matches_filter(smart_home_lvgl_t *ui,
                                 const smart_home_device_t *device)
{
    int filter;

    if (!ui || !ui->panel_room_dd || !device) {
        return 1;
    }

    filter = lv_dropdown_get_selected(ui->panel_room_dd);
    if (filter <= 0) {
        return 1;
    }
    {
        const char *room = smart_home_room_get(ui->device_state, filter - 1);

        return room && strcmp(device->room, room) == 0;
    }
}
#endif

static void format_device_card_text(const smart_home_device_t *device,
                                    char *buffer,
                                    size_t buffer_size)
{
    if (!device || !buffer || buffer_size == 0u) {
        return;
    }

    if (device->type == SMART_HOME_DEVICE_AC) {
        if (device->on) {
            snprintf(buffer,
                     buffer_size,
                     "%s · %d°C · %s",
                     device_room_name(device->room), device->temperature,
                     smart_home_ac_mode_name(device->ac_mode));
        } else {
            snprintf(buffer, buffer_size, "%s · 已关闭",
                     device_room_name(device->room));
        }
    } else if (device->on) {
        snprintf(buffer,
                 buffer_size,
                 "%s · 亮度 %d%%",
                 device_room_name(device->room),
                 device->brightness);
    } else {
        snprintf(buffer, buffer_size, "%s · 已关闭",
                 device_room_name(device->room));
    }
}

static int device_card_width(void)
{
    int columns = smart_home_lvgl_compact() ? 2 : 4;
    int gap = smart_home_lvgl_compact() ? 8 : 12;

    return (smart_home_lvgl_content_w() - gap * (columns - 1)) / columns;
}

static void device_card_switch_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    lv_obj_t *sw = lv_event_get_current_target(event);
    smart_home_device_t *device;
    int slot;
    int value;
    int mode;
    int fan_speed;
    int on;

    if (!ui || !sw) {
        return;
    }

    /* A switch is an immediate control, never a request to open the card's
     * fine-control sheet.  Stop the event before it can reach the card. */
    lv_event_stop_bubbling(event);
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || !ui->app ||
        !ui->device_state) {
        return;
    }

    slot = (int)(intptr_t)lv_obj_get_user_data(sw);
    device = smart_home_device_get_by_slot(ui->device_state, slot);
    if (!device) {
        return;
    }

    on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    value = device->type == SMART_HOME_DEVICE_AC ? device->temperature :
                                                   device->brightness;
    if (on && device->type != SMART_HOME_DEVICE_AC && value == 0) {
        /* The current state model stores an off light as brightness zero.
         * Give a fast re-enable a useful default rather than an invisible
         * "on at 0%" state. */
        value = 70;
    }
    mode = device->type == SMART_HOME_DEVICE_AC ? device->ac_mode : 0;
    fan_speed = device->type == SMART_HOME_DEVICE_AC ?
                device->ac_fan_speed : 3;
    if (smart_home_device_service_set_device_control(
            &ui->app->device_service, device->id, on, value, mode,
            fan_speed) != AGENT_OK) {
        if (device->on) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
        return;
    }

    /* Rebuilding the grid deletes this switch, therefore postpone it until
     * LVGL has finished dispatching the current value-change event. */
    lv_async_call(device_switch_refresh_cb, ui);
}

static void set_card_content(lv_obj_t *card,
                             const char *icon_name,
                             const char *title,
                             const char *detail,
                             int is_on,
                             smart_home_lvgl_t *ui,
                             int slot)
{
    lv_obj_t *badge;
    lv_obj_t *glyph;
    lv_obj_t *label;
    lv_obj_t *sw;

    lv_obj_clean(card);

    /* Horizontal layout: large icon badge on the left, name and status in
     * the middle, state switch on the right. */
    badge = lv_obj_create(card);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 48, 48);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(badge, 13, 0);
    smart_home_lvgl_set_bg(badge, is_on ? lv_color_hex(0xFFF8EF) :
                           SMART_HOME_UI_COLOR_SURFACE_SOFT);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    glyph = smart_home_lvgl_icon_create(badge, icon_name, 32, 32);
    if (glyph) {
        lv_obj_set_style_text_color(glyph, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor(glyph, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
        lv_obj_center(glyph);
    }

    if (ui && slot >= 0) {
        sw = lv_switch_create(card);
        lv_obj_set_size(sw, 40, 22);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        if (is_on) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_set_user_data(sw, (void *)(intptr_t)slot);
        lv_obj_add_event_cb(sw, device_card_switch_cb, LV_EVENT_ALL, ui);
    }

    label = smart_home_lvgl_label_create(card, title,
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY, 16);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 62, -12);
    label = smart_home_lvgl_label_create(card, detail,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY, 12);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 62, 14);
}

static lv_obj_t *create_action_button(lv_obj_t *parent,
                                      const char *text,
                                      lv_color_t bg,
                                      lv_color_t fg)
{
    lv_obj_t *btn;
    lv_obj_t *label;

    btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn,
                    smart_home_lvgl_compact() ? 62 : 78,
                    smart_home_lvgl_compact() ? 30 : 34);
    lv_obj_set_style_radius(btn, 8, 0);
    smart_home_lvgl_set_bg(btn, bg);

    label = smart_home_lvgl_label_create(btn,
                                         text,
                                         fg,
                                         smart_home_lvgl_compact() ? 10 : 12);
    lv_obj_center(label);
    return btn;
}

static void ctrl_cancel_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (ui && ui->ctrl_popup) {
        lv_obj_add_flag(ui->ctrl_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ctrl_toggle_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (!ui) {
        return;
    }

    ui->ctrl_pending_on = !ui->ctrl_pending_on;
    smart_home_lvgl_set_bg(ui->ctrl_switch,
                           ui->ctrl_pending_on ?
                               SMART_HOME_UI_COLOR_PRIMARY :
                               SMART_HOME_UI_COLOR_SURFACE_SOFT);
}

static void ctrl_slider_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (!ui || !ui->ctrl_slider || !ui->ctrl_value_label) {
        return;
    }

    ui->ctrl_pending_val = lv_slider_get_value(ui->ctrl_slider);
    update_popup_value(ui);
}

static void ctrl_mode_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (!ui || !ui->ctrl_mode_dd) {
        return;
    }

    ui->ctrl_pending_mode = lv_dropdown_get_selected(ui->ctrl_mode_dd);
    if (ui->ctrl_slider) {
        if (ui->ctrl_pending_mode == 2 || ui->ctrl_pending_mode == 3) {
            lv_obj_add_flag(ui->ctrl_slider, LV_OBJ_FLAG_HIDDEN);
            if (ui->ctrl_value_label) {
                lv_label_set_text(ui->ctrl_value_label, "当前模式无需设定温度");
            }
        } else {
            lv_obj_clear_flag(ui->ctrl_slider, LV_OBJ_FLAG_HIDDEN);
            update_popup_value(ui);
        }
    }
}

static void ctrl_fan_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (!ui || !ui->ctrl_fan_dd) {
        return;
    }

    ui->ctrl_pending_fan_speed = lv_dropdown_get_selected(ui->ctrl_fan_dd);
}

static void ctrl_confirm_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    smart_home_device_t *device;
    const char *room;

    if (!ui || !ui->app || !ui->device_state || !ui->ctrl_room_dd) {
        return;
    }

    device = smart_home_device_find_by_id(ui->device_state, ui->ctrl_device_id);
    ui->ctrl_pending_room_index = lv_dropdown_get_selected(ui->ctrl_room_dd);
    room = smart_home_room_get(ui->device_state, ui->ctrl_pending_room_index);
    if (!device || !room) {
        return;
    }
    if (strcmp(device->room, room) != 0 &&
        smart_home_device_service_update_meta(&ui->app->device_service,
                                              device->id, room,
                                              device->name) != AGENT_OK) {
        return;
    }

    if (smart_home_device_service_set_device_control(
            &ui->app->device_service, ui->ctrl_device_id,
            ui->ctrl_pending_on, ui->ctrl_pending_val,
            ui->ctrl_pending_mode, ui->ctrl_pending_fan_speed) != AGENT_OK) {
        return;
    }

    smart_home_lvgl_refresh_cards(ui);
    if (ui->ctrl_popup) {
        lv_obj_add_flag(ui->ctrl_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ctrl_edit_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    smart_home_device_t *device;

    if (!ui || !ui->app) {
        return;
    }

    device = smart_home_device_find_by_id(ui->device_state, ui->ctrl_device_id);
    if (!device) {
        return;
    }

    if (ui->ctrl_popup) {
        lv_obj_add_flag(ui->ctrl_popup, LV_OBJ_FLAG_HIDDEN);
    }
    open_device_editor(ui, device);
}

static void ctrl_delete_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (!ui || !ui->app || !ui->device_state) {
        return;
    }

    smart_home_device_service_remove(&ui->app->device_service,
                                     ui->ctrl_device_id);
    if (ui->ctrl_popup) {
        lv_obj_add_flag(ui->ctrl_popup, LV_OBJ_FLAG_HIDDEN);
    }
    smart_home_lvgl_refresh_cards(ui);
}

static void env_slider_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    char text[32];
    int value;

    if (!ui || !ui->env_slider || !ui->env_value_label) {
        return;
    }

    value = lv_slider_get_value(ui->env_slider);
    if (ui->env_pending_type == ENV_LIGHT) {
        value = (value / 10) * 10;
        lv_slider_set_value(ui->env_slider, value, LV_ANIM_OFF);
    }
    ui->env_pending_value = value;

    if (ui->env_pending_type == ENV_TEMP) {
        snprintf(text, sizeof(text), "%d C", value);
    } else if (ui->env_pending_type == ENV_HUM) {
        snprintf(text, sizeof(text), "%d%%", value);
    } else {
        snprintf(text, sizeof(text), "%dlx", value);
    }
    lv_label_set_text(ui->env_value_label, text);
}

static void env_cancel_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (ui && ui->env_popup) {
        lv_obj_add_flag(ui->env_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void env_confirm_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    smart_home_state_t *state;
    int temperature;
    int humidity;
    int ambient_light;

    if (!ui || !ui->app || !ui->device_state) {
        return;
    }

    state = ui->device_state;
    temperature = state->env_temperature;
    humidity = state->env_humidity;
    ambient_light = state->env_light;

    if (ui->env_pending_type == ENV_TEMP) {
        temperature = ui->env_pending_value;
    } else if (ui->env_pending_type == ENV_HUM) {
        humidity = ui->env_pending_value;
    } else {
        ambient_light = ui->env_pending_value;
    }

    smart_home_device_service_set_environment(&ui->app->device_service,
                                              temperature,
                                              humidity,
                                              ambient_light);
    smart_home_lvgl_refresh_cards(ui);
    if (ui->env_popup) {
        lv_obj_add_flag(ui->env_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void device_editor_cancel_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (ui && ui->device_popup) {
        layout_device_keyboard(ui, 0);
        lv_obj_add_flag(ui->device_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void device_editor_confirm_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    const char *name;
    const char *room;
    int ret;

    if (!ui || !ui->app || !ui->device_state || !ui->device_name_input ||
        !ui->device_room_dd) {
        return;
    }

    name = lv_textarea_get_text(ui->device_name_input);
    room = smart_home_room_get(ui->device_state,
                               lv_dropdown_get_selected(ui->device_room_dd));
    if (!room) {
        return;
    }

    if (ui->device_edit_id == 0) {
        smart_home_device_type_t type;

        if (!ui->device_type_dd) {
            return;
        }
        type = smart_home_device_type_from_index(lv_dropdown_get_selected(
            ui->device_type_dd));
        ret = smart_home_device_service_add(&ui->app->device_service,
                                            room, name, type, NULL);
    } else {
        ret = smart_home_device_service_update_meta(&ui->app->device_service,
                                                    ui->device_edit_id,
                                                    room,
                                                    name);
    }

    if (ret != AGENT_OK) {
        lv_label_set_text(ui->device_popup_title, "保存失败，请检查设备名称和房间");
        return;
    }

    layout_device_keyboard(ui, 0);
    lv_obj_add_flag(ui->device_popup, LV_OBJ_FLAG_HIDDEN);
    smart_home_lvgl_refresh_cards(ui);
}

static void panel_room_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    rebuild_device_cards(ui);
}

static void room_chip_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    lv_obj_t *chip = lv_event_get_current_target(event);
    lv_obj_t *row;
    int selected;
    uint32_t i;

    if (!ui || !ui->panel_room_dd || !chip ||
        lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    selected = (int)(intptr_t)lv_obj_get_user_data(chip);
    lv_dropdown_set_selected(ui->panel_room_dd, selected);
    row = lv_obj_get_parent(chip);
    for (i = 0; row && i < lv_obj_get_child_count(row); i++) {
        lv_obj_t *item = lv_obj_get_child(row, i);
        lv_obj_t *label;
        int active = (int)(intptr_t)lv_obj_get_user_data(item) == selected;

        smart_home_lvgl_set_bg(item, active ? SMART_HOME_UI_COLOR_SURFACE_ON :
                               SMART_HOME_UI_COLOR_SURFACE);
        label = lv_obj_get_child(item, 0);
        if (label) {
            lv_obj_set_style_text_color(label,
                                        active ? SMART_HOME_UI_COLOR_PRIMARY_DARK :
                                        SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                        0);
        }
    }
    rebuild_device_cards(ui);
}

static void create_room_chip(lv_obj_t *row, smart_home_lvgl_t *ui,
                             const char *text, int index)
{
    lv_obj_t *chip = lv_btn_create(row);
    lv_obj_t *label;
    int selected = ui && ui->panel_room_dd &&
                   lv_dropdown_get_selected(ui->panel_room_dd) == index;

    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, smart_home_lvgl_compact() ? 62 : 82, 34);
    lv_obj_set_style_radius(chip, 12, 0);
    smart_home_lvgl_set_bg(chip, selected ? SMART_HOME_UI_COLOR_SURFACE_ON :
                           SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_user_data(chip, (void *)(intptr_t)index);
    lv_obj_add_event_cb(chip, room_chip_cb, LV_EVENT_CLICKED, ui);
    label = smart_home_lvgl_label_create(chip, text,
                                         selected ? SMART_HOME_UI_COLOR_PRIMARY_DARK :
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY, 14);
    lv_obj_center(label);
}

static void room_popup_cancel_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    if (ui && ui->room_popup) {
        lv_obj_add_flag(ui->room_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void rebuild_room_filters(smart_home_lvgl_t *ui);

static void room_popup_confirm_cb(lv_event_t *event)
{
    static const char *const presets[] = {
        "kitchen", "bathroom", "study", "balcony",
    };
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    int selected;
    int ret;

    if (!ui || !ui->app || !ui->room_preset_dd) {
        return;
    }
    selected = lv_dropdown_get_selected(ui->room_preset_dd);
    if (selected < 0 || selected >= (int)(sizeof(presets) / sizeof(presets[0]))) {
        return;
    }
    ret = smart_home_device_service_add_room(&ui->app->device_service,
                                             presets[selected]);
    if (ret != AGENT_OK) {
        if (ui->room_popup_title) {
            lv_label_set_text(ui->room_popup_title, "新增房间失败或数量已满");
        }
        return;
    }
    {
        char rooms[128];

        room_dropdown_options(ui->device_state, rooms, sizeof(rooms), 1);
        lv_dropdown_set_options(ui->panel_room_dd, rooms);
        lv_dropdown_set_selected(ui->panel_room_dd, 0);
    }
    rebuild_room_filters(ui);
    rebuild_device_cards(ui);
    if (ui->room_popup) {
        lv_obj_add_flag(ui->room_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_room_creator(smart_home_lvgl_t *ui)
{
    lv_obj_t *popup;
    lv_obj_t *btn;
    int compact = smart_home_lvgl_compact();

    if (!ui || !ui->screen_panel) {
        return;
    }
    if (!ui->room_popup) {
        popup = lv_obj_create(ui->screen_panel);
        lv_obj_remove_style_all(popup);
        lv_obj_set_size(popup, compact ? 240 : 320, compact ? 154 : 184);
        lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
        smart_home_lvgl_card_style(popup);
        smart_home_lvgl_set_bg(popup, SMART_HOME_UI_COLOR_SURFACE);
        ui->room_popup = popup;

        ui->room_popup_title = smart_home_lvgl_label_create(
            popup, "新增房间", SMART_HOME_UI_COLOR_TEXT_PRIMARY,
            compact ? 14 : 16);
        lv_obj_align(ui->room_popup_title, LV_ALIGN_TOP_MID, 0,
                     compact ? 12 : 16);

        ui->room_preset_dd = lv_dropdown_create(popup);
        lv_dropdown_set_options(ui->room_preset_dd, "厨房\n卫生间\n书房\n阳台");
        lv_obj_set_size(ui->room_preset_dd, lv_pct(74), compact ? 32 : 36);
        lv_obj_align(ui->room_preset_dd, LV_ALIGN_TOP_MID, 0,
                     compact ? 48 : 62);
        lv_obj_set_style_text_font(ui->room_preset_dd,
                                   smart_home_lvgl_font(12), 0);

        btn = create_action_button(popup, "取消",
                                   SMART_HOME_UI_COLOR_BTN_SECONDARY,
                                   SMART_HOME_UI_COLOR_TEXT_PRIMARY);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, compact ? 22 : 30,
                     compact ? -10 : -16);
        lv_obj_add_event_cb(btn, room_popup_cancel_cb, LV_EVENT_CLICKED, ui);
        btn = create_action_button(popup, "创建",
                                   SMART_HOME_UI_COLOR_PRIMARY, lv_color_white());
        lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, compact ? -22 : -30,
                     compact ? -10 : -16);
        lv_obj_add_event_cb(btn, room_popup_confirm_cb, LV_EVENT_CLICKED, ui);
    }
    lv_label_set_text(ui->room_popup_title, "新增房间");
    lv_obj_clear_flag(ui->room_popup, LV_OBJ_FLAG_HIDDEN);
}

static void add_room_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    open_room_creator(ui);
}

static void rebuild_room_filters(smart_home_lvgl_t *ui)
{
    int i;

    if (!ui || !ui->panel_filter_row || !ui->panel_room_dd) {
        return;
    }
    lv_obj_clean(ui->panel_filter_row);
    create_room_chip(ui->panel_filter_row, ui, "全部", 0);
    for (i = 0; i < smart_home_room_count(ui->device_state); i++) {
        create_room_chip(ui->panel_filter_row, ui,
                         device_room_name(smart_home_room_get(ui->device_state, i)),
                         i + 1);
    }
    {
        lv_obj_t *btn = lv_btn_create(ui->panel_filter_row);
        lv_obj_t *label;

        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, smart_home_lvgl_compact() ? 72 : 96, 34);
        lv_obj_set_style_radius(btn, 12, 0);
        smart_home_lvgl_set_bg(btn, SMART_HOME_UI_COLOR_SURFACE_SOFT);
        lv_obj_add_event_cb(btn, add_room_cb, LV_EVENT_CLICKED, ui);
        label = smart_home_lvgl_label_create(btn, "+ 新增房间",
                                             SMART_HOME_UI_COLOR_PRIMARY_DARK,
                                             12);
        lv_obj_center(label);
    }
}

static void add_device_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);

    open_device_editor(ui, NULL);
}

static void update_popup_value(smart_home_lvgl_t *ui)
{
    smart_home_device_t *device;
    char text[32];

    if (!ui || !ui->ctrl_value_label || !ui->device_state) {
        return;
    }

    device = smart_home_device_find_by_id(ui->device_state, ui->ctrl_device_id);
    snprintf(text,
             sizeof(text),
             device && device->type == SMART_HOME_DEVICE_AC ? "%d°C" : "亮度 %d%%",
             ui->ctrl_pending_val);
    lv_label_set_text(ui->ctrl_value_label, text);
}

static void refresh_sensor_bar(smart_home_lvgl_t *ui)
{
    smart_home_state_t *state;
    smart_home_device_t *ac;
    char text[40];

    if (!ui) {
        return;
    }

    state = ui->device_state;
    if (!state) {
        return;
    }

    if (ui->env_temp_label) {
        snprintf(text, sizeof(text), "温度 %d°C", state->env_temperature);
        lv_label_set_text(ui->env_temp_label, text);
    }
    if (ui->env_hum_label) {
        snprintf(text, sizeof(text), "湿度 %d%%", state->env_humidity);
        lv_label_set_text(ui->env_hum_label, text);
    }
    if (ui->env_light_label) {
        snprintf(text, sizeof(text), "光照 %d lx", state->env_light);
        lv_label_set_text(ui->env_light_label, text);
    }
    if (ui->env_ac_label) {
        ac = smart_home_device_find_first(state,
                                          "bedroom",
                                          SMART_HOME_DEVICE_AC);
        if (ac) {
            snprintf(text,
                     sizeof(text),
                     "空调 %s %d°C",
                     ac->on ? smart_home_ac_mode_name(ac->ac_mode) : "已关闭",
                     ac->temperature);
        } else {
            snprintf(text, sizeof(text), "暂无空调");
        }
        lv_label_set_text(ui->env_ac_label, text);
    }
}

void smart_home_lvgl_refresh_cards(smart_home_lvgl_t *ui)
{
    if (!ui) {
        return;
    }

    rebuild_device_cards(ui);
    refresh_sensor_bar(ui);
    smart_home_lvgl_refresh_home(ui);
}

static void device_switch_refresh_cb(void *data)
{
    smart_home_lvgl_refresh_cards((smart_home_lvgl_t *)data);
}

static void ensure_control_popup(smart_home_lvgl_t *ui)
{
    lv_obj_t *popup;
    lv_obj_t *label;
    lv_obj_t *row;
    lv_obj_t *btn;
    int compact = smart_home_lvgl_compact();
    int drawer_w;
    int drawer_h;

    if (!ui || ui->ctrl_popup) {
        return;
    }

    popup = lv_obj_create(ui->screen_panel);
    lv_obj_remove_style_all(popup);
    drawer_w = compact ? smart_home_lvgl_content_w() :
               smart_home_lvgl_disp_w() * 62 / 100;
    drawer_h = smart_home_lvgl_disp_h() - SMART_HOME_TOPBAR_H -
               SMART_HOME_NAV_H - 10;
    lv_obj_set_size(popup, drawer_w, drawer_h);
    lv_obj_set_pos(popup, smart_home_lvgl_disp_w() -
                   smart_home_lvgl_pad_x() - drawer_w,
                   SMART_HOME_TOPBAR_H + 6);
    smart_home_lvgl_card_style(popup);
    smart_home_lvgl_set_bg(popup, SMART_HOME_UI_COLOR_SURFACE);
    ui->ctrl_popup = popup;

    ui->ctrl_title = smart_home_lvgl_label_create(popup,
                                                  "",
                                                  SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                                  compact ? 16 : 22);
    lv_obj_align(ui->ctrl_title, LV_ALIGN_TOP_LEFT,
                 compact ? 16 : 28, compact ? 10 : 18);

    ui->ctrl_switch = lv_btn_create(popup);
    lv_obj_remove_style_all(ui->ctrl_switch);
    lv_obj_set_size(ui->ctrl_switch, compact ? 82 : 96, compact ? 32 : 38);
    lv_obj_align(ui->ctrl_switch, LV_ALIGN_TOP_RIGHT,
                 compact ? -16 : -28, compact ? 10 : 16);
    lv_obj_set_style_radius(ui->ctrl_switch, 8, 0);
    lv_obj_add_event_cb(ui->ctrl_switch, ctrl_toggle_cb, LV_EVENT_CLICKED, ui);
    label = smart_home_lvgl_label_create(ui->ctrl_switch,
                                         "开关",
                                         lv_color_white(),
                                         12);
    lv_obj_center(label);

    label = smart_home_lvgl_label_create(popup, "所属房间",
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         compact ? 11 : 13);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, compact ? 16 : 28,
                 compact ? 52 : 68);
    ui->ctrl_room_dd = lv_dropdown_create(popup);
    lv_obj_set_size(ui->ctrl_room_dd, compact ? 112 : 142,
                    compact ? 30 : 34);
    lv_obj_align(ui->ctrl_room_dd, LV_ALIGN_TOP_LEFT,
                 compact ? 76 : 98, compact ? 46 : 60);
    lv_obj_set_style_text_font(ui->ctrl_room_dd, smart_home_lvgl_font(12), 0);

    ui->ctrl_value_label = smart_home_lvgl_label_create(popup,
                                                        "",
                                                        SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                        compact ? 16 : 22);
    lv_obj_align(ui->ctrl_value_label,
                 LV_ALIGN_TOP_LEFT,
                 compact ? 16 : 28,
                 compact ? 90 : 114);

    ui->ctrl_mode_dd = lv_dropdown_create(popup);
    lv_dropdown_set_options(ui->ctrl_mode_dd, "制冷\n制热\n除湿\n送风\n自动");
    lv_obj_set_size(ui->ctrl_mode_dd, compact ? 104 : 116, 32);
    lv_obj_align(ui->ctrl_mode_dd,
                 LV_ALIGN_TOP_LEFT,
                 compact ? 16 : 28,
                 compact ? 126 : 164);
    lv_obj_set_style_text_font(ui->ctrl_mode_dd, smart_home_lvgl_font(12), 0);
    lv_obj_add_event_cb(ui->ctrl_mode_dd,
                        ctrl_mode_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    ui->ctrl_fan_dd = lv_dropdown_create(popup);
    lv_dropdown_set_options(ui->ctrl_fan_dd, "低速\n中速\n高速\n自动");
    lv_obj_set_size(ui->ctrl_fan_dd, compact ? 104 : 116, 32);
    lv_obj_align(ui->ctrl_fan_dd,
                 LV_ALIGN_TOP_RIGHT,
                 compact ? -16 : -28,
                 compact ? 126 : 164);
    lv_obj_set_style_text_font(ui->ctrl_fan_dd, smart_home_lvgl_font(12), 0);
    lv_obj_add_event_cb(ui->ctrl_fan_dd,
                        ctrl_fan_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    ui->ctrl_slider = lv_slider_create(popup);
    lv_obj_set_size(ui->ctrl_slider, lv_pct(72), 8);
    lv_obj_align(ui->ctrl_slider, LV_ALIGN_TOP_MID, 0, compact ? 182 : 226);
    lv_obj_add_event_cb(ui->ctrl_slider,
                        ctrl_slider_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    row = lv_obj_create(popup);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row,
                    drawer_w - (compact ? 18 : 36),
                    compact ? 32 : 38);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, compact ? -10 : -18);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    btn = create_action_button(row,
                               "删除",
                               SMART_HOME_UI_COLOR_DANGER,
                               lv_color_white());
    lv_obj_add_event_cb(btn, ctrl_delete_cb, LV_EVENT_CLICKED, ui);

    btn = create_action_button(row,
                               "关闭",
                               SMART_HOME_UI_COLOR_BTN_SECONDARY,
                               SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    lv_obj_add_event_cb(btn, ctrl_cancel_cb, LV_EVENT_CLICKED, ui);

    btn = create_action_button(row,
                               "重命名",
                               SMART_HOME_UI_COLOR_SURFACE_SOFT,
                               SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    lv_obj_add_event_cb(btn, ctrl_edit_cb, LV_EVENT_CLICKED, ui);

    btn = create_action_button(row,
                               "保存",
                               SMART_HOME_UI_COLOR_PRIMARY,
                               lv_color_white());
    lv_obj_add_event_cb(btn, ctrl_confirm_cb, LV_EVENT_CLICKED, ui);
}

static void card_click_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    int slot =
        (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(event));
    smart_home_device_t *device;
    int is_ac;
    char rooms[128];

    if (!ui || !ui->device_state) {
        return;
    }

    device = smart_home_device_get_by_slot(ui->device_state, slot);
    if (!device) {
        return;
    }

    is_ac = device->type == SMART_HOME_DEVICE_AC;
    ui->ctrl_device_id = device->id;
    ui->ctrl_pending_on = device->on;
    ui->ctrl_pending_val = is_ac ? device->temperature : device->brightness;
    ui->ctrl_pending_mode = is_ac ? device->ac_mode : 0;
    ui->ctrl_pending_fan_speed = is_ac ? device->ac_fan_speed : 3;

    ensure_control_popup(ui);
    if (!ui->ctrl_popup) {
        return;
    }

    lv_label_set_text(ui->ctrl_title, device_display_name(device));
    room_dropdown_options(ui->device_state, rooms, sizeof(rooms), 0);
    lv_dropdown_set_options(ui->ctrl_room_dd, rooms);
    ui->ctrl_pending_room_index = smart_home_room_index(ui->device_state,
                                                         device->room);
    if (ui->ctrl_pending_room_index < 0) {
        ui->ctrl_pending_room_index = 0;
    }
    lv_dropdown_set_selected(ui->ctrl_room_dd, ui->ctrl_pending_room_index);
    smart_home_lvgl_set_bg(ui->ctrl_switch,
                           device->on ? SMART_HOME_UI_COLOR_PRIMARY :
                                        SMART_HOME_UI_COLOR_SURFACE_SOFT);
    lv_slider_set_range(ui->ctrl_slider, is_ac ? 16 : 0, is_ac ? 30 : 100);
    lv_slider_set_value(ui->ctrl_slider, ui->ctrl_pending_val, LV_ANIM_OFF);

    if (is_ac) {
        lv_dropdown_set_selected(ui->ctrl_mode_dd, ui->ctrl_pending_mode);
        lv_dropdown_set_selected(ui->ctrl_fan_dd, ui->ctrl_pending_fan_speed);
        lv_obj_clear_flag(ui->ctrl_mode_dd, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->ctrl_fan_dd, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui->ctrl_mode_dd, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->ctrl_fan_dd, LV_OBJ_FLAG_HIDDEN);
    }

    if (is_ac && (ui->ctrl_pending_mode == 2 || ui->ctrl_pending_mode == 3)) {
        lv_obj_add_flag(ui->ctrl_slider, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui->ctrl_value_label, "当前模式无需设定温度");
    } else {
        lv_obj_clear_flag(ui->ctrl_slider, LV_OBJ_FLAG_HIDDEN);
        update_popup_value(ui);
    }
    lv_obj_clear_flag(ui->ctrl_popup, LV_OBJ_FLAG_HIDDEN);
}

static void env_click_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)lv_event_get_user_data(event);
    int type =
        (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(event));
    smart_home_state_t *state;
    int min = 0;
    int max = 100;
    const char *title = "Environment";

    if (!ui || !ui->device_state) {
        return;
    }

    state = ui->device_state;
    ui->env_pending_type = type;
    if (type == ENV_TEMP) {
        title = "Temperature";
        max = 45;
        ui->env_pending_value = state->env_temperature;
    } else if (type == ENV_HUM) {
        title = "Humidity";
        max = 100;
        ui->env_pending_value = state->env_humidity;
    } else {
        title = "Ambient Light";
        max = 1000;
        ui->env_pending_value = state->env_light;
    }

    if (!ui->env_popup) {
        lv_obj_t *popup;
        lv_obj_t *btn;
        lv_obj_t *label;
        int compact = smart_home_lvgl_compact();

        popup = lv_obj_create(ui->screen_panel);
        lv_obj_remove_style_all(popup);
        lv_obj_set_size(popup, smart_home_lvgl_content_w(), compact ? 188 : 216);
        lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
        smart_home_lvgl_card_style(popup);
        smart_home_lvgl_set_bg(popup, SMART_HOME_UI_COLOR_SURFACE);
        ui->env_popup = popup;

        ui->env_title = smart_home_lvgl_label_create(popup,
                                                     "",
                                                     SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                                     compact ? 14 : 16);
        lv_obj_align(ui->env_title, LV_ALIGN_TOP_MID, 0, compact ? 12 : 16);

        ui->env_value_label = smart_home_lvgl_label_create(popup,
                                                           "",
                                                           SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                           compact ? 14 : 16);
        lv_obj_align(ui->env_value_label,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 48 : 58);

        ui->env_slider = lv_slider_create(popup);
        lv_obj_set_size(ui->env_slider, lv_pct(78), 8);
        lv_obj_align(ui->env_slider, LV_ALIGN_TOP_MID, 0, compact ? 86 : 102);
        lv_obj_add_event_cb(ui->env_slider,
                            env_slider_cb,
                            LV_EVENT_VALUE_CHANGED,
                            ui);

        btn = create_action_button(popup,
                                   "Cancel",
                                   SMART_HOME_UI_COLOR_BTN_SECONDARY,
                                   SMART_HOME_UI_COLOR_TEXT_PRIMARY);
        lv_obj_align(btn,
                     LV_ALIGN_BOTTOM_LEFT,
                     compact ? 24 : 30,
                     compact ? -10 : -18);
        lv_obj_add_event_cb(btn, env_cancel_cb, LV_EVENT_CLICKED, ui);

        btn = create_action_button(popup,
                                   "Apply",
                                   SMART_HOME_UI_COLOR_PRIMARY,
                                   lv_color_white());
        lv_obj_align(btn,
                     LV_ALIGN_BOTTOM_RIGHT,
                     compact ? -24 : -30,
                     compact ? -10 : -18);
        lv_obj_add_event_cb(btn, env_confirm_cb, LV_EVENT_CLICKED, ui);

        (void)label;
    }

    lv_label_set_text(ui->env_title, title);
    lv_slider_set_range(ui->env_slider, min, max);
    lv_slider_set_value(ui->env_slider, ui->env_pending_value, LV_ANIM_OFF);
    env_slider_cb(event);
    lv_obj_clear_flag(ui->env_popup, LV_OBJ_FLAG_HIDDEN);
}

static void open_device_editor(smart_home_lvgl_t *ui,
                               const smart_home_device_t *device)
{
    lv_obj_t *popup;
    lv_obj_t *btn;
    int is_edit = device != NULL;
    int compact = smart_home_lvgl_compact();
    char rooms[128];

    if (!ui || !ui->screen_panel) {
        return;
    }

    if (!ui->device_popup) {
        popup = lv_obj_create(ui->screen_panel);
        lv_obj_remove_style_all(popup);
        lv_obj_set_size(popup, smart_home_lvgl_content_w(), compact ? 218 : 274);
        lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
        smart_home_lvgl_card_style(popup);
        smart_home_lvgl_set_bg(popup, SMART_HOME_UI_COLOR_SURFACE);
        ui->device_popup = popup;

        ui->device_popup_title = smart_home_lvgl_label_create(
            popup,
            "",
            SMART_HOME_UI_COLOR_TEXT_PRIMARY,
            compact ? 14 : 16);
        lv_obj_align(ui->device_popup_title,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 8 : 14);

        ui->device_name_input = lv_textarea_create(popup);
        lv_textarea_set_one_line(ui->device_name_input, true);
        lv_textarea_set_max_length(ui->device_name_input,
                                   SMART_HOME_DEVICE_NAME_SIZE - 1);
        lv_obj_set_size(ui->device_name_input,
                        lv_pct(78),
                        compact ? 32 : 36);
        lv_obj_align(ui->device_name_input,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 38 : 52);
        lv_obj_set_style_text_font(ui->device_name_input,
                                   smart_home_lvgl_font(12),
                                   0);
        lv_obj_add_event_cb(ui->device_name_input,
                            device_name_input_cb,
                            LV_EVENT_ALL,
                            ui);

        ui->device_room_dd = lv_dropdown_create(popup);
        lv_dropdown_set_options(ui->device_room_dd, "客厅\n卧室");
        lv_obj_set_size(ui->device_room_dd, lv_pct(78), compact ? 32 : 34);
        lv_obj_align(ui->device_room_dd,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 82 : 102);
        lv_obj_set_style_text_font(ui->device_room_dd,
                                   smart_home_lvgl_font(12),
                                   0);

        ui->device_type_dd = lv_dropdown_create(popup);
        lv_dropdown_set_options(ui->device_type_dd, "灯光\n空调");
        lv_obj_set_size(ui->device_type_dd, lv_pct(78), compact ? 32 : 34);
        lv_obj_align(ui->device_type_dd,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 126 : 152);
        lv_obj_set_style_text_font(ui->device_type_dd,
                                   smart_home_lvgl_font(12),
                                   0);

        btn = create_action_button(popup,
                                   "取消",
                                   SMART_HOME_UI_COLOR_BTN_SECONDARY,
                                   SMART_HOME_UI_COLOR_TEXT_PRIMARY);
        lv_obj_align(btn,
                     LV_ALIGN_BOTTOM_LEFT,
                     compact ? 24 : 30,
                     compact ? -10 : -18);
        lv_obj_add_event_cb(btn,
                            device_editor_cancel_cb,
                            LV_EVENT_CLICKED,
                            ui);

        btn = create_action_button(popup,
                                   "保存",
                                   SMART_HOME_UI_COLOR_PRIMARY,
                                   lv_color_white());
        lv_obj_align(btn,
                     LV_ALIGN_BOTTOM_RIGHT,
                     compact ? -24 : -30,
                     compact ? -10 : -18);
        lv_obj_add_event_cb(btn,
                            device_editor_confirm_cb,
                            LV_EVENT_CLICKED,
                            ui);

        ui->device_keyboard = lv_keyboard_create(ui->screen_panel);
        smart_home_lvgl_style_keyboard(ui->device_keyboard);
        lv_obj_add_event_cb(ui->device_keyboard,
                            device_keyboard_cb,
                            LV_EVENT_ALL,
                            ui);
        layout_device_keyboard(ui, 0);
    }

    ui->device_edit_id = is_edit ? device->id : 0;
    room_dropdown_options(ui->device_state, rooms, sizeof(rooms), 0);
    lv_dropdown_set_options(ui->device_room_dd, rooms);
    lv_label_set_text(ui->device_popup_title,
                      is_edit ? "编辑设备" : "添加设备");
    lv_textarea_set_text(ui->device_name_input,
                         is_edit ? device->name : "");
    {
        int room_index = is_edit ?
            smart_home_room_index(ui->device_state, device->room) : 0;

        lv_dropdown_set_selected(ui->device_room_dd,
                                 room_index >= 0 ? room_index : 0);
    }
    lv_dropdown_set_selected(
        ui->device_type_dd,
        is_edit ? smart_home_device_type_to_index(device->type) : 0);

    if (is_edit) {
        lv_obj_add_flag(ui->device_type_dd, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ui->device_type_dd, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(ui->device_popup, LV_OBJ_FLAG_HIDDEN);
}

#ifndef CONFIG_SMART_HOME_MILOCO_BRIDGE
static lv_obj_t *create_device_card(lv_obj_t *grid,
                                    smart_home_lvgl_t *ui,
                                    int slot,
                                    const smart_home_device_t *device)
{
    int card_w = device_card_width();
    int card_h = smart_home_lvgl_compact() ? 92 : 112;
    lv_obj_t *card = lv_obj_create(grid);
    char text[128];

    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    lv_obj_set_user_data(card, (void *)(intptr_t)slot);
    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, ui);

    format_device_card_text(device, text, sizeof(text));
    set_card_content(card, device_icon(device), device_display_name(device), text,
                     device && device->on, ui, slot);

    return card;
}
#endif

#ifndef CONFIG_SMART_HOME_MILOCO_BRIDGE
static lv_obj_t *create_add_card(lv_obj_t *grid, smart_home_lvgl_t *ui)
{
    int card_w = device_card_width();
    int card_h = smart_home_lvgl_compact() ? 92 : 112;
    lv_obj_t *card = lv_obj_create(grid);

    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    lv_obj_add_event_cb(card, add_device_cb, LV_EVENT_CLICKED, ui);
    set_card_content(card, ICON_ADD, "添加设备", "接入新的家庭设备", 0,
                     NULL, -1);
    smart_home_lvgl_set_bg(card, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    return card;
}
#endif

#ifdef CONFIG_SMART_HOME_NODE_GATEWAY
static void format_remote_command_text(const caddons_node_info_t *node,
                                       char *buffer,
                                       size_t buffer_size)
{
    const char *separator = "";
    size_t i;
    size_t used;

    if (!buffer || buffer_size == 0u) {
        return;
    }

    snprintf(buffer, buffer_size, "Commands: ");
    used = strlen(buffer);
    if (!node || node->command_count == 0u) {
        snprintf(buffer, buffer_size, "Commands: none");
        return;
    }

    for (i = 0; i < node->command_count &&
                i < CAGENT_ADDONS_MAX_NODE_COMMANDS; i++) {
        int written;

        if (!node->commands[i][0]) {
            continue;
        }
        written = snprintf(buffer + used, buffer_size - used, "%s%s",
                           separator, node->commands[i]);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            if (buffer_size >= 4u) {
                memcpy(buffer + buffer_size - 4u, "...", 4u);
            }
            return;
        }
        used += (size_t)written;
        separator = ", ";
    }

    if (used == strlen("Commands: ")) {
        snprintf(buffer, buffer_size, "Commands: none");
    }
}

static lv_obj_t *create_remote_node_card(lv_obj_t *grid,
                                         const caddons_node_info_t *node)
{
    int card_w = device_card_width();
    int card_h = smart_home_lvgl_compact() ? 104 : 112;
    int font_size = smart_home_lvgl_compact() ? 10 : 11;
    lv_obj_t *card;
    lv_obj_t *content;
    lv_obj_t *label;
    char commands[192];
    const char *name;

    card = lv_obj_create(grid);
    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    smart_home_lvgl_set_bg(card, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    content = lv_obj_create(card);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, smart_home_lvgl_compact() ? 2 : 4, 0);
    lv_obj_set_style_pad_left(content, 6, 0);
    lv_obj_set_style_pad_right(content, 6, 0);

    smart_home_lvgl_icon_create(content,
                                ICON_DEVICE_GENERIC,
                                smart_home_lvgl_compact() ? 18 : 22,
                                smart_home_lvgl_compact() ? 18 : 22);
    name = node->display_name[0] ? node->display_name : node->node_id;
    label = smart_home_lvgl_label_create(content, name,
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         font_size + 1);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    label = smart_home_lvgl_label_create(content, "Online",
                                         SMART_HOME_UI_COLOR_SUCCESS,
                                         font_size);
    label = smart_home_lvgl_label_create(content, "Source: Node",
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         font_size);
    format_remote_command_text(node, commands, sizeof(commands));
    label = smart_home_lvgl_label_create(content, commands,
                                         SMART_HOME_UI_COLOR_TEXT_MUTED,
                                         font_size);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return card;
}

static void append_online_remote_node_cards(smart_home_lvgl_t *ui)
{
    caddons_node_info_t nodes[CAGENT_ADDONS_MAX_NODES];
    uint32_t revision = 0;
    size_t count;
    size_t i;

    if (!ui || !ui->app || !ui->app->node_gateway) {
        ui->remote_node_revision = 0;
        return;
    }

    count = smart_home_node_gateway_list(ui->app->node_gateway, nodes,
                                          CAGENT_ADDONS_MAX_NODES, &revision);
    ui->remote_node_revision = revision;
    for (i = 0; i < count && i < CAGENT_ADDONS_MAX_NODES; i++) {
        if (nodes[i].online) {
            create_remote_node_card(ui->panel_grid, &nodes[i]);
        }
    }
}

static void remote_node_timer_cb(lv_timer_t *timer)
{
    smart_home_lvgl_t *ui = lv_timer_get_user_data(timer);
    uint32_t revision = 0;

    if (!ui || !ui->app || !ui->app->node_gateway) {
        return;
    }

    (void)smart_home_node_gateway_list(ui->app->node_gateway, NULL, 0,
                                        &revision);
    if (revision != ui->remote_node_revision) {
        smart_home_lvgl_refresh_cards(ui);
    }
}
#endif

#ifdef CONFIG_SMART_HOME_MILOCO_BRIDGE
/* 米家（Miloco 网关）远程设备卡：电源开关提交异步控制，实际状态
 * 以 worker 轮询回读为准（revision 变化触发整卡重建）。 */
static void miloco_switch_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    lv_obj_t *sw = lv_event_get_current_target(event);
    const char *did = lv_obj_get_user_data(sw);

    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || !ui ||
        !ui->app || !ui->app->miloco || !did) {
        return;
    }
    if (smart_home_miloco_submit_power(ui->app->miloco, did,
                                       lv_obj_has_state(sw,
                                                        LV_STATE_CHECKED))
            != AGENT_OK) {
        /* 提交失败：回读当前缓存状态以恢复开关视觉。 */
        lv_obj_remove_state(sw, LV_STATE_CHECKED);
    }
}

static lv_obj_t *create_miloco_card(smart_home_lvgl_t *ui,
                                    const smart_home_miloco_device_t *device)
{
    int compact = smart_home_lvgl_compact();
    int card_w = device_card_width();
    int card_h = compact ? 104 : 112;
    int font_size = compact ? 10 : 11;
    const char *icon = ICON_DEVICE_GENERIC;
    lv_obj_t *card;
    lv_obj_t *content;
    lv_obj_t *label;
    lv_obj_t *sw;
    char room_text[48];

    switch (device->category) {
    case SMART_HOME_MILOCO_CATEGORY_LIGHT:
        icon = ICON_DEVICE_LIGHT;
        break;
    case SMART_HOME_MILOCO_CATEGORY_AC:
        icon = ICON_DEVICE_AC;
        break;
    default:
        icon = ICON_DEVICE_GENERIC;
        break;
    }

    card = lv_obj_create(ui->panel_grid);
    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    smart_home_lvgl_set_bg(card, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    content = lv_obj_create(card);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, compact ? 2 : 4, 0);
    lv_obj_set_style_pad_left(content, 6, 0);
    lv_obj_set_style_pad_right(content, 6, 0);

    smart_home_lvgl_icon_create(content, icon, compact ? 18 : 22,
                                compact ? 18 : 22);
    label = smart_home_lvgl_label_create(content, device->name,
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         font_size + 1);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    if (device->room[0]) {
        snprintf(room_text, sizeof(room_text), "%s · 米家",
                 device->room);
    } else {
        snprintf(room_text, sizeof(room_text), "米家");
    }
    label = smart_home_lvgl_label_create(content, room_text,
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         font_size);
    label = smart_home_lvgl_label_create(
        content, device->online ? "Online" : "Offline",
        device->online ? SMART_HOME_UI_COLOR_SUCCESS :
                         SMART_HOME_UI_COLOR_TEXT_MUTED,
        font_size);

    if (device->controllable) {
        sw = lv_switch_create(content);
        lv_obj_set_size(sw, 40, 22);
        if (device->power_on) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        /* did 字符串由设备缓存长期持有，卡片生命周期内有效。 */
        lv_obj_set_user_data(sw, (void *)device->did);
        lv_obj_add_event_cb(sw, miloco_switch_cb, LV_EVENT_VALUE_CHANGED, ui);
    }
    return card;
}

/* 网关未连接时设备页的主体提示：真实设备在网关连接后出现。 */
static void create_miloco_placeholder_card(smart_home_lvgl_t *ui)
{
    lv_obj_t *card;
    lv_obj_t *label;
    int card_w = device_card_width();
    int card_h = smart_home_lvgl_compact() ? 104 : 112;

    card = lv_obj_create(ui->panel_grid);
    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    smart_home_lvgl_set_bg(card, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    label = smart_home_lvgl_label_create(card, "米家设备",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                         smart_home_lvgl_compact() ? 11 : 12);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    label = smart_home_lvgl_label_create(
        card, "网关未连接\n在系统设置中配置\n米家网关后显示",
        SMART_HOME_UI_COLOR_TEXT_SECONDARY,
        smart_home_lvgl_compact() ? 10 : 11);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 24);
}

static void append_miloco_cards(smart_home_lvgl_t *ui)
{
    smart_home_miloco_device_t devices[SMART_HOME_MILOCO_MAX_DEVICES];
    size_t count;
    size_t i;

    if (!ui || !ui->app || !ui->app->miloco) {
        ui->miloco_revision = 0;
        return;
    }
    count = smart_home_miloco_list(ui->app->miloco, devices,
                                   SMART_HOME_MILOCO_MAX_DEVICES,
                                   &ui->miloco_revision);
    for (i = 0; i < count; i++) {
        create_miloco_card(ui, &devices[i]);
    }
}

static void miloco_timer_cb(lv_timer_t *timer)
{
    smart_home_lvgl_t *ui = lv_timer_get_user_data(timer);
    uint32_t revision = 0;

    if (!ui || !ui->app || !ui->app->miloco) {
        return;
    }
    (void)smart_home_miloco_list(ui->app->miloco, NULL, 0, &revision);
    if (revision != ui->miloco_revision) {
        smart_home_lvgl_refresh_cards(ui);
    }
}

/* 设备页可见时启动轮询、离开时停止（与安防页摄像头同一门控模式）。 */
void smart_home_lvgl_miloco_poll_set_enabled(smart_home_lvgl_t *ui, int enable)
{
    if (!ui) {
        return;
    }
    if (enable && !ui->miloco_timer) {
        ui->miloco_timer = lv_timer_create(miloco_timer_cb, 1000, ui);
    } else if (!enable && ui->miloco_timer) {
        lv_timer_delete(ui->miloco_timer);
        ui->miloco_timer = NULL;
    }
}
#endif

static void rebuild_device_cards(smart_home_lvgl_t *ui)
{
    int slot;

    if (!ui || !ui->panel_grid || !ui->device_state) {
        return;
    }

    lv_obj_clean(ui->panel_grid);
#ifdef CONFIG_SMART_HOME_MILOCO_BRIDGE
    /* 米家桥接模式下设备页只呈现真实设备（米家 + Node）；本地虚拟
     * 设备不上屏，仅保留给 Agent 本地工具与场景使用。 */
    for (slot = 0; slot < SMART_HOME_MAX_DEVICES; slot++) {
        ui->device_cards[slot] = NULL;
    }
    ui->panel_add_btn = NULL;
    if (!ui->app || !ui->app->miloco ||
        !smart_home_miloco_reachable(ui->app->miloco)) {
        create_miloco_placeholder_card(ui);
    }
#else
    for (slot = 0; slot < SMART_HOME_MAX_DEVICES; slot++) {
        smart_home_device_t *device;

        ui->device_cards[slot] = NULL;
        device = smart_home_device_get_by_slot(ui->device_state, slot);
        if (!device) {
            continue;
        }
        if (!device_matches_filter(ui, device)) {
            continue;
        }

        ui->device_cards[slot] =
            create_device_card(ui->panel_grid, ui, slot, device);
    }

    ui->panel_add_btn = create_add_card(ui->panel_grid, ui);
#endif

#ifdef CONFIG_SMART_HOME_NODE_GATEWAY
    append_online_remote_node_cards(ui);
#endif
#ifdef CONFIG_SMART_HOME_MILOCO_BRIDGE
    append_miloco_cards(ui);
#endif
}

static lv_obj_t *create_sensor_entry(lv_obj_t *row,
                                     smart_home_lvgl_t *ui,
                                     int type,
                                     lv_obj_t **label_out)
{
    int entry_w = smart_home_lvgl_content_w() / 4 - 4;
    lv_obj_t *entry = lv_btn_create(row);
    lv_obj_t *content;
    lv_obj_t *icon;
    lv_obj_t *label;
    const char *icon_name = ICON_TEMP;

    lv_obj_remove_style_all(entry);
    lv_obj_set_size(entry, entry_w, smart_home_lvgl_compact() ? 24 : 28);
    lv_obj_set_style_bg_opa(entry, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(entry, 8, 0);
    lv_obj_set_user_data(entry, (void *)(intptr_t)type);
    lv_obj_add_event_cb(entry, env_click_cb, LV_EVENT_CLICKED, ui);

    if (type == ENV_HUM) {
        icon_name = ICON_HUMIDITY;
    } else if (type == ENV_LIGHT) {
        icon_name = ICON_SUN;
    }

    content = lv_obj_create(entry);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, smart_home_lvgl_compact() ? 3 : 4, 0);
    lv_obj_center(content);

    icon = smart_home_lvgl_icon_create(content,
                                       icon_name,
                                       smart_home_lvgl_compact() ? 14 : 16,
                                       smart_home_lvgl_compact() ? 14 : 16);
    if (icon) {
        lv_obj_set_style_text_color(icon,
                                    SMART_HOME_UI_COLOR_TEXT_MUTED,
                                    0);
        lv_obj_set_style_image_recolor(icon,
                                        SMART_HOME_UI_COLOR_TEXT_MUTED,
                                        0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    }

    label = smart_home_lvgl_label_create(content,
                                         "",
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                         smart_home_lvgl_compact() ? 11 : 12);
    if (label_out) {
        *label_out = label;
    }

    return entry;
}

void smart_home_lvgl_build_panel_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *grid;
    lv_obj_t *sensor_bar;
    lv_obj_t *filter_row;
    lv_obj_t *label;
    int content_w = smart_home_lvgl_content_w();
    int pad_x = smart_home_lvgl_pad_x();
    int compact = smart_home_lvgl_compact();
    int grid_y = SMART_HOME_TOPBAR_H + (compact ? 84 : 122);
    int sensor_h = compact ? 26 : 30;
    int sensor_bottom =
        SMART_HOME_NAV_H + SMART_HOME_NAV_BOTTOM_PAD + (compact ? 8 : 16);
    int grid_h = smart_home_lvgl_disp_h() -
                 grid_y -
                 sensor_bottom -
                 sensor_h -
                 (compact ? 6 : 12);
    char room_options[128];

    if (grid_h < (compact ? 84 : 120)) {
        grid_h = compact ? 84 : 120;
    }

    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    smart_home_lvgl_set_bg(screen, SMART_HOME_UI_COLOR_BG);
    ui->screen_panel = screen;

    smart_home_lvgl_build_top_bar(screen, ui, "OpenVela HOME");

    ui->panel_title = smart_home_lvgl_label_create(screen,
                                                   "我的设备",
                                                   SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                                   compact ? 20 : 28);
    lv_obj_align(ui->panel_title,
                 LV_ALIGN_TOP_LEFT,
                 pad_x,
                 SMART_HOME_TOPBAR_H + (compact ? 10 : 26));

    ui->panel_room_dd = lv_dropdown_create(screen);
    room_dropdown_options(ui->device_state, room_options,
                          sizeof(room_options), 1);
    lv_dropdown_set_options(ui->panel_room_dd, room_options);
    /* Keep the existing selection contract as the source of truth while the
     * product page exposes it as touch-friendly room chips below. */
    lv_obj_set_size(ui->panel_room_dd, 1, 1);
    lv_obj_add_flag(ui->panel_room_dd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui->panel_room_dd,
                        panel_room_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    filter_row = lv_obj_create(screen);
    lv_obj_remove_style_all(filter_row);
    lv_obj_set_size(filter_row, content_w, 38);
    lv_obj_align(filter_row, LV_ALIGN_TOP_LEFT, pad_x,
                 SMART_HOME_TOPBAR_H + (compact ? 44 : 70));
    lv_obj_set_style_bg_opa(filter_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filter_row, 0, 0);
    lv_obj_set_style_pad_column(filter_row, compact ? 6 : 10, 0);
    lv_obj_set_flex_flow(filter_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filter_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(filter_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(filter_row, LV_SCROLLBAR_MODE_OFF);
    ui->panel_filter_row = filter_row;
    rebuild_room_filters(ui);

    grid = lv_obj_create(screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, content_w, grid_h);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, grid_y);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, compact ? 8 : 12, 0);
    lv_obj_set_style_pad_column(grid, compact ? 8 : 12, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    ui->panel_grid = grid;

    sensor_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(sensor_bar);
    lv_obj_set_size(sensor_bar, content_w, sensor_h);
    lv_obj_align(sensor_bar,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 -sensor_bottom);
    lv_obj_set_style_bg_opa(sensor_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(sensor_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sensor_bar,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sensor_bar, 6, 0);
    ui->panel_sensor_bar = sensor_bar;

    create_sensor_entry(sensor_bar, ui, ENV_TEMP, &ui->env_temp_label);
    label = smart_home_lvgl_label_create(sensor_bar,
                                         "|",
                                         SMART_HOME_UI_COLOR_TEXT_MUTED,
                                         12);
    lv_obj_center(label);
    create_sensor_entry(sensor_bar, ui, ENV_HUM, &ui->env_hum_label);
    label = smart_home_lvgl_label_create(sensor_bar,
                                         "|",
                                         SMART_HOME_UI_COLOR_TEXT_MUTED,
                                         12);
    lv_obj_center(label);
    create_sensor_entry(sensor_bar, ui, ENV_LIGHT, &ui->env_light_label);
    label = smart_home_lvgl_label_create(sensor_bar,
                                         "|",
                                         SMART_HOME_UI_COLOR_TEXT_MUTED,
                                         12);
    lv_obj_center(label);
    ui->env_ac_label = smart_home_lvgl_label_create(sensor_bar,
                                                   "",
                                                   SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                   12);

    smart_home_lvgl_build_nav_bar(screen, ui);
    smart_home_lvgl_refresh_cards(ui);
#ifdef CONFIG_SMART_HOME_NODE_GATEWAY
    ui->remote_node_timer = lv_timer_create(remote_node_timer_cb, 500, ui);
#endif
}
