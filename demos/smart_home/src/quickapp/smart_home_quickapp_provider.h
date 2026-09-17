#pragma once

#include "smart_home_device_service.h"

#include <pthread.h>
#include <stdint.h>

#define SMART_HOME_QUICKAPP_API_VERSION 1u
#define SMART_HOME_QUICKAPP_DEVICE_ID "sim-living-room-light"
#define SMART_HOME_QUICKAPP_COMMAND_SET_POWER "setPower"
#define SMART_HOME_QUICKAPP_DEFAULT_BRIGHTNESS 50
#define SMART_HOME_QUICKAPP_REQUEST_ID_SIZE 48u
#define SMART_HOME_QUICKAPP_REQUEST_CACHE_SIZE 8u

typedef struct {
    uint32_t revision;
    int online;
    int power;
    int brightness;
} smart_home_quickapp_snapshot_t;

typedef struct {
    char request_id[SMART_HOME_QUICKAPP_REQUEST_ID_SIZE];
    int pending;
    int result;
    smart_home_quickapp_snapshot_t snapshot;
} smart_home_quickapp_request_cache_entry_t;

typedef struct {
    smart_home_device_service_t *device_service;
    pthread_mutex_t mutex;
    int mutex_initialized;
    size_t next_request_slot;
    smart_home_quickapp_request_cache_entry_t
        requests[SMART_HOME_QUICKAPP_REQUEST_CACHE_SIZE];
} smart_home_quickapp_provider_t;

int smart_home_quickapp_provider_init(
    smart_home_quickapp_provider_t *provider,
    smart_home_device_service_t *device_service);
void smart_home_quickapp_provider_deinit(smart_home_quickapp_provider_t *provider);

int smart_home_quickapp_provider_get_snapshot(
    smart_home_quickapp_provider_t *provider,
    smart_home_quickapp_snapshot_t *out_snapshot);

int smart_home_quickapp_provider_control(
    smart_home_quickapp_provider_t *provider,
    const char *request_id,
    const char *device_id,
    const char *command,
    int value,
    uint32_t expected_revision,
    smart_home_quickapp_snapshot_t *out_snapshot);
