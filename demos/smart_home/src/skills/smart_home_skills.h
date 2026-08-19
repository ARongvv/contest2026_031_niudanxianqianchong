#pragma once

#include "smart_home_skill_loader.h"

#include <agent.h>

int smart_home_skills_register(agent_t *agent,
                               smart_home_skill_store_t *store);
