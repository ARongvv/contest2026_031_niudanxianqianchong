#pragma once

#include <stddef.h>

typedef struct smart_home_agent_app smart_home_agent_app_t;
typedef struct smart_home_agent_run_service smart_home_agent_run_service_t;

typedef void (*smart_home_agent_run_listener_t)(const char *type,
                                                const char *run_id,
                                                const char *data_json,
                                                void *user_data);

int smart_home_agent_run_service_start(smart_home_agent_run_service_t **out,
                                       smart_home_agent_app_t *app);
void smart_home_agent_run_service_stop(smart_home_agent_run_service_t **service);
void smart_home_agent_run_service_set_listener(smart_home_agent_run_service_t *service,
                                               smart_home_agent_run_listener_t listener,
                                               void *user_data);
int smart_home_agent_run_service_submit(smart_home_agent_run_service_t *service,
                                        const char *conversation_id,
                                        const char *message,
                                        char *run_id,
                                        size_t run_id_size);
int smart_home_agent_run_service_get(smart_home_agent_run_service_t *service,
                                     const char *run_id, char *buffer,
                                     size_t buffer_size);
