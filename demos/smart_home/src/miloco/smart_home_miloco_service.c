/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Miloco 网关服务：独立 worker 轮询设备列表并执行电源控制。
 *
 * 数据流：
 *   GET /miot/device_list           → 设备 did/name/room/online（小 JSON）
 *   GET /miot/devices/{did}/spec    → 新设备一次性取 category（小 JSON）
 *   GET /miot/devices/{did}/status?iid=prop.2.1 → 电源态回读
 *   POST /miot/devices/{did}/control → set_property 开关
 *
 * 响应统一为 {code, message, data}；code==0 才视为成功。
 */

#include "smart_home_miloco.h"
#include "smart_home_miloco_client.h"

#include "../smart_home_memory.h"
#include "../config/cjson_compat.h"
#include "../config/smart_home_secrets.h"

#include <cagent/types.h>

#include <errno.h>
#include <nuttx/irq.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* 24 KiB：HTTP 路径（getaddrinfo + 非阻塞 connect + 收包解析）实测
 * 栈深，8 KiB 时溢出会踩坏相邻 PSRAM，症状延迟到后续堆操作（cJSON）
 * 才爆——分段日志定位的教训，见开发日志。 */
#define MILOCO_WORKER_STACK_SIZE   24576u
#define MILOCO_CONTROL_QUEUE_DEPTH 4
#define MILOCO_RESPONSE_BYTES      (16u * 1024u)
#define MILOCO_STATUS_RESPONSE_BYTES 512u

typedef struct {
    char did[sizeof(((smart_home_miloco_device_t *)0)->did)];
    bool on;
} miloco_control_request_t;

struct smart_home_miloco {
    smart_home_miloco_client_config_t client_config;
    pthread_t worker;
    bool worker_started;
    bool stop_requested;
    /* worker 栈（PSRAM）：保存指针以便 stop 时释放，避免泄漏。 */
    void *worker_stack;
    size_t worker_stack_bytes;
    /* reconfigure 待生效配置：worker 在循环顶部安全切换，UI 线程
     * 永不 join、永不重建线程。 */
    smart_home_miloco_client_config_t pending_config;
    bool config_dirty;
    /* 保存请求：worker 代写 secrets.json 后应用配置。 */
    smart_home_miloco_config_t save_config;
    bool save_pending;

    pthread_mutex_t lock;
    sem_t wake;

    /* 以下字段由 lock 保护。 */
    miloco_control_request_t pending[MILOCO_CONTROL_QUEUE_DEPTH];
    size_t pending_count;
    smart_home_miloco_device_t devices[SMART_HOME_MILOCO_MAX_DEVICES];
    size_t device_count;
    uint32_t revision;
    bool reachable;
    bool xiaomi_bound;
};

static void lock_state(smart_home_miloco_t *service)
{
    pthread_mutex_lock(&service->lock);
}

static void unlock_state(smart_home_miloco_t *service)
{
    pthread_mutex_unlock(&service->lock);
}

bool smart_home_miloco_config_valid(const smart_home_miloco_config_t *config)
{
    const unsigned char *p;

    if (!config || config->host[0] == '\0' || config->port == 0) {
        return false;
    }
    for (p = (const unsigned char *)config->host; *p; p++) {
        if (*p < 0x20u || *p == 0x7fu) {
            return false;
        }
    }
    return true;
}

size_t smart_home_miloco_list(const smart_home_miloco_t *service,
                              smart_home_miloco_device_t *devices,
                              size_t capacity,
                              uint32_t *revision_out)
{
    size_t count;

    if (!service) {
        if (revision_out) {
            *revision_out = 0;
        }
        return 0;
    }
    lock_state((smart_home_miloco_t *)service);
    count = service->device_count;
    if (devices && capacity > 0) {
        if (count > capacity) {
            count = capacity;
        }
        memcpy(devices, service->devices,
               count * sizeof(devices[0]));
    }
    if (revision_out) {
        *revision_out = service->revision;
    }
    unlock_state((smart_home_miloco_t *)service);
    return count;
}

bool smart_home_miloco_bound(const smart_home_miloco_t *service)
{
    bool bound = false;

    if (service) {
        lock_state((smart_home_miloco_t *)service);
        bound = service->reachable && service->xiaomi_bound;
        unlock_state((smart_home_miloco_t *)service);
    }
    return bound;
}

