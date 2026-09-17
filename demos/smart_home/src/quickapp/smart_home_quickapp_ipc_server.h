#pragma once

#include "smart_home_quickapp_provider.h"

#include <mqueue.h>
#include <pthread.h>

typedef struct {
    smart_home_quickapp_provider_t *provider;
    mqd_t request_queue;
    pthread_t worker;
    int running;
    int worker_started;
} smart_home_quickapp_ipc_server_t;

int smart_home_quickapp_ipc_server_init(
    smart_home_quickapp_ipc_server_t *server,
    smart_home_quickapp_provider_t *provider);
void smart_home_quickapp_ipc_server_deinit(
    smart_home_quickapp_ipc_server_t *server);
