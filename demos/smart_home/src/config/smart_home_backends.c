#include "smart_home_backends.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const smart_home_llm_backend_preset_t g_llm_presets[] = {
    {
        "deepseek",
        "DeepSeek",
        "api.deepseek.com",
        "/v1/chat/completions",
        "443",
        "deepseek-v4-flash",
        30000u,
        SMART_HOME_LLM_FLAG_TOOL_CALLING
    },
    {
        "mimo",
        "MiMo",
        "api.xiaomimimo.com",
        "/v1/chat/completions",
        "443",
        "mimo-v2.5",
        30000u,
        SMART_HOME_LLM_FLAG_TOOL_CALLING
    },
    {
        "qwen",
        "Qwen",
        "dashscope.aliyuncs.com",
        "/compatible-mode/v1/chat/completions",
        "443",
        "qwen-turbo",
        30000u,
        SMART_HOME_LLM_FLAG_TOOL_CALLING
    },
    {
        "openai",
        "OpenAI",
        "api.openai.com",
        "/v1/chat/completions",
        "443",
        "gpt-4o-mini",
        30000u,
        SMART_HOME_LLM_FLAG_TOOL_CALLING
    },
    {
        "custom",
        "Custom",
        "",
        "",
        "443",
        "",
        30000u,
        SMART_HOME_LLM_FLAG_CUSTOM
    },
};

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0u) {
        return;
    }

    dst[0] = '\0';
    if (!src) {
        return;
    }

    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static int contains_ctl_or_space(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!p) {
        return 1;
    }

    while (*p) {
        if (*p <= 0x20u || *p == 0x7fu) {
            return 1;
        }
        p++;
    }

    return 0;
}

static int contains_crlf(const char *text)
{
    if (!text) {
        return 1;
    }

    while (*text) {
        if (*text == '\r' || *text == '\n') {
            return 1;
        }
        text++;
    }

    return 0;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0u) {
        return;
    }

    snprintf(error, error_size, "%s", message ? message : "invalid config");
}

size_t smart_home_llm_backend_count(void)
{
    return sizeof(g_llm_presets) / sizeof(g_llm_presets[0]);
}

const smart_home_llm_backend_preset_t *
smart_home_llm_backend_get(size_t index)
{
    if (index >= smart_home_llm_backend_count()) {
        return NULL;
    }

    return &g_llm_presets[index];
}

const smart_home_llm_backend_preset_t *
smart_home_llm_backend_default(void)
{
    return &g_llm_presets[0];
}

int smart_home_llm_backend_index_by_id(const char *id)
{
    size_t i;

    if (!id || !id[0]) {
        return -1;
    }

    for (i = 0u; i < smart_home_llm_backend_count(); i++) {
        if (strcmp(g_llm_presets[i].id, id) == 0) {
            return (int)i;
        }
    }

    return -1;
}

int smart_home_llm_backend_index_for_config(
    const smart_home_model_config_t *config)
{
    size_t i;
    int index;

    if (!config) {
        return 0;
    }

    index = smart_home_llm_backend_index_by_id(config->backend_id);
    if (index >= 0 &&
        (size_t)index < smart_home_llm_backend_count()) {
        return index;
    }

    for (i = 0u; i < smart_home_llm_backend_count(); i++) {
        const smart_home_llm_backend_preset_t *preset = &g_llm_presets[i];
        if ((preset->flags & SMART_HOME_LLM_FLAG_CUSTOM) != 0u) {
            continue;
        }

        if (strcmp(config->host, preset->host) == 0 &&
            strcmp(config->path, preset->path) == 0 &&
            strcmp(config->port, preset->port) == 0) {
            return (int)i;
        }
    }

    return smart_home_llm_backend_index_by_id("custom");
}

int smart_home_model_config_from_preset(
    smart_home_model_config_t *config,
    const smart_home_llm_backend_preset_t *preset,
    int keep_api_key)
{
    char api_key[sizeof(config->api_key)];

    if (!config || !preset) {
        return AGENT_ERROR_INVALID;
    }

    copy_string(api_key, sizeof(api_key), keep_api_key ? config->api_key : "");
    copy_string(config->backend_id, sizeof(config->backend_id), preset->id);
    if ((preset->flags & SMART_HOME_LLM_FLAG_CUSTOM) == 0u) {
        copy_string(config->host, sizeof(config->host), preset->host);
        copy_string(config->path, sizeof(config->path), preset->path);
        copy_string(config->port, sizeof(config->port), preset->port);
        copy_string(config->model, sizeof(config->model), preset->default_model);
    } else {
        if (!config->port[0]) {
            copy_string(config->port, sizeof(config->port), preset->port);
        }
    }
    if (preset->default_timeout_ms > 0u) {
        config->timeout_ms = (int)preset->default_timeout_ms;
    }
    copy_string(config->api_key, sizeof(config->api_key), api_key);

    return AGENT_OK;
}

int smart_home_model_config_validate(const smart_home_model_config_t *config,
                                     char *error,
                                     size_t error_size)
{
    const char *port;

    if (!config) {
        set_error(error, error_size, "Missing model config.");
        return AGENT_ERROR_INVALID;
    }

    if (!config->host[0]) {
        set_error(error, error_size, "Host is required.");
        return AGENT_ERROR_INVALID;
    }
    if (contains_ctl_or_space(config->host) || strchr(config->host, '/')) {
        set_error(error, error_size, "Host contains invalid characters.");
        return AGENT_ERROR_INVALID;
    }

    if (!config->path[0] || config->path[0] != '/') {
        set_error(error, error_size, "Path must start with /.");
        return AGENT_ERROR_INVALID;
    }
    if (contains_crlf(config->path)) {
        set_error(error, error_size, "Path contains invalid characters.");
        return AGENT_ERROR_INVALID;
    }

    if (!config->port[0]) {
        set_error(error, error_size, "Port is required.");
        return AGENT_ERROR_INVALID;
    }
    for (port = config->port; *port; port++) {
        if (!isdigit((unsigned char)*port)) {
            set_error(error, error_size, "Port must be numeric.");
            return AGENT_ERROR_INVALID;
        }
    }

    if (!config->model[0]) {
        set_error(error, error_size, "Model is required.");
        return AGENT_ERROR_INVALID;
    }
    if (contains_crlf(config->model)) {
        set_error(error, error_size, "Model contains invalid characters.");
        return AGENT_ERROR_INVALID;
    }

    if (config->timeout_ms < 5000 || config->timeout_ms > 60000) {
        set_error(error, error_size, "Timeout must be 5000-60000 ms.");
        return AGENT_ERROR_INVALID;
    }

    set_error(error, error_size, "");
    return AGENT_OK;
}
