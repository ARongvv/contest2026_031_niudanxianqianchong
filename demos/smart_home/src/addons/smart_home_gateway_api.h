#pragma once

#include "../agent/smart_home_agent.h"

#define SMART_HOME_GATEWAY_RESPONSE_SIZE 16384

typedef struct {
    const char *method;
    const char *path;
    const char *body;
    const char *request_id;
} smart_home_gateway_request_t;

typedef struct {
    int status_code;
    char body[SMART_HOME_GATEWAY_RESPONSE_SIZE];
} smart_home_gateway_response_t;

int smart_home_gateway_api_handle(smart_home_agent_app_t *app,
                                  const smart_home_gateway_request_t *request,
                                  smart_home_gateway_response_t *response);
