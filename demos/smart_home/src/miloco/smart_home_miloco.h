/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Miloco 米家网关接入（路径 C：家庭服务器上的 Xiaomi Miloco 后端）。
 *
 * P4 不直接对接米家云，而是通过局域网明文 HTTP 调用 Miloco 的
 * /miot REST API。设备列表轮询与控制请求都由独立 worker 线程执行，
 * LVGL 与 Agent 只接触本头文件导出的快照与提交接口。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMART_HOME_MILOCO_HOST_SIZE 64
#define SMART_HOME_MILOCO_TOKEN_SIZE 64
#define SMART_HOME_MILOCO_MAX_DEVICES 16
#define SMART_HOME_MILOCO_POLL_INTERVAL_SEC 5

/* Miloco 默认监听端口（backend settings server.port）。 */
#define SMART_HOME_MILOCO_DEFAULT_PORT 1810

typedef enum {
    SMART_HOME_MILOCO_CATEGORY_UNKNOWN = 0,
    SMART_HOME_MILOCO_CATEGORY_LIGHT,
    SMART_HOME_MILOCO_CATEGORY_AC,
    SMART_HOME_MILOCO_CATEGORY_OUTLET,
    SMART_HOME_MILOCO_CATEGORY_CAMERA,
    SMART_HOME_MILOCO_CATEGORY_FAN,
    SMART_HOME_MILOCO_CATEGORY_OTHER,
} smart_home_miloco_category_t;

typedef struct {
    char did[24];
    char name[40];
    char room[24];
    smart_home_miloco_category_t category;
    bool online;
    bool controllable;
    bool power_on;
} smart_home_miloco_device_t;

typedef struct {
    char host[SMART_HOME_MILOCO_HOST_SIZE];
    uint16_t port;
    char token[SMART_HOME_MILOCO_TOKEN_SIZE];
} smart_home_miloco_config_t;

typedef struct smart_home_miloco smart_home_miloco_t;

/* 配置校验：host 非空、无控制字符且端口非零。 */
bool smart_home_miloco_config_valid(const smart_home_miloco_config_t *config);

/*
 * 启动网关服务：创建 worker 线程（PSRAM 栈）并立即开始轮询。
 * 网络未就绪时轮询失败并按周期重试，不需要显式重连。
 * *service 已存在时返回 AGENT_ERROR_INVALID，调用方应先 stop。
 */
int smart_home_miloco_start(smart_home_miloco_t **service,
                            const smart_home_miloco_config_t *config);

/* 原子切换网关目标配置：不销毁 worker 线程（无 join 阻塞），LVGL
 * 线程可安全调用；worker 在下一循环边界应用并立即轮询。 */
int smart_home_miloco_reconfigure(smart_home_miloco_t *service,
                                  const smart_home_miloco_config_t *config);

/* 提交保存请求：secrets.json 的文件写入由 worker 线程执行（LVGL/
 * 主线程上的 LittleFS 写入会挂死系统，见开发日志），worker 完成后
 * 应用配置并立即轮询。返回 AGENT_OK 仅表示请求已入队。 */
int smart_home_miloco_request_save(smart_home_miloco_t *service,
                                   const smart_home_miloco_config_t *config);

/* 停止 worker、等待退出并释放全部资源。容忍 NULL 与重复调用。 */
void smart_home_miloco_stop(smart_home_miloco_t **service);

/*
 * 拷贝当前设备快照（含在线态与电源态）。revision_out 非空时输出
 * 快照版本号：只有版本变化才需要刷新 UI。返回设备数量。
 */
size_t smart_home_miloco_list(const smart_home_miloco_t *service,
                              smart_home_miloco_device_t *devices,
                              size_t capacity,
                              uint32_t *revision_out);

/* 网关最近一次轮询是否成功；未启动返回 false。 */
bool smart_home_miloco_reachable(const smart_home_miloco_t *service);

/* 小米账号是否已在网关侧完成绑定（GET /api/miot/status 的 is_bound）。
 * 网关不可达或未启动时返回 false。 */
bool smart_home_miloco_bound(const smart_home_miloco_t *service);

/*
 * 提交一次电源控制（异步）：worker 醒来后 POST set_property。
 * 队列已满时返回 AGENT_ERROR_LIMIT；参数非法返回 AGENT_ERROR_INVALID。
 */
int smart_home_miloco_submit_power(smart_home_miloco_t *service,
                                   const char *did,
                                   bool on);

#ifdef __cplusplus
}
#endif
