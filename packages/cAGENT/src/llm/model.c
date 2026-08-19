/* SPDX-License-Identifier: Apache-2.0 */
#include "../core/agent_internal.h"

#include <stdlib.h>
#include <string.h>

agent_model_t *agent_model_create(const agent_model_ops_t *ops, void *provider)
{
    agent_model_t *model;

    if (!ops || !ops->complete) {
        return NULL;
    }

    model = (agent_model_t *)malloc(sizeof(*model));
    if (!model) {
        return NULL;
    }

    memset(model, 0, sizeof(*model));
    model->ops = *ops;
    model->provider = provider;

    return model;
}

void agent_model_destroy(agent_model_t *model)
{
    if (!model) {
        return;
    }

    if (model->ops.destroy) {
        model->ops.destroy(model->provider);
    }
    free(model);
}

int agent_model_complete(agent_model_t *model,
                         agent_runtime_t *runtime,
                         const agent_model_request_t *request,
                         agent_model_response_t *response)
{
    if (!model || !model->ops.complete || !request || !response) {
        return AGENT_ERROR_INVALID;
    }

    return model->ops.complete(model->provider, runtime, request, response);
}

int agent_model_cancel(agent_model_t *model)
{
    if (!model) {
        return AGENT_ERROR_INVALID;
    }
    if (!model->ops.cancel) {
        return AGENT_ERROR_NOTSUP;
    }

    return model->ops.cancel(model->provider);
}
