/**
 * smart_home LVGL panel screen.
 */

#include "smart_home_lvgl_internal.h"
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

static int device_matches_filter(smart_home_lvgl_t *ui,
                                 const smart_home_device_t *device)
{
    int filter;

    if (!ui || !ui->panel_room_dd || !device) {
        return 1;
    }

    filter = lv_dropdown_get_selected(ui->panel_room_dd);
    if (filter == 1) {
        return smart_home_room_to_index(device->room) == 0;
    }
    if (filter == 2) {
        return smart_home_room_to_index(device->room) == 1;
    }
    return 1;
}

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
                     "%s\n%s | %d C | %s",
                     device->name,
                     smart_home_ac_mode_name(device->ac_mode),
                     device->temperature,
                     smart_home_ac_fan_speed_name(device->ac_fan_speed));
        } else {
            snprintf(buffer, buffer_size, "%s\n%s | OFF", device->name,
                     device->room);
        }
    } else if (device->on) {
        snprintf(buffer,
                 buffer_size,
                 "%s\n%s | %d%%",
                 device->name,
                 device->room,
                 device->brightness);
    } else {
        snprintf(buffer, buffer_size, "%s\n%s | OFF", device->name,
                 device->room);
    }
}

static void set_card_content(lv_obj_t *card,
                             const char *icon_name,
                             const char *text,
                             int is_on)
{
    lv_obj_t *cont;
    lv_obj_t *label;

    lv_obj_clean(card);

    cont = lv_obj_create(card);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, smart_home_lvgl_compact() ? 4 : 6, 0);
    lv_obj_center(cont);

    smart_home_lvgl_icon_create(cont,
                                icon_name,
                                smart_home_lvgl_compact() ? 22 : 28,
                                smart_home_lvgl_compact() ? 22 : 28);

    label = smart_home_lvgl_label_create(cont,
                                         text,
                                         is_on ?
                                             SMART_HOME_UI_COLOR_TEXT_PRIMARY :
                                             SMART_HOME_UI_COLOR_TEXT_MUTED,
                                         smart_home_lvgl_compact() ? 11 : 12);
    lv_obj_set_width(label, smart_home_lvgl_compact() ? 112 : 150);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
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
                lv_label_set_text(ui->ctrl_value_label, "No temp target");
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

    if (!ui || !ui->app) {
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
    room = smart_home_room_from_index(lv_dropdown_get_selected(
        ui->device_room_dd));

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
        lv_label_set_text(ui->device_popup_title, "Apply failed");
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
             device && device->type == SMART_HOME_DEVICE_AC ? "%d C" : "%d%%",
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
        snprintf(text, sizeof(text), "Temp %dC", state->env_temperature);
        lv_label_set_text(ui->env_temp_label, text);
    }
    if (ui->env_hum_label) {
        snprintf(text, sizeof(text), "Hum %d%%", state->env_humidity);
        lv_label_set_text(ui->env_hum_label, text);
    }
    if (ui->env_light_label) {
        snprintf(text, sizeof(text), "Light %dlx", state->env_light);
        lv_label_set_text(ui->env_light_label, text);
    }
    if (ui->env_ac_label) {
        ac = smart_home_device_find_first(state,
                                          "bedroom",
                                          SMART_HOME_DEVICE_AC);
        if (ac) {
            snprintf(text,
                     sizeof(text),
                     "AC %s %dC",
                     ac->on ? smart_home_ac_mode_name(ac->ac_mode) : "off",
                     ac->temperature);
        } else {
            snprintf(text, sizeof(text), "AC none");
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
}

static void ensure_control_popup(smart_home_lvgl_t *ui)
{
    lv_obj_t *popup;
    lv_obj_t *label;
    lv_obj_t *row;
    lv_obj_t *btn;
    int compact = smart_home_lvgl_compact();

    if (!ui || ui->ctrl_popup) {
        return;
    }

    popup = lv_obj_create(ui->screen_panel);
    lv_obj_remove_style_all(popup);
    lv_obj_set_size(popup, smart_home_lvgl_content_w(), compact ? 214 : 306);
    lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
    smart_home_lvgl_card_style(popup);
    smart_home_lvgl_set_bg(popup, SMART_HOME_UI_COLOR_SURFACE);
    ui->ctrl_popup = popup;

    ui->ctrl_title = smart_home_lvgl_label_create(popup,
                                                  "",
                                                  SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                                  compact ? 14 : 16);
    lv_obj_align(ui->ctrl_title, LV_ALIGN_TOP_MID, 0, compact ? 8 : 14);

    ui->ctrl_switch = lv_btn_create(popup);
    lv_obj_remove_style_all(ui->ctrl_switch);
    lv_obj_set_size(ui->ctrl_switch, compact ? 82 : 92, compact ? 32 : 38);
    lv_obj_align(ui->ctrl_switch, LV_ALIGN_TOP_MID, 0, compact ? 34 : 54);
    lv_obj_set_style_radius(ui->ctrl_switch, 8, 0);
    lv_obj_add_event_cb(ui->ctrl_switch, ctrl_toggle_cb, LV_EVENT_CLICKED, ui);
    label = smart_home_lvgl_label_create(ui->ctrl_switch,
                                         "ON / OFF",
                                         lv_color_white(),
                                         12);
    lv_obj_center(label);

    ui->ctrl_value_label = smart_home_lvgl_label_create(popup,
                                                        "",
                                                        SMART_HOME_UI_COLOR_TEXT_SECONDARY,
                                                        compact ? 12 : 14);
    lv_obj_align(ui->ctrl_value_label,
                 LV_ALIGN_TOP_MID,
                 0,
                 compact ? 74 : 104);

    ui->ctrl_mode_dd = lv_dropdown_create(popup);
    lv_dropdown_set_options(ui->ctrl_mode_dd, "cool\nheat\ndry\nfan\nauto");
    lv_obj_set_size(ui->ctrl_mode_dd, compact ? 104 : 116, 32);
    lv_obj_align(ui->ctrl_mode_dd,
                 LV_ALIGN_TOP_LEFT,
                 compact ? 22 : 34,
                 compact ? 98 : 128);
    lv_obj_set_style_text_font(ui->ctrl_mode_dd, smart_home_lvgl_font(12), 0);
    lv_obj_add_event_cb(ui->ctrl_mode_dd,
                        ctrl_mode_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    ui->ctrl_fan_dd = lv_dropdown_create(popup);
    lv_dropdown_set_options(ui->ctrl_fan_dd, "low\nmedium\nhigh\nauto");
    lv_obj_set_size(ui->ctrl_fan_dd, compact ? 104 : 116, 32);
    lv_obj_align(ui->ctrl_fan_dd,
                 LV_ALIGN_TOP_RIGHT,
                 compact ? -22 : -34,
                 compact ? 98 : 128);
    lv_obj_set_style_text_font(ui->ctrl_fan_dd, smart_home_lvgl_font(12), 0);
    lv_obj_add_event_cb(ui->ctrl_fan_dd,
                        ctrl_fan_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    ui->ctrl_slider = lv_slider_create(popup);
    lv_obj_set_size(ui->ctrl_slider, lv_pct(72), 8);
    lv_obj_align(ui->ctrl_slider, LV_ALIGN_TOP_MID, 0, compact ? 144 : 180);
    lv_obj_add_event_cb(ui->ctrl_slider,
                        ctrl_slider_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    row = lv_obj_create(popup);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row,
                    smart_home_lvgl_content_w() - (compact ? 18 : 36),
                    compact ? 32 : 38);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, compact ? -8 : -16);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    btn = create_action_button(row,
                               "Delete",
                               SMART_HOME_UI_COLOR_DANGER,
                               lv_color_white());
    lv_obj_add_event_cb(btn, ctrl_delete_cb, LV_EVENT_CLICKED, ui);

    btn = create_action_button(row,
                               "Cancel",
                               SMART_HOME_UI_COLOR_BTN_SECONDARY,
                               SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    lv_obj_add_event_cb(btn, ctrl_cancel_cb, LV_EVENT_CLICKED, ui);

    btn = create_action_button(row,
                               "Edit",
                               SMART_HOME_UI_COLOR_SURFACE_SOFT,
                               SMART_HOME_UI_COLOR_TEXT_PRIMARY);
    lv_obj_add_event_cb(btn, ctrl_edit_cb, LV_EVENT_CLICKED, ui);

    btn = create_action_button(row,
                               "Apply",
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

    lv_label_set_text(ui->ctrl_title, device->name);
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
        lv_label_set_text(ui->ctrl_value_label, "No temp target");
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
        lv_dropdown_set_options(ui->device_room_dd, "Living Room\nBedroom");
        lv_obj_set_size(ui->device_room_dd, lv_pct(78), compact ? 32 : 34);
        lv_obj_align(ui->device_room_dd,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 82 : 102);
        lv_obj_set_style_text_font(ui->device_room_dd,
                                   smart_home_lvgl_font(12),
                                   0);

        ui->device_type_dd = lv_dropdown_create(popup);
        lv_dropdown_set_options(ui->device_type_dd, "Light\nAC");
        lv_obj_set_size(ui->device_type_dd, lv_pct(78), compact ? 32 : 34);
        lv_obj_align(ui->device_type_dd,
                     LV_ALIGN_TOP_MID,
                     0,
                     compact ? 126 : 152);
        lv_obj_set_style_text_font(ui->device_type_dd,
                                   smart_home_lvgl_font(12),
                                   0);

        btn = create_action_button(popup,
                                   "Cancel",
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
                                   "Apply",
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
        lv_obj_set_style_pad_all(ui->device_keyboard, 2, 0);
        lv_obj_add_event_cb(ui->device_keyboard,
                            device_keyboard_cb,
                            LV_EVENT_ALL,
                            ui);
        layout_device_keyboard(ui, 0);
    }

    ui->device_edit_id = is_edit ? device->id : 0;
    lv_label_set_text(ui->device_popup_title,
                      is_edit ? "Edit Device" : "Add Device");
    lv_textarea_set_text(ui->device_name_input,
                         is_edit ? device->name : "");
    lv_dropdown_set_selected(ui->device_room_dd,
                             is_edit ? smart_home_room_to_index(device->room) :
                                       0);
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

static lv_obj_t *create_device_card(lv_obj_t *grid,
                                    smart_home_lvgl_t *ui,
                                    int slot,
                                    const smart_home_device_t *device)
{
    int card_w = smart_home_lvgl_content_w() / 2 - 12;
    int card_h = smart_home_lvgl_compact() ? 82 : 98;
    lv_obj_t *card = lv_obj_create(grid);
    char text[128];

    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    lv_obj_set_user_data(card, (void *)(intptr_t)slot);
    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, ui);

    format_device_card_text(device, text, sizeof(text));
    set_card_content(card, device_icon(device), text, device && device->on);

    if (device && device->on) {
        smart_home_lvgl_set_bg(card, SMART_HOME_UI_COLOR_SURFACE_ON);
        lv_obj_set_style_border_color(card,
                                      SMART_HOME_UI_COLOR_DEVICE_ON_BORDER,
                                      0);
    }

    return card;
}

static lv_obj_t *create_add_card(lv_obj_t *grid, smart_home_lvgl_t *ui)
{
    int card_w = smart_home_lvgl_content_w() / 2 - 12;
    int card_h = smart_home_lvgl_compact() ? 82 : 98;
    lv_obj_t *card = lv_obj_create(grid);

    lv_obj_set_size(card, card_w, card_h);
    smart_home_lvgl_card_style(card);
    lv_obj_add_event_cb(card, add_device_cb, LV_EVENT_CLICKED, ui);
    set_card_content(card, ICON_ADD, "Add Device", 0);
    smart_home_lvgl_set_bg(card, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    return card;
}

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
    int card_w = smart_home_lvgl_content_w() / 2 - 12;
    int card_h = smart_home_lvgl_compact() ? 118 : 132;
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

static void rebuild_device_cards(smart_home_lvgl_t *ui)
{
    int slot;

    if (!ui || !ui->panel_grid || !ui->device_state) {
        return;
    }

    lv_obj_clean(ui->panel_grid);
    ui->panel_add_btn = create_add_card(ui->panel_grid, ui);
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

#ifdef CONFIG_SMART_HOME_NODE_GATEWAY
    append_online_remote_node_cards(ui);
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
    lv_obj_t *label;
    int content_w = smart_home_lvgl_content_w();
    int pad_x = smart_home_lvgl_pad_x();
    int compact = smart_home_lvgl_compact();
    int grid_y = compact ? 42 : 54;
    int sensor_h = compact ? 26 : 30;
    int sensor_bottom =
        SMART_HOME_NAV_H + SMART_HOME_NAV_BOTTOM_PAD + (compact ? 8 : 16);
    int grid_h = smart_home_lvgl_disp_h() -
                 grid_y -
                 sensor_bottom -
                 sensor_h -
                 (compact ? 6 : 12);

    if (grid_h < (compact ? 84 : 120)) {
        grid_h = compact ? 84 : 120;
    }

    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    smart_home_lvgl_set_bg(screen, SMART_HOME_UI_COLOR_BG);
    ui->screen_panel = screen;

    ui->panel_title = smart_home_lvgl_label_create(screen,
                                                   "Smart Home",
                                                   SMART_HOME_UI_COLOR_TEXT_PRIMARY,
                                                   16);
    lv_obj_align(ui->panel_title,
                 LV_ALIGN_TOP_LEFT,
                 pad_x,
                 compact ? 8 : 12);

    ui->panel_room_dd = lv_dropdown_create(screen);
    lv_dropdown_set_options(ui->panel_room_dd, "All\nLiving\nBedroom");
    lv_obj_set_size(ui->panel_room_dd, compact ? 88 : 100, compact ? 28 : 32);
    lv_obj_align(ui->panel_room_dd,
                 LV_ALIGN_TOP_RIGHT,
                 -pad_x,
                 compact ? 6 : 8);
    lv_obj_set_style_text_font(ui->panel_room_dd, smart_home_lvgl_font(12), 0);
    lv_obj_add_event_cb(ui->panel_room_dd,
                        panel_room_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    grid = lv_obj_create(screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, content_w, grid_h);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, grid_y);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, compact ? 8 : 10, 0);
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