bool smart_home_miloco_reachable(const smart_home_miloco_t *service)
{
    bool reachable = false;

    if (service) {
        lock_state((smart_home_miloco_t *)service);
        reachable = service->reachable;
        unlock_state((smart_home_miloco_t *)service);
    }
    return reachable;
}

int smart_home_miloco_submit_power(smart_home_miloco_t *service,
                                   const char *did,
                                   bool on)
{
    int ret = AGENT_OK;

    if (!service || !did || !did[0] || strlen(did) >=
        sizeof(service->pending[0].did)) {
        return AGENT_ERROR_INVALID;
    }
    lock_state(service);
    if (service->stop_requested) {
        ret = AGENT_ERROR_INVALID;
    } else if (service->pending_count >= MILOCO_CONTROL_QUEUE_DEPTH) {
        ret = AGENT_ERROR_LIMIT;
    } else {
        strcpy(service->pending[service->pending_count].did, did);
        service->pending[service->pending_count].on = on;
        service->pending_count++;
    }
    unlock_state(service);
    if (ret == AGENT_OK) {
        sem_post(&service->wake);
    }
    return ret;
}

/*
 * 原子切换网关目标配置：worker 在下一个循环边界应用并立即轮询。
 * 与 stop+start 的区别：不销毁线程（无 join 阻塞、无栈重分配），
 * LVGL 线程调用安全。
 */
int smart_home_miloco_reconfigure(smart_home_miloco_t *service,
                                  const smart_home_miloco_config_t *config)
{
    smart_home_miloco_client_config_t client_config;
    int ret = AGENT_OK;

    if (!service || !smart_home_miloco_config_valid(config)) {
        return AGENT_ERROR_INVALID;
    }
    snprintf(client_config.host, sizeof(client_config.host), "%s",
             config->host);
    client_config.port = config->port;
    snprintf(client_config.token, sizeof(client_config.token), "%s",
             config->token);

    lock_state(service);
    if (service->stop_requested) {
        ret = AGENT_ERROR_INVALID;
    } else {
        service->pending_config = client_config;
        service->config_dirty = true;
    }
    unlock_state(service);
    if (ret == AGENT_OK) {
        sem_post(&service->wake);
    }
    return ret;
}

int smart_home_miloco_request_save(smart_home_miloco_t *service,
                                   const smart_home_miloco_config_t *config)
{
    int ret = AGENT_OK;

    if (!service || !smart_home_miloco_config_valid(config)) {
        return AGENT_ERROR_INVALID;
    }
    lock_state(service);
    if (service->stop_requested) {
        ret = AGENT_ERROR_INVALID;
    } else {
        service->save_config = *config;
        service->save_pending = true;
    }
    unlock_state(service);
    if (ret == AGENT_OK) {
        sem_post(&service->wake);
    }
    return ret;
}

/* ── JSON 解析 ─────────────────────────────────────────────── */

static smart_home_miloco_category_t category_from_name(const char *name)
{
    if (!name) {
        return SMART_HOME_MILOCO_CATEGORY_UNKNOWN;
    }
    if (strcmp(name, "light") == 0) {
        return SMART_HOME_MILOCO_CATEGORY_LIGHT;
    }
    if (strcmp(name, "air-conditioner") == 0 ||
        strcmp(name, "air-conditioner-outdoor") == 0) {
        return SMART_HOME_MILOCO_CATEGORY_AC;
    }
    if (strcmp(name, "outlet") == 0 || strcmp(name, "plug") == 0 ||
        strcmp(name, "switch") == 0) {
        return SMART_HOME_MILOCO_CATEGORY_OUTLET;
    }
    return SMART_HOME_MILOCO_CATEGORY_OTHER;
}

static void copy_text(char *dst, size_t dst_size, const cJSON *value)
{
    const char *text = cJSON_IsString(value) ? value->valuestring : "";

    snprintf(dst, dst_size, "%s", text);
}

