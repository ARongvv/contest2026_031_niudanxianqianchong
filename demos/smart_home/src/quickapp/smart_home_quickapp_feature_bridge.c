#include "smart_home_quickapp_feature_bridge.h"

#include <smart_home_quickapp_bridge.h>

#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_bridge_mutex = PTHREAD_MUTEX_INITIALIZER;
static smart_home_quickapp_provider_t *g_provider;

static int bridge_status_from_provider(int status)
{
    if (status == AGENT_ERROR_INVALID || status == AGENT_ERROR_NOTFOUND) {
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_INVALID;
    }

    if (status == SMART_HOME_DEVICE_SERVICE_ERROR_STALE_REVISION) {
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_STALE;
    }

    return SMART_HOME_QUICKAPP_BRIDGE_ERROR_UNAVAILABLE;
}

static void bridge_snapshot_from_provider(
    const smart_home_quickapp_snapshot_t *snapshot,
    struct smart_home_quickapp_bridge_snapshot_s *out_snapshot)
{
    out_snapshot->device_id = SMART_HOME_QUICKAPP_DEVICE_ID;
    out_snapshot->revision = snapshot->revision;
    out_snapshot->online = snapshot->online != 0;
    out_snapshot->power = snapshot->power != 0;
    out_snapshot->brightness = (uint8_t)snapshot->brightness;
}

void smart_home_quickapp_feature_bridge_register(
    smart_home_quickapp_provider_t *provider)
{
    pthread_mutex_lock(&g_bridge_mutex);
    g_provider = provider;
    pthread_mutex_unlock(&g_bridge_mutex);
}

void smart_home_quickapp_feature_bridge_unregister(
    smart_home_quickapp_provider_t *provider)
{
    pthread_mutex_lock(&g_bridge_mutex);
    if (g_provider == provider) {
        g_provider = NULL;
    }
    pthread_mutex_unlock(&g_bridge_mutex);
}

int smart_home_quickapp_bridge_get_capabilities(
    struct smart_home_quickapp_bridge_capability_s *out_capability)
{
    int ret = SMART_HOME_QUICKAPP_BRIDGE_OK;

    if (!out_capability) {
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_INVALID;
    }

    pthread_mutex_lock(&g_bridge_mutex);
    if (!g_provider) {
        ret = SMART_HOME_QUICKAPP_BRIDGE_ERROR_UNAVAILABLE;
    } else {
        out_capability->api_version = SMART_HOME_QUICKAPP_API_VERSION;
        out_capability->device_id = SMART_HOME_QUICKAPP_DEVICE_ID;
        out_capability->type = "light";
        out_capability->command = SMART_HOME_QUICKAPP_COMMAND_SET_POWER;
    }
    pthread_mutex_unlock(&g_bridge_mutex);
    return ret;
}

int smart_home_quickapp_bridge_get_snapshot(const char *device_id,
    struct smart_home_quickapp_bridge_snapshot_s *out_snapshot)
{
    smart_home_quickapp_snapshot_t snapshot;
    int ret;

    if (!device_id || !out_snapshot ||
        strcmp(device_id, SMART_HOME_QUICKAPP_DEVICE_ID) != 0) {
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_INVALID;
    }

    pthread_mutex_lock(&g_bridge_mutex);
    if (!g_provider) {
        pthread_mutex_unlock(&g_bridge_mutex);
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_UNAVAILABLE;
    }

    ret = smart_home_quickapp_provider_get_snapshot(g_provider, &snapshot);
    if (ret == AGENT_OK) {
        bridge_snapshot_from_provider(&snapshot, out_snapshot);
        ret = SMART_HOME_QUICKAPP_BRIDGE_OK;
    } else {
        ret = bridge_status_from_provider(ret);
    }
    pthread_mutex_unlock(&g_bridge_mutex);
    return ret;
}

int smart_home_quickapp_bridge_control(const char *request_id,
    const char *device_id, const char *command, bool value,
    uint32_t expected_revision,
    struct smart_home_quickapp_bridge_snapshot_s *out_snapshot)
{
    smart_home_quickapp_snapshot_t snapshot;
    int ret;

    if (!request_id || !device_id || !command || !out_snapshot) {
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_INVALID;
    }

    pthread_mutex_lock(&g_bridge_mutex);
    if (!g_provider) {
        pthread_mutex_unlock(&g_bridge_mutex);
        return SMART_HOME_QUICKAPP_BRIDGE_ERROR_UNAVAILABLE;
    }

    ret = smart_home_quickapp_provider_control(g_provider, request_id,
                                                device_id, command,
                                                value ? 1 : 0,
                                                expected_revision, &snapshot);
    if (ret == AGENT_OK) {
        bridge_snapshot_from_provider(&snapshot, out_snapshot);
        ret = SMART_HOME_QUICKAPP_BRIDGE_OK;
    } else {
        ret = bridge_status_from_provider(ret);
    }
    pthread_mutex_unlock(&g_bridge_mutex);
    return ret;
}
