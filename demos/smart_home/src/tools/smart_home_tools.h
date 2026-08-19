#pragma once

#include "../device/smart_home_device_service.h"

#include <agent.h>

int smart_home_tools_register(agent_t *agent,
                              smart_home_device_service_t *device_service);