/* 解析 data: [{did,name,online,room_name,...}, ...]（device_list 响应）。 */
static int parse_device_list(smart_home_miloco_t *service, const char *body,
                             smart_home_miloco_device_t *fresh_categories,
                             size_t fresh_capacity,
                             size_t *fresh_count)
{
    cJSON *root = cJSON_Parse(body);
    cJSON *data;
    cJSON *item;
    smart_home_miloco_device_t parsed[SMART_HOME_MILOCO_MAX_DEVICES];
    size_t count = 0;

    *fresh_count = 0;
    if (!root) {
        return AGENT_ERROR_PARSE;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return AGENT_ERROR_PARSE;
    }

    cJSON_ArrayForEach(item, data) {
        const cJSON *did = cJSON_GetObjectItemCaseSensitive(item, "did");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        const cJSON *room = cJSON_GetObjectItemCaseSensitive(item, "room_name");
        smart_home_miloco_device_t *slot;

        if (!cJSON_IsString(did) || !did->valuestring[0] ||
            count >= SMART_HOME_MILOCO_MAX_DEVICES) {
            continue;
        }
        slot = &parsed[count++];
        memset(slot, 0, sizeof(*slot));
        copy_text(slot->did, sizeof(slot->did), did);
        copy_text(slot->name, sizeof(slot->name), name);
        copy_text(slot->room, sizeof(slot->room), room);
        slot->online = cJSON_IsTrue(online);
    }
    cJSON_Delete(root);

    /* 保留旧条目的 category/controllable/power_on；新 did 记入 fresh 列表
     * 供 worker 后续拉取 spec。 */
    {
        size_t i;
        size_t j;

        for (i = 0; i < count; i++) {
            const smart_home_miloco_device_t *old = NULL;

            for (j = 0; j < service->device_count; j++) {
                if (strcmp(service->devices[j].did, parsed[i].did) == 0) {
                    old = &service->devices[j];
                    break;
                }
            }
            if (old) {
                parsed[i].category = old->category;
                parsed[i].controllable = old->controllable;
                parsed[i].power_on = old->power_on;
            } else if (fresh_categories && *fresh_count < fresh_capacity) {
                strcpy(fresh_categories[*fresh_count].did, parsed[i].did);
                (*fresh_count)++;
            }
        }
    }

    lock_state(service);
    memcpy(service->devices, parsed, count * sizeof(parsed[0]));
    service->device_count = count;
    service->reachable = true;
    service->revision++;
    unlock_state(service);
    return AGENT_OK;
}

