#pragma once

#include "../agent/smart_home_agent.h"

typedef struct smart_home_app_bridge smart_home_app_bridge_t;

int smart_home_app_bridge_start(smart_home_app_bridge_t **bridge_out,
                                smart_home_agent_app_t *app);
void smart_home_app_bridge_stop(smart_home_app_bridge_t **bridge_ptr);
