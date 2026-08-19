/**
 * smart_home LVGL UI — main header.
 *
 * Provides types and public API for the multi-screen LVGL smart home UI.
 * LVGL is owned exclusively by the UI thread. Agent workers enqueue plain C
 * payloads; the UI loop drains them before updating LVGL widgets.
 *
 * Screens: Panel (device management) / Chat (bubbles + tool cards) / Settings
 *
 * Usage:
 *   smart_home_lvgl_t *ui = smart_home_lvgl_init(&app);
 *   agent_set_event_callback(app->agent, smart_home_lvgl_event_cb, ui);
 *   smart_home_lvgl_show(ui);   // load panel screen
 *   smart_home_lvgl_deinit(ui); // cleanup
 */

#pragma once

#include <agent.h>
#include <lvgl/lvgl.h>
#include <pthread.h>
#include <stdint.h>

#include "../../agent/smart_home_agent.h"
#include "../../device/smart_home_device.h"
#include "smart_home_lvgl_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── UI state type ──────────────────────────────────────────── */

#define SMART_HOME_LVGL_TRACE_TOOL_MAX 8

typedef struct {
    lv_obj_t *row;
    lv_obj_t *status_label;
    char id[64];
    char name[64];
    int state; /* -1 running, 0 failed, 1 ok */
} smart_home_lvgl_trace_tool_t;

typedef struct {
    /* Screens */
    lv_obj_t *screen_panel;
    lv_obj_t *screen_chat;
    lv_obj_t *screen_settings;

    /* Shared nav bar (recreated per screen) */
    int      active_tab;         /* 0=panel, 1=chat, 2=settings */

    /* Panel: device cards */
    lv_obj_t *panel_grid;
    lv_obj_t *panel_room_dd;    /* room filter dropdown */
    lv_obj_t *panel_sensor_bar;
    lv_obj_t *panel_title;
    lv_obj_t *panel_add_btn;
    lv_timer_t *remote_node_timer;
    uint32_t remote_node_revision;

    /* Panel: card widgets (indexed by device slot) */
    lv_obj_t *device_cards[SMART_HOME_MAX_DEVICES];
    lv_obj_t *env_temp_label;
    lv_obj_t *env_hum_label;
    lv_obj_t *env_light_label;
    lv_obj_t *env_ac_label;

    /* Device control popup (overlay on panel) */
    lv_obj_t *ctrl_popup;
    lv_obj_t *ctrl_slider;
    lv_obj_t *ctrl_switch;
    lv_obj_t *ctrl_mode_dd;
    lv_obj_t *ctrl_fan_dd;
    lv_obj_t *ctrl_title;
    lv_obj_t *ctrl_value_label;
    int       ctrl_device_id;
    int       ctrl_pending_on;
    int       ctrl_pending_val;
    int       ctrl_pending_mode;
    int       ctrl_pending_fan_speed;

    /* Device add/edit popup */
    lv_obj_t *device_popup;
    lv_obj_t *device_popup_title;
    lv_obj_t *device_name_input;
    lv_obj_t *device_room_dd;
    lv_obj_t *device_type_dd;
    lv_obj_t *device_keyboard;
    int       device_edit_id; /* 0 = add */

    /* Environment simulation popup (sensor values, UI/device owned) */
    lv_obj_t *env_popup;
    lv_obj_t *env_slider;
    lv_obj_t *env_title;
    lv_obj_t *env_value_label;
    int       env_pending_type; /* 0=temp, 1=humidity, 2=ambient light */
    int       env_pending_value;

    /* Chat: message list + status hint */
    lv_obj_t *chat_list;
    lv_obj_t *chat_status;
    lv_obj_t *chat_input_bar;
    lv_obj_t *chat_input;
    lv_obj_t *chat_send_btn;
    lv_obj_t *chat_keyboard;
    lv_obj_t *chat_trace_row;
    lv_obj_t *chat_trace_body;
    lv_obj_t *chat_trace_title;
    lv_obj_t *chat_trace_status;
    smart_home_lvgl_trace_tool_t
              chat_trace_tools[SMART_HOME_LVGL_TRACE_TOOL_MAX];
    int       chat_trace_collapsed;
    int       chat_trace_active;
    int       chat_trace_tool_count;
    int       chat_msg_count;

    /* Settings screen widgets */
    lv_obj_t *settings_page;
    lv_obj_t *settings_keyboard;
    lv_obj_t *settings_model_api_body;
    lv_obj_t *settings_model_api_toggle;
    lv_obj_t *settings_tool_directory_body;
    lv_obj_t *settings_tool_directory_toggle;
    lv_obj_t *settings_system_status_body;
    lv_obj_t *settings_system_status_toggle;
    lv_obj_t *settings_backend_dd;
    lv_obj_t *settings_host_input;
    lv_obj_t *settings_path_input;
    lv_obj_t *settings_port_input;
    lv_obj_t *settings_model_input;
    lv_obj_t *settings_api_key_input;
    lv_obj_t *settings_timeout_input;
    lv_obj_t *settings_max_tokens_input;
    lv_obj_t *settings_status_label;
    lv_obj_t *settings_mcp_discover_btn;
    /* Device state (shared, NOT owned) */
    smart_home_state_t *device_state;

    /* Shared application object (agent/tools/device, NOT owned) */
    smart_home_agent_app_t *app;

    /* Pending worker-to-UI messages.  The queue itself contains no LVGL
     * objects and is protected by pending_mutex. */
    pthread_mutex_t pending_mutex;
    void *pending_head;
    void *pending_tail;
    int pending_tool_count;
    int request_inflight;

    /* One reusable PSRAM stack backs the joinable Chat Agent worker. */
    pthread_t agent_worker;
    void *agent_worker_stack_alloc;
    void *agent_worker_stack;
    int agent_worker_active;
} smart_home_lvgl_t;

/* ── Public API ─────────────────────────────────────────────── */

/** Allocate and initialise the UI state. app is shared and not owned. */
smart_home_lvgl_t *smart_home_lvgl_init(smart_home_agent_app_t *app);

/** Bind a new app object after init, if needed. */
void smart_home_lvgl_set_app(smart_home_lvgl_t *ui, smart_home_agent_app_t *app);

/** Load the panel screen (typically called once after init). */
void smart_home_lvgl_show(smart_home_lvgl_t *ui);

/** Tear down all screens and free state. */
void smart_home_lvgl_deinit(smart_home_lvgl_t *ui);

/** Refresh device cards from current device_state. UI owner thread only. */
void smart_home_lvgl_refresh_cards(smart_home_lvgl_t *ui);

/** cAGENT event callback — queues an update for the LVGL thread. */
void smart_home_lvgl_event_cb(const agent_event_t *event, void *user_data);

/** Apply queued worker updates.  Must be called by the LVGL owner thread. */
void smart_home_lvgl_process_pending(smart_home_lvgl_t *ui);

/** Discard queued worker updates during UI teardown. */
void smart_home_lvgl_discard_pending(smart_home_lvgl_t *ui);

/** Run LVGL backend: init LVGL, show screens, enter lv_timer_handler loop. */
int smart_home_lvgl_run(smart_home_agent_app_t *app, const char *initial_input);

#ifdef __cplusplus
}
#endif
