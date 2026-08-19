/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cagent/model_openai.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef agent_model_openai_config_t agent_model_openai_compat_config_t;

agent_model_openai_compat_config_t agent_model_openai_compat_config_default(void);

agent_model_t *agent_model_openai_compat_create(
    const agent_model_openai_compat_config_t *config);

int agent_model_openai_compat_set_backend(agent_model_t *model,
                                          const char *host,
                                          const char *path,
                                          const char *port);
int agent_model_openai_compat_set_api_key(agent_model_t *model,
                                          const char *api_key);
int agent_model_openai_compat_set_model(agent_model_t *model,
                                        const char *model_name);

#ifdef __cplusplus
}
#endif
