/* SPDX-License-Identifier: Apache-2.0 */
/*
 * 米家扫码绑定页：显示网关绑定页地址的二维码，手机扫码完成小米账号
 * 授权后，本页通过 miloco service 的 is_bound 快照在数秒内跟随跳转。
 *
 * 状态机（UI 线程只读快照，网络 IO 全部在 service worker）：
 *   WAITING  网关可达且未绑定 —— "等待手机完成授权…"
 *   OFFLINE  网关不可达       —— "网关离线，请检查服务器"
 *   BOUND    is_bound=true    —— "已绑定 ✓ N 台设备"，2 秒后进设备页
 */

#include "smart_home_lvgl_internal.h"
#ifdef CONFIG_SMART_HOME_MILOCO_BRIDGE

#include "images/smart_home_icons.h"
#include "../../config/smart_home_secrets.h"
#include "../../miloco/smart_home_miloco.h"
#include "../../miloco/smart_home_miloco_qr.h"

#include <stdio.h>

#define BIND_QR_TARGET_PX 264
#define BIND_JUMP_DELAY_TICKS 2

static uint8_t s_qr_matrix[SMART_HOME_MILOCO_QR_MAX_MODULES *
                            SMART_HOME_MILOCO_QR_MAX_MODULES];

/* QR 渲染：白底卡片上按模块画黑块。一次性构建，不参与后续刷新。 */
static void bind_render_qr(lv_obj_t *parent, smart_home_lvgl_t *ui)
{
    smart_home_miloco_config_t qr_config;
    char url[96];
    char host[64];
    char token[64];
    uint16_t port = SMART_HOME_MILOCO_DEFAULT_PORT;
    int modules = 0;
    int scale;
    int x;
    int y;

    host[0] = '\0';
    token[0] = '\0';
    /* 运行态配置优先（挥发型模式下 secrets 为空）；回落 secrets。 */
    if (ui && ui->app && ui->app->miloco &&
        smart_home_miloco_get_config(ui->app->miloco, &qr_config)) {
        snprintf(host, sizeof(host), "%s", qr_config.host);
        port = qr_config.port;
    } else if (smart_home_secrets_get_miloco(host, sizeof(host), &port,
                                             token, sizeof(token))
                   != AGENT_OK ||
               !host[0]) {
        lv_obj_t *label = smart_home_lvgl_label_create(
            parent, "请先在米家网关设置中\n配置服务器地址",
            SMART_HOME_UI_COLOR_WARNING, 14);
        lv_obj_center(label);
        return;
    }
    snprintf(url, sizeof(url), "http://%s:%u/", host, (unsigned)port);

    if (smart_home_miloco_qr_encode(url, s_qr_matrix, &modules) != 0) {
        lv_obj_t *label = smart_home_lvgl_label_create(
            parent, "二维码生成失败", SMART_HOME_UI_COLOR_DANGER, 14);
        lv_obj_center(label);
        return;
    }

    scale = BIND_QR_TARGET_PX / modules;
    if (scale < 2) {
        scale = 2;
    }
    for (y = 0; y < modules; y++) {
        for (x = 0; x < modules; x++) {
            lv_obj_t *dot;
            int px;
            int py;

            if (!s_qr_matrix[y * SMART_HOME_MILOCO_QR_MAX_MODULES + x]) {
                continue;
            }
            px = (BIND_QR_TARGET_PX - modules * scale) / 2 + x * scale;
            py = (BIND_QR_TARGET_PX - modules * scale) / 2 + y * scale;
            dot = lv_obj_create(parent);
            lv_obj_remove_style_all(dot);
            lv_obj_set_pos(dot, px, py);
            lv_obj_set_size(dot, scale, scale);
            lv_obj_set_style_bg_color(dot, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

static void bind_set_status(smart_home_lvgl_t *ui, const char *text,
                            lv_color_t color)
{
    if (ui->bind_status_label) {
        lv_label_set_text(ui->bind_status_label, text);
        lv_obj_set_style_text_color(ui->bind_status_label, color, 0);
    }
}

static void bind_timer_cb(lv_timer_t *timer)
{
    smart_home_lvgl_t *ui = lv_timer_get_user_data(timer);
    static int jump_countdown;
    char text[64];

    if (!ui || !ui->app || !ui->app->miloco) {
        return;
    }
    /* 只在绑定页激活时工作；离开页面后空转，开销可忽略。
     * 例外：米家设置页借用本定时器每秒刷新状态行（同文本时 LVGL
     * 跳过重绘，无闪烁），弥补该页缺少周期刷新的缺陷。 */
    if (lv_scr_act() != ui->screen_miloco_bind) {
        jump_countdown = 0;
        if (ui->screen_miloco && lv_scr_act() == ui->screen_miloco) {
            smart_home_lvgl_refresh_miloco_screen(ui);
        }
        return;
    }

    if (smart_home_miloco_bound(ui->app->miloco)) {
        if (jump_countdown == 0) {
            size_t count = smart_home_miloco_list(ui->app->miloco, NULL, 0,
                                                  NULL);
            snprintf(text, sizeof(text), "✓ 已绑定 · %u 台设备，正在进入…",
                     (unsigned)count);
            bind_set_status(ui, text, SMART_HOME_UI_COLOR_SUCCESS);
            jump_countdown = BIND_JUMP_DELAY_TICKS;
        } else if (--jump_countdown == 0) {
            smart_home_lvgl_load_tab(ui, SMART_HOME_TAB_DEVICES);
        }
        return;
    }
    jump_countdown = 0;
    if (!smart_home_miloco_reachable(ui->app->miloco)) {
        bind_set_status(ui, "网关离线，请检查 Miloco 服务器",
                        SMART_HOME_UI_COLOR_WARNING);
    } else {
        bind_set_status(ui, "等待手机完成授权…",
                        SMART_HOME_UI_COLOR_TEXT_SECONDARY);
    }
}

void smart_home_lvgl_build_miloco_bind_screen(smart_home_lvgl_t *ui)
{
    lv_obj_t *screen;
    lv_obj_t *card;
    lv_obj_t *label;
    int x = smart_home_lvgl_pad_x();
    int y = SMART_HOME_TOPBAR_H + 78;

    if (!ui) {
        return;
    }
    screen = page_screen(ui);
    ui->screen_miloco_bind = screen;
    page_heading(screen, "连接米家");

    /* 左侧：QR 卡（白底保证 quiet zone）。 */
    card = page_card(screen, x, y, BIND_QR_TARGET_PX + 56,
                     BIND_QR_TARGET_PX + 56);
    smart_home_lvgl_set_bg(card, lv_color_white());
    bind_render_qr(card, ui);

    /* 右侧：步骤说明与状态。 */
    card = page_card(screen, x + BIND_QR_TARGET_PX + 70, y,
                     smart_home_lvgl_content_w() - BIND_QR_TARGET_PX - 70,
                     BIND_QR_TARGET_PX + 56);
    page_icon_badge(card, ICON_MIJIA, lv_color_hex(0xFFF8F4));
    label = smart_home_lvgl_label_create(card, "扫码绑定米家账号",
                                         SMART_HOME_UI_COLOR_TEXT_PRIMARY, 18);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 56, 4);
    label = smart_home_lvgl_label_create(
        card,
        "1. 用手机扫描左侧二维码\n"
        "2. 在手机上完成小米账号授权\n"
        "3. 复制授权码，回到手机页面粘贴提交\n"
        "4. 此屏幕将自动进入设备列表",
        SMART_HOME_UI_COLOR_TEXT_SECONDARY, 14);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 52);
    ui->bind_status_label = smart_home_lvgl_label_create(
        card, "等待手机完成授权…",
        SMART_HOME_UI_COLOR_TEXT_SECONDARY, 14);
    lv_obj_align(ui->bind_status_label, LV_ALIGN_BOTTOM_LEFT, 0, -8);

    ui->bind_timer = lv_timer_create(bind_timer_cb, 1000, ui);
    smart_home_lvgl_build_nav_bar(screen, ui);
}

#endif /* CONFIG_SMART_HOME_MILOCO_BRIDGE */
