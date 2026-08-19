#pragma once

#include "../agent/smart_home_agent.h"

#include <agent.h>

void smart_home_ui_event_cb(const agent_event_t *event, void *user_data);
int smart_home_ui_run(smart_home_agent_app_t *app);
int smart_home_ui_run_once(smart_home_agent_app_t *app, const char *input);
