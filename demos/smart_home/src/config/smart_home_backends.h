#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../agent/smart_home_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SMART_HOME_LLM_FLAG_TOOL_CALLING (1u << 0)
#define SMART_HOME_LLM_FLAG_CHAT_ONLY    (1u << 1)
#define SMART_HOME_LLM_FLAG_CUSTOM       (1u << 2)

typedef struct {
    const char *id;
    const char *name;
    const char *host;
    const char *path;
    const char *port;
    const char *default_model;
    uint32_t default_timeout_ms;
    uint32_t flags;
} smart_home_llm_backend_preset_t;

size_t smart_home_llm_backend_count(void);
const smart_home_llm_backend_preset_t *
smart_home_llm_backend_get(size_t index);
const smart_home_llm_backend_preset_t *
smart_home_llm_backend_default(void);
int smart_home_llm_backend_index_by_id(const char *id);
int smart_home_llm_backend_index_for_config(
    const smart_home_model_config_t *config);

int smart_home_model_config_from_preset(
    smart_home_model_config_t *config,
    const smart_home_llm_backend_preset_t *preset,
    int keep_api_key);
int smart_home_model_config_validate(const smart_home_model_config_t *config,
                                     char *error,
                                     size_t error_size);

#ifdef __cplusplus
}
#endif
