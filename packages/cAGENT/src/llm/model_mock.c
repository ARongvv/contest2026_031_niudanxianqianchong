/* SPDX-License-Identifier: Apache-2.0 */
#include <cagent/model.h>

#include "../types_internal.h"

#include <stdlib.h>
#include <string.h>

struct agent_model_mock {
    agent_model_mock_step_t *steps;
    size_t step_count;
    size_t cursor;
    uint32_t call_count;
    int repeat_last;
    int cancel_requested;
    agent_tool_call_t current_call;
};

static const agent_model_mock_step_t default_steps[] = {
    {
        AGENT_MODEL_MOCK_TOOL_CALL,
        NULL,
        "mock_call_1",
        "demo_echo",
        "{\"value\":\"demo_tool_ok\"}",
    },
    {
        AGENT_MODEL_MOCK_FINAL,
        "mock final reply",
        NULL,
        NULL,
        NULL,
    },
};

static const agent_model_mock_step_t *select_step(agent_model_mock_t *mock)
{
    size_t index;

    if (!mock || mock->step_count == 0u) {
        return NULL;
    }

    if (mock->cursor < mock->step_count) {
        index = mock->cursor++;
        return &mock->steps[index];
    }

    if (mock->repeat_last) {
        return &mock->steps[mock->step_count - 1u];
    }

    return NULL;
}

static int mock_complete(void *provider,
                         agent_runtime_t *runtime,
                         const agent_model_request_t *request,
                         agent_model_response_t *response)
{
    agent_model_mock_t *mock = (agent_model_mock_t *)provider;
    const agent_model_mock_step_t *step;

    CAGENT_UNUSED(runtime);
    CAGENT_UNUSED(request);

    if (!mock || !response) {
        return AGENT_ERROR_INVALID;
    }

    memset(response, 0, sizeof(*response));
    mock->call_count++;

    if (mock->cancel_requested) {
        response->status = AGENT_ERROR_CANCELLED;
        return AGENT_ERROR_CANCELLED;
    }

    step = select_step(mock);
    if (!step) {
        response->content = "";
        response->status = AGENT_OK;
        return AGENT_OK;
    }

    if (step->type == AGENT_MODEL_MOCK_TOOL_CALL) {
        if (!step->tool_name) {
            response->status = AGENT_ERROR_INVALID;
            return AGENT_ERROR_INVALID;
        }

        memset(&mock->current_call, 0, sizeof(mock->current_call));
        mock->current_call.id = step->tool_call_id ? step->tool_call_id : "mock_call";
        mock->current_call.name = step->tool_name;
        mock->current_call.arguments_json = step->arguments_json ? step->arguments_json : "{}";

        response->tool_calls = &mock->current_call;
        response->tool_call_count = 1u;
        response->status = AGENT_OK;
        return AGENT_OK;
    }

    response->content = step->content ? step->content : "";
    response->status = AGENT_OK;
    return AGENT_OK;
}

static int mock_cancel(void *provider)
{
    agent_model_mock_t *mock = (agent_model_mock_t *)provider;

    if (!mock) {
        return AGENT_ERROR_INVALID;
    }

    mock->cancel_requested = 1;
    return AGENT_OK;
}

static void mock_destroy(void *provider)
{
    agent_model_mock_t *mock = (agent_model_mock_t *)provider;

    if (!mock) {
        return;
    }

    free(mock->steps);
    free(mock);
}

agent_model_t *agent_model_mock_create(const agent_model_mock_config_t *config,
                                       agent_model_mock_t **mock_out)
{
    static const agent_model_ops_t ops = {
        mock_complete,
        mock_cancel,
        mock_destroy,
    };
    const agent_model_mock_step_t *steps = default_steps;
    size_t step_count = sizeof(default_steps) / sizeof(default_steps[0]);
    agent_model_mock_t *mock;
    agent_model_t *model;

    if (config && config->steps && config->step_count > 0u) {
        steps = config->steps;
        step_count = config->step_count;
    }

    mock = (agent_model_mock_t *)malloc(sizeof(*mock));
    if (!mock) {
        return NULL;
    }
    memset(mock, 0, sizeof(*mock));

    mock->steps = (agent_model_mock_step_t *)malloc(step_count * sizeof(mock->steps[0]));
    if (!mock->steps) {
        free(mock);
        return NULL;
    }
    memcpy(mock->steps, steps, step_count * sizeof(mock->steps[0]));
    mock->step_count = step_count;
    mock->repeat_last = config ? config->repeat_last : 1;

    model = agent_model_create(&ops, mock);
    if (!model) {
        mock_destroy(mock);
        return NULL;
    }

    if (mock_out) {
        *mock_out = mock;
    }
    return model;
}

uint32_t agent_model_mock_call_count(const agent_model_mock_t *mock)
{
    return mock ? mock->call_count : 0u;
}