/* 解析 data: {did, name, category, spec,...}（单设备 spec 响应）。 */
static int parse_device_spec(smart_home_miloco_t *service, const char *body)
{
    cJSON *root = cJSON_Parse(body);
    cJSON *data;
    const cJSON *did;
    const cJSON *category;
    smart_home_miloco_category_t mapped;
    const char *category_text;
    size_t i;

    if (!root) {
        return AGENT_ERROR_PARSE;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    did = cJSON_IsObject(data) ?
        cJSON_GetObjectItemCaseSensitive(data, "did") : NULL;
    category = cJSON_IsObject(data) ?
        cJSON_GetObjectItemCaseSensitive(data, "category") : NULL;
    if (!cJSON_IsString(did) || !did->valuestring[0]) {
        cJSON_Delete(root);
        return AGENT_ERROR_PARSE;
    }
    category_text = cJSON_IsString(category) ? category->valuestring : NULL;
    mapped = category_from_name(category_text);
    cJSON_Delete(root);

    lock_state(service);
    for (i = 0; i < service->device_count; i++) {
        if (strcmp(service->devices[i].did, did->valuestring) == 0) {
            service->devices[i].category = mapped;
            /* V1 控制范围：灯/空调/插座，电源走 miotspec 惯例 prop.2.1。 */
            service->devices[i].controllable =
                mapped == SMART_HOME_MILOCO_CATEGORY_LIGHT ||
                mapped == SMART_HOME_MILOCO_CATEGORY_AC ||
                mapped == SMART_HOME_MILOCO_CATEGORY_OUTLET;
            service->revision++;
            break;
        }
    }
    unlock_state(service);
    return AGENT_OK;
}

/* 解析 data: [{iid, value}, ...]（status 响应，仅查 prop.2.1）。 */
static int parse_power_status(smart_home_miloco_t *service,
                              const char *did_text,
                              const char *body)
{
    cJSON *root = cJSON_Parse(body);
    cJSON *data;
    cJSON *item;
    bool power_on = false;
    bool found = false;
    size_t i;

    if (!root) {
        return AGENT_ERROR_PARSE;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ArrayForEach(item, data) {
        const cJSON *iid = cJSON_GetObjectItemCaseSensitive(item, "iid");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "value");

        if (cJSON_IsString(iid) && strcmp(iid->valuestring, "prop.2.1") == 0) {
            power_on = cJSON_IsTrue(value);
            found = true;
        }
    }
    cJSON_Delete(root);
    if (!found) {
        return AGENT_ERROR_NOTFOUND;
    }

    lock_state(service);
    for (i = 0; i < service->device_count; i++) {
        if (strcmp(service->devices[i].did, did_text) == 0) {
            service->devices[i].power_on = power_on;
            service->revision++;
            break;
        }
    }
    unlock_state(service);
    return AGENT_OK;
}

/* ── worker ────────────────────────────────────────────────── */

static int fetch_spec_for(smart_home_miloco_t *service, const char *did,
                          char *body, size_t body_size)
{
    char path[80];
    int http_status = 0;
    int ret;

    snprintf(path, sizeof(path), "/api/miot/devices/%.23s/spec", did);
    ret = smart_home_miloco_http_get(&service->client_config, path,
                                     body, body_size, &http_status);
    if (ret < 0) {
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    return parse_device_spec(service, body);
}

static int refresh_power_status(smart_home_miloco_t *service, const char *did,
                                char *body, size_t body_size)
{
    char path[96];
    int http_status = 0;
    int ret;

    snprintf(path, sizeof(path), "/api/miot/devices/%.23s/status?iid=prop.2.1", did);
    ret = smart_home_miloco_http_get(&service->client_config, path,
                                     body, body_size, &http_status);
    if (ret < 0) {
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    return parse_power_status(service, did, body);
}

/* GET /api/miot/status 解析 data.is_bound。绑定状态翻转时递增 revision
 * 触发 UI 刷新；未绑定时设备列表清空。 */
static int poll_bind_status(smart_home_miloco_t *service,
                            char *body, size_t body_size)
{
    cJSON *root;
    cJSON *data;
    const cJSON *is_bound;
    bool bound = false;
    int http_status = 0;
    int ret;

    ret = smart_home_miloco_http_get(&service->client_config,
                                     "/api/miot/status", body, body_size,
                                     &http_status);
    if (ret < 0) {
        lock_state(service);
        if (service->reachable) {
            service->reachable = false;
            service->revision++;
        }
        unlock_state(service);
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    root = cJSON_Parse(body);
    if (!root) {
        return AGENT_ERROR_PARSE;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    is_bound = cJSON_IsObject(data)
        ? cJSON_GetObjectItemCaseSensitive(data, "is_bound") : NULL;
    bound = cJSON_IsTrue(is_bound);
    cJSON_Delete(root);

    lock_state(service);
    if (!service->reachable || service->xiaomi_bound != bound) {
        service->revision++;
    }
    service->reachable = true;
    service->xiaomi_bound = bound;
    if (!bound && service->device_count > 0) {
        service->device_count = 0;
        service->revision++;
    }
    unlock_state(service);
    return bound ? AGENT_OK : AGENT_ERROR_NOTFOUND;
}

static int poll_device_list(smart_home_miloco_t *service,
                            char *body, size_t body_size)
{
    smart_home_miloco_device_t fresh[SMART_HOME_MILOCO_MAX_DEVICES];
    size_t fresh_count = 0;
    int http_status = 0;
    int ret;
    size_t i;

    ret = smart_home_miloco_http_get(&service->client_config,
                                     "/api/miot/device_list", body, body_size,
                                     &http_status);
    if (ret < 0) {
        lock_state(service);
        if (service->reachable) {
            service->reachable = false;
            service->revision++;
        }
        unlock_state(service);
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    ret = parse_device_list(service, body, fresh,
                            sizeof(fresh) / sizeof(fresh[0]), &fresh_count);
    if (ret != AGENT_OK) {
        return ret;
    }

    /* 新设备补 spec 类别；可控设备回读电源态。复用响应缓冲。 */
    for (i = 0; i < fresh_count; i++) {
        fetch_spec_for(service, fresh[i].did, body, body_size);
    }
    {
        smart_home_miloco_device_t snapshot[SMART_HOME_MILOCO_MAX_DEVICES];
        size_t count;

        count = smart_home_miloco_list(service, snapshot,
                                       SMART_HOME_MILOCO_MAX_DEVICES, NULL);
        for (i = 0; i < count; i++) {
            if (snapshot[i].controllable && snapshot[i].online) {
                refresh_power_status(service, snapshot[i].did, body, body_size);
            }
        }
    }
    return AGENT_OK;
}

static int execute_control(smart_home_miloco_t *service,
                           const miloco_control_request_t *request,
                           char *body, size_t body_size)
{
    char path[80];
    char request_body[128];
    int http_status = 0;
    int ret;

    snprintf(path, sizeof(path), "/api/miot/devices/%.23s/control", request->did);
    snprintf(request_body, sizeof(request_body),
             "{\"type\":\"set_property\",\"iid\":\"prop.2.1\",\"value\":%s}",
             request->on ? "true" : "false");
    ret = smart_home_miloco_http_post(&service->client_config, path,
                                      request_body, body, body_size,
                                      &http_status);
    if (ret < 0) {
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    /* 控制成功后回读真实状态，失败也接受（下轮轮询会补）。 */
    (void)refresh_power_status(service, request->did, body, body_size);
    return AGENT_OK;
}

static void *miloco_worker(void *argument)
{
    smart_home_miloco_t *service = argument;
    char *body;
    struct timespec wait_until;
    bool poll_due = true;

    syslog(LOG_INFO, "[milo] worker: entry\n");
    body = smart_home_bulk_alloc(MILOCO_RESPONSE_BYTES);
    if (!body) {
        syslog(LOG_ERR, "ERROR: [miloco] response buffer alloc failed\n");
        return NULL;
    }

    while (1) {
        bool stop;
        bool has_control = false;
        miloco_control_request_t control;

        if (poll_due) {
            syslog(LOG_INFO, "[milo] worker: poll begin\n");
            if (poll_bind_status(service, body, MILOCO_RESPONSE_BYTES)
                    == AGENT_OK) {
                /* 已绑定才轮询设备；未绑定时绑定页只需要 is_bound。 */
                poll_device_list(service, body, MILOCO_RESPONSE_BYTES);
            }
            poll_due = false;
        }

        lock_state(service);
        stop = service->stop_requested;
        if (service->save_pending) {
            smart_home_miloco_config_t save = service->save_config;
            smart_home_miloco_client_config_t client_config;
            int save_ret;

            service->save_pending = false;
            snprintf(client_config.host, sizeof(client_config.host), "%s",
                     save.host);
            client_config.port = save.port;
            snprintf(client_config.token, sizeof(client_config.token), "%s",
                     save.token);
            unlock_state(service);
            syslog(LOG_INFO, "[milo] worker: saving secrets\n");
            save_ret = smart_home_secrets_set_miloco(save.host, save.port,
                                                     save.token);
            syslog(LOG_INFO, "[milo] worker: secrets save ret=%d\n",
                   save_ret);
            lock_state(service);
            if (save_ret == AGENT_OK) {
                service->client_config = client_config;
                service->reachable = false;
                service->revision++;
            }
            poll_due = true;
        }
        if (service->config_dirty) {
            service->client_config = service->pending_config;
            service->config_dirty = false;
            poll_due = true;
        }
        if (!stop && service->pending_count > 0) {
            control = service->pending[0];
            memmove(&service->pending[0], &service->pending[1],
                    (service->pending_count - 1) *
                        sizeof(service->pending[0]));
            service->pending_count--;
            has_control = true;
        }
        unlock_state(service);

        if (stop) {
            break;
        }
        if (has_control) {
            if (execute_control(service, &control, body,
                                MILOCO_RESPONSE_BYTES) != AGENT_OK) {
                syslog(LOG_WARNING,
                       "WARNING: [miloco] control did=%s on=%d failed\n",
                       control.did, control.on ? 1 : 0);
            }
            continue; /* 立即处理后续排队的控制请求。 */
        }

        if (clock_gettime(CLOCK_REALTIME, &wait_until) == 0) {
            wait_until.tv_sec += SMART_HOME_MILOCO_POLL_INTERVAL_SEC;
            if (sem_timedwait(&service->wake, &wait_until) == 0) {
                /* 被控制请求或停机唤醒；poll 不提前。 */
                continue;
            }
        } else {
            sleep(SMART_HOME_MILOCO_POLL_INTERVAL_SEC);
        }
        poll_due = true;
    }

    smart_home_bulk_free(body);
    return NULL;
}

int smart_home_miloco_start(smart_home_miloco_t **service_out,
                            const smart_home_miloco_config_t *config)
{
    smart_home_miloco_t *service;
    pthread_attr_t attr;
    void *stack;
    int ret;

    if (!service_out || !smart_home_miloco_config_valid(config)) {
        return AGENT_ERROR_INVALID;
    }
    if (*service_out) {
        return AGENT_ERROR_INVALID;
    }

    syslog(LOG_INFO, "[milo] start: enter host=%s\n", config->host);
    service = calloc(1, sizeof(*service));
    if (!service) {
        return AGENT_ERROR_NOMEM;
    }
    snprintf(service->client_config.host,
             sizeof(service->client_config.host), "%s", config->host);
    service->client_config.port = config->port;
    snprintf(service->client_config.token,
             sizeof(service->client_config.token), "%s", config->token);

    ret = pthread_mutex_init(&service->lock, NULL);
    if (ret != 0) {
        free(service);
        return AGENT_ERROR;
    }
    if (sem_init(&service->wake, 0, 0) != 0) {
        pthread_mutex_destroy(&service->lock);
        free(service);
        return AGENT_ERROR;
    }

    /* worker 栈走 PSRAM，避免占用默认 pthread 栈预算；pthread 栈有
     * 架构对齐要求（RISC-V 16B），PSRAM 分配不保证，必须向上对齐
     * （网络配网 worker 同款做法）。 */
    syslog(LOG_INFO, "[milo] start: mutex/sem ready\n");
    stack = smart_home_bulk_alloc(MILOCO_WORKER_STACK_SIZE +
                                  STACK_ALIGNMENT - 1u);
    if (!stack) {
        sem_destroy(&service->wake);
        pthread_mutex_destroy(&service->lock);
        free(service);
        return AGENT_ERROR_NOMEM;
    }
    service->worker_stack = (void *)STACK_ALIGN_UP((uintptr_t)stack);
    service->worker_stack_bytes = MILOCO_WORKER_STACK_SIZE;
    syslog(LOG_INFO, "[milo] start: stack=%p bytes=%u\n",
           service->worker_stack, (unsigned)service->worker_stack_bytes);
    ret = pthread_attr_init(&attr);
    if (ret == 0) {
        ret = pthread_attr_setstack(&attr, service->worker_stack,
                                    service->worker_stack_bytes);
    }
    syslog(LOG_INFO, "[milo] start: attr ret=%d, creating thread\n", ret);
    if (ret == 0) {
        ret = pthread_create(&service->worker, &attr, miloco_worker, service);
    }
    syslog(LOG_INFO, "[milo] start: pthread_create ret=%d\n", ret);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        smart_home_bulk_free(service->worker_stack);
        sem_destroy(&service->wake);
        pthread_mutex_destroy(&service->lock);
        free(service);
        return AGENT_ERROR;
    }
    service->worker_started = true;
    syslog(LOG_INFO,
           "INFO: [miloco] gateway worker started host=%s port=%u\n",
           config->host, (unsigned)config->port);
    *service_out = service;
    return AGENT_OK;
}

void smart_home_miloco_stop(smart_home_miloco_t **service_ptr)
{
    smart_home_miloco_t *service;

    if (!service_ptr || !*service_ptr) {
        return;
    }
    service = *service_ptr;
    *service_ptr = NULL;

    lock_state(service);
    service->stop_requested = true;
    unlock_state(service);
    sem_post(&service->wake);

    if (service->worker_started) {
        pthread_join(service->worker, NULL);
    }
    smart_home_bulk_free(service->worker_stack);
    sem_destroy(&service->wake);
    pthread_mutex_destroy(&service->lock);
    free(service);
    syslog(LOG_INFO, "INFO: [miloco] gateway worker stopped\n");
}
