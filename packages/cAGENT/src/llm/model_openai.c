/* SPDX-License-Identifier: Apache-2.0 */
#include <cagent/model_openai.h>

#include "../core/agent_internal.h"
#include "../runtime/runtime.h"
#ifdef CAGENT_RUNTIME_OPENVELA
#include "../runtime/runtime_openvela.h"
#define openai_mem_region_log(label, pointer) ov_mem_region_log(label, pointer)

static void *openai_bulk_malloc(size_t size, void *user_data)
{
    (void)user_data;
    return ov_mem_bulk_alloc(size);
}

static void openai_bulk_free(void *pointer, void *user_data)
{
    (void)user_data;
    ov_mem_bulk_free(pointer);
}
#else
#define openai_mem_region_log(label, pointer) ((void)0)
#endif
#include "../types_internal.h"

#include <ctype.h>
#include <limits.h>
#ifdef __NuttX__
#include <malloc.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENAI_COMPAT_MAGIC 0x4f414943u

#ifdef __NuttX__
static void openai_mem_diag(const char *point)
{
    struct mallinfo info = mallinfo();

    printf("[cagent_mem] %s fordblks=%u mxordblk=%u uordblks=%u\n",
           point, info.fordblks, info.mxordblk, info.uordblks);
}
#else
#define openai_mem_diag(point) ((void)0)
#endif

#ifdef CAGENT_MODEL_OPENAI_DEBUG
#define OPENAI_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define OPENAI_DEBUG_PRINTF(...) ((void)0)
#endif

typedef struct {
    uint32_t magic;
    char host[CAGENT_MODEL_OPENAI_HOST_MAX];
    char path[CAGENT_MODEL_OPENAI_PATH_MAX];
    char port[CAGENT_MODEL_OPENAI_PORT_MAX];
    char api_key[CAGENT_MODEL_OPENAI_KEY_MAX];
    char model[CAGENT_MODEL_OPENAI_MODEL_MAX];
    uint32_t timeout_ms;
    uint32_t request_buffer_size;
    uint32_t response_buffer_size;
    char *last_response;
    agent_runtime_t last_response_runtime;
    bool has_last_response_runtime;
    agent_tool_call_t tool_calls[CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS];
    char tool_call_ids[CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS][64];
    char tool_call_names[CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS][64];
    char tool_call_args[CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS][CAGENT_TOOL_ARGS_MAX_SIZE];
    volatile int cancel_requested;
} openai_compat_provider_t;

static void free_last_response(openai_compat_provider_t *provider)
{
    if (!provider || !provider->last_response) {
        return;
    }

    if (provider->has_last_response_runtime) {
        agent_runtime_free(&provider->last_response_runtime, provider->last_response);
    } else {
        free(provider->last_response);
    }
    provider->last_response = NULL;
    provider->has_last_response_runtime = false;
    memset(&provider->last_response_runtime, 0, sizeof(provider->last_response_runtime));
}

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

static uint32_t effective_buffer_size(uint32_t configured, uint32_t fallback)
{
    return configured ? configured : fallback;
}

static size_t json_escape_bound(const char *text)
{
    size_t len;

    if (!text) {
        return 1u;
    }

    len = strlen(text);
    if (len > ((size_t)-1 - 1u) / 6u) {
        return 0u;
    }

    return len * 6u + 1u;
}

static int json_escape_copy(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0u;

    if (!dst || dst_size == 0u) {
        return AGENT_ERROR_INVALID;
    }

    if (!src) {
        dst[0] = '\0';
        return AGENT_OK;
    }

    while (*src) {
        unsigned char c = (unsigned char)*src++;
        const char *rep = NULL;
        char unicode[7];

        switch (c) {
        case '\\':
            rep = "\\\\";
            break;
        case '"':
            rep = "\\\"";
            break;
        case '\b':
            rep = "\\b";
            break;
        case '\f':
            rep = "\\f";
            break;
        case '\n':
            rep = "\\n";
            break;
        case '\r':
            rep = "\\r";
            break;
        case '\t':
            rep = "\\t";
            break;
        default:
            if (c < 0x20u) {
                snprintf(unicode, sizeof(unicode), "\\u%04x", c);
                rep = unicode;
            }
            break;
        }

        if (rep) {
            size_t n = strlen(rep);
            if (out + n >= dst_size) {
                return AGENT_ERROR_CONTEXT_OVERFLOW;
            }
            memcpy(dst + out, rep, n);
            out += n;
        } else {
            if (out + 1u >= dst_size) {
                return AGENT_ERROR_CONTEXT_OVERFLOW;
            }
            dst[out++] = (char)c;
        }
    }

    dst[out] = '\0';
    return AGENT_OK;
}

static char *alloc_escaped(const char *text)
{
    size_t cap = json_escape_bound(text);
    char *out;

    if (cap == 0u) {
        return NULL;
    }

    out = (char *)malloc(cap);
    if (!out) {
        return NULL;
    }

    if (json_escape_copy(text, out, cap) != AGENT_OK) {
        free(out);
        return NULL;
    }

    return out;
}

static int build_request_json(openai_compat_provider_t *provider,
                              const agent_model_request_t *request,
                              char *buffer,
                              size_t buffer_size)
{
    char *context;
    char *input;
    const char *messages_json;
    const char *messages_body;
    const char *tools_json;
    size_t messages_len;
    size_t messages_body_len;
    bool has_messages_json;
    uint32_t max_tokens;
    int n;

    context = alloc_escaped(request->context ? request->context : "");
    input = alloc_escaped(request->input ? request->input : "");
    if (!context || !input) {
        free(context);
        free(input);
        return AGENT_ERROR_NOMEM;
    }

    messages_json = request->messages_json;
    messages_len = messages_json ? strlen(messages_json) : 0u;
    has_messages_json = messages_len >= 2u
                        && messages_len <= (size_t)INT_MAX
                        && messages_json[0] == '['
                        && messages_json[messages_len - 1u] == ']'
                        && strcmp(messages_json, "[]") != 0;
    messages_body = has_messages_json ? messages_json + 1u : NULL;
    messages_body_len = has_messages_json ? messages_len - 2u : 0u;

    tools_json = request->tools_json;
    max_tokens = request->max_output_tokens ? request->max_output_tokens
                                            : CAGENT_DEFAULT_MAX_OUTPUT_TOKENS;

    if (tools_json && tools_json[0] != '\0' && strcmp(tools_json, "[]") != 0) {
        if (has_messages_json) {
            n = snprintf(buffer,
                         buffer_size,
                         "{\"model\":\"%s\",\"messages\":["
                         "{\"role\":\"system\",\"content\":\"%s\"},%.*s],"
                         "\"tools\":%s,\"tool_choice\":\"auto\","
                         "\"max_tokens\":%u}",
                         provider->model,
                         context,
                         (int)messages_body_len,
                         messages_body,
                         tools_json,
                         (unsigned int)max_tokens);
        } else {
            n = snprintf(buffer,
                         buffer_size,
                         "{\"model\":\"%s\",\"messages\":["
                         "{\"role\":\"system\",\"content\":\"%s\"},"
                         "{\"role\":\"user\",\"content\":\"%s\"}],"
                         "\"tools\":%s,\"tool_choice\":\"auto\","
                         "\"max_tokens\":%u}",
                         provider->model,
                         context,
                         input,
                         tools_json,
                         (unsigned int)max_tokens);
        }
    } else {
        if (has_messages_json) {
            n = snprintf(buffer,
                         buffer_size,
                         "{\"model\":\"%s\",\"messages\":["
                         "{\"role\":\"system\",\"content\":\"%s\"},%.*s],"
                         "\"max_tokens\":%u}",
                         provider->model,
                         context,
                         (int)messages_body_len,
                         messages_body,
                         (unsigned int)max_tokens);
        } else {
            n = snprintf(buffer,
                         buffer_size,
                         "{\"model\":\"%s\",\"messages\":["
                         "{\"role\":\"system\",\"content\":\"%s\"},"
                         "{\"role\":\"user\",\"content\":\"%s\"}],"
                         "\"max_tokens\":%u}",
                         provider->model,
                         context,
                         input,
                         (unsigned int)max_tokens);
        }
    }

    free(context);
    free(input);

    if (n < 0) {
        return AGENT_ERROR;
    }
    if ((size_t)n >= buffer_size) {
        return AGENT_ERROR_CONTEXT_OVERFLOW;
    }

    return AGENT_OK;
}

static char *skip_ws(char *p)
{
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static char *find_key(char *json, const char *key)
{
    char *p;

    if (!json || !key) {
        return NULL;
    }

    p = strstr(json, key);
    if (!p) {
        return NULL;
    }

    p = strchr(p + strlen(key), ':');
    if (!p) {
        return NULL;
    }
    return skip_ws(p + 1);
}

static char *parse_json_string_inplace(char *p)
{
    char *readp;
    char *writep;

    if (!p || *p != '"') {
        return NULL;
    }

    readp = p + 1;
    writep = readp;
    while (*readp) {
        if (*readp == '\\') {
            readp++;
            switch (*readp) {
            case 'n':
                *writep++ = '\n';
                break;
            case 'r':
                *writep++ = '\r';
                break;
            case 't':
                *writep++ = '\t';
                break;
            case 'b':
                *writep++ = '\b';
                break;
            case 'f':
                *writep++ = '\f';
                break;
            case '\\':
            case '"':
            case '/':
                *writep++ = *readp;
                break;
            case '\0':
                return NULL;
            default:
                *writep++ = *readp;
                break;
            }
            readp++;
            continue;
        }

        if (*readp == '"') {
            *writep = '\0';
            return p + 1;
        }

        *writep++ = *readp++;
    }

    return NULL;
}

static char *find_matching_json_end(char *p)
{
    char open_ch;
    char close_ch;
    uint32_t depth = 0u;

    if (!p || (*p != '{' && *p != '[')) {
        return NULL;
    }

    open_ch = *p;
    close_ch = open_ch == '{' ? '}' : ']';

    while (*p) {
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '\\') {
                    if (p[1] == '\0') {
                        return NULL;
                    }
                    p += 2;
                    continue;
                }
                if (*p == '"') {
                    break;
                }
                p++;
            }
            if (*p == '\0') {
                return NULL;
            }
        } else if (*p == open_ch) {
            depth++;
        } else if (*p == close_ch) {
            depth--;
            if (depth == 0u) {
                return p;
            }
        }
        p++;
    }

    return NULL;
}

static int copy_json_value(char *dst, size_t dst_size, char *value)
{
    char *end;
    size_t len;

    if (!dst || dst_size == 0u || !value) {
        return AGENT_ERROR_INVALID;
    }

    value = skip_ws(value);
    if (!value) {
        return AGENT_ERROR_PARSE;
    }

    if (*value == '"') {
        value = parse_json_string_inplace(value);
        if (!value) {
            return AGENT_ERROR_PARSE;
        }
        len = strlen(value);
    } else if (*value == '{' || *value == '[') {
        end = find_matching_json_end(value);
        if (!end) {
            return AGENT_ERROR_PARSE;
        }
        len = (size_t)(end - value + 1);
    } else {
        return AGENT_ERROR_PARSE;
    }

    if (len >= dst_size) {
        return AGENT_ERROR_LIMIT;
    }

    memcpy(dst, value, len);
    dst[len] = '\0';
    return AGENT_OK;
}

static char *extract_content_string(char *json)
{
    char *p = find_key(json, "\"content\"");

    if (!p || *p == 'n') {
        return NULL;
    }

    return parse_json_string_inplace(p);
}

static int parse_tool_call_item(openai_compat_provider_t *provider,
                                char *item,
                                size_t index)
{
    char *id;
    char *name;
    char *args;
    int ret;

    if (index >= CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS) {
        return AGENT_ERROR_LIMIT;
    }

    id = find_key(item, "\"id\"");
    name = find_key(item, "\"name\"");
    args = find_key(item, "\"arguments\"");
    if (!id || !name || !args) {
        return AGENT_ERROR_PARSE;
    }

    ret = copy_json_value(provider->tool_call_ids[index],
                          sizeof(provider->tool_call_ids[index]),
                          id);
    if (ret != AGENT_OK) {
        return ret;
    }
    ret = copy_json_value(provider->tool_call_names[index],
                          sizeof(provider->tool_call_names[index]),
                          name);
    if (ret != AGENT_OK) {
        return ret;
    }
    ret = copy_json_value(provider->tool_call_args[index],
                          sizeof(provider->tool_call_args[index]),
                          args);
    if (ret != AGENT_OK) {
        return ret;
    }

    provider->tool_calls[index].id = provider->tool_call_ids[index];
    provider->tool_calls[index].name = provider->tool_call_names[index];
    provider->tool_calls[index].arguments_json = provider->tool_call_args[index];
    return AGENT_OK;
}

static int extract_tool_calls(openai_compat_provider_t *provider,
                              char *json,
                              size_t *count)
{
    char *array;
    char *p;
    size_t parsed = 0u;

    if (!provider || !json || !count) {
        return AGENT_ERROR_INVALID;
    }

    *count = 0u;
    array = find_key(json, "\"tool_calls\"");
    if (!array || *array == 'n') {
        return AGENT_OK;
    }
    if (*array != '[') {
        return AGENT_ERROR_PARSE;
    }

    p = skip_ws(array + 1);
    while (p && *p && *p != ']') {
        char *item_start;
        char *item_end;
        char saved;
        int ret;

        if (*p != '{') {
            return AGENT_ERROR_PARSE;
        }

        item_start = p;
        item_end = find_matching_json_end(item_start);
        if (!item_end) {
            return AGENT_ERROR_PARSE;
        }
        if (parsed >= CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS) {
            return AGENT_ERROR_LIMIT;
        }

        saved = item_end[1];
        item_end[1] = '\0';
        ret = parse_tool_call_item(provider, item_start, parsed);
        item_end[1] = saved;
        if (ret != AGENT_OK) {
            return ret;
        }
        parsed++;

        p = skip_ws(item_end + 1);
        if (*p == ',') {
            p = skip_ws(p + 1);
        } else if (*p != ']') {
            return AGENT_ERROR_PARSE;
        }
    }

    *count = parsed;
    return AGENT_OK;
}

static int openai_compat_complete(void *provider_data,
                                  agent_runtime_t *runtime,
                                  const agent_model_request_t *request,
                                  agent_model_response_t *response)
{
    openai_compat_provider_t *provider = (openai_compat_provider_t *)provider_data;
    char *request_body;
    char headers[CAGENT_MODEL_OPENAI_KEY_MAX + 96u];
    agent_http_request_t http_request;
    agent_http_response_t http_response;
    agent_runtime_t buffer_runtime;
    uint32_t request_size;
    uint32_t response_size;
    uint32_t timeout_ms;
    int ret;

    if (!provider || provider->magic != OPENAI_COMPAT_MAGIC ||
        !runtime || !request || !response) {
        return AGENT_ERROR_INVALID;
    }
    if (provider->host[0] == '\0' || provider->path[0] == '\0' ||
        provider->port[0] == '\0' || provider->model[0] == '\0') {
        return AGENT_ERROR_INVALID;
    }

    memset(response, 0, sizeof(*response));
    provider->cancel_requested = 0;

    request_size = effective_buffer_size(provider->request_buffer_size,
                                         CAGENT_HTTP_REQUEST_BUFFER_SIZE);
    response_size = effective_buffer_size(provider->response_buffer_size,
                                          CAGENT_HTTP_RESPONSE_BUFFER_SIZE);

    buffer_runtime = *runtime;
#ifdef CAGENT_RUNTIME_OPENVELA
    buffer_runtime.malloc_fn = openai_bulk_malloc;
    buffer_runtime.free_fn = openai_bulk_free;
    buffer_runtime.user_data = NULL;
    ov_mem_bulk_diag("model-buffer-before");
#endif

    openai_mem_diag("model-request-before");
    request_body = (char *)agent_runtime_malloc(&buffer_runtime, request_size);
    if (!request_body) {
        return AGENT_ERROR_NOMEM;
    }
    openai_mem_region_log("model-request-buffer", request_body);
    openai_mem_diag("model-request-after");

    ret = build_request_json(provider, request, request_body, request_size);
    if (ret != AGENT_OK) {
        agent_runtime_free(&buffer_runtime, request_body);
        return ret;
    }

    free_last_response(provider);
    openai_mem_diag("model-response-before");
    provider->last_response =
        (char *)agent_runtime_malloc(&buffer_runtime, response_size);
    if (!provider->last_response) {
        agent_runtime_free(&buffer_runtime, request_body);
        return AGENT_ERROR_NOMEM;
    }
    provider->last_response_runtime = buffer_runtime;
    provider->has_last_response_runtime = true;
    provider->last_response[0] = '\0';
    openai_mem_region_log("model-response-buffer", provider->last_response);
    openai_mem_diag("model-response-after");

    if (provider->api_key[0] != '\0') {
        snprintf(headers,
                 sizeof(headers),
                 "Content-Type: application/json\r\n"
                 "Authorization: Bearer %s\r\n",
                 provider->api_key);
    } else {
        snprintf(headers,
                 sizeof(headers),
                 "Content-Type: application/json\r\n");
    }

    timeout_ms = provider->timeout_ms ? provider->timeout_ms : request->timeout_ms;

    memset(&http_request, 0, sizeof(http_request));
    http_request.method = "POST";
    http_request.host = provider->host;
    http_request.path = provider->path;
    http_request.port = provider->port;
    http_request.headers = headers;
    http_request.body = request_body;
    http_request.body_size = strlen(request_body);
    http_request.timeout_ms = timeout_ms;

    memset(&http_response, 0, sizeof(http_response));
    http_response.body = provider->last_response;
    http_response.body_size = response_size;

    ret = agent_runtime_http_post(runtime, &http_request, &http_response);
    agent_runtime_free(&buffer_runtime, request_body);
    openai_mem_diag("model-request-finished");
#ifdef CAGENT_RUNTIME_OPENVELA
    ov_mem_bulk_diag("model-buffer-after-request");
#endif

    if (provider->cancel_requested) {
        response->status = AGENT_ERROR_CANCELLED;
        return AGENT_ERROR_CANCELLED;
    }
    if (ret != AGENT_OK) {
        response->status = ret;
        return ret;
    }
    if (http_response.status_code != 0 &&
        (http_response.status_code < 200 || http_response.status_code >= 300)) {
        OPENAI_DEBUG_PRINTF("[model_openai] HTTP %d, response: %.200s\n",
                            http_response.status_code,
                            provider->last_response ? provider->last_response : "(null)");
        response->status = AGENT_ERROR_NETWORK;
        return AGENT_ERROR_NETWORK;
    }

    if (http_response.bytes_written < response_size) {
        provider->last_response[http_response.bytes_written] = '\0';
    } else {
        provider->last_response[response_size - 1u] = '\0';
    }

    OPENAI_DEBUG_PRINTF("[model_openai] HTTP %d, %zu bytes: %.300s\n",
                        http_response.status_code,
                        http_response.bytes_written,
                        provider->last_response ? provider->last_response : "(null)");

    ret = extract_tool_calls(provider,
                             provider->last_response,
                             &response->tool_call_count);
    if (ret != AGENT_OK) {
        response->status = ret;
        return ret;
    }
    if (response->tool_call_count > 0u) {
        response->tool_calls = provider->tool_calls;
        response->content = NULL;
        response->status = AGENT_OK;
        return AGENT_OK;
    }

    response->content = extract_content_string(provider->last_response);
    if (!response->content) {
        OPENAI_DEBUG_PRINTF("[model_openai] extract_content_string FAILED, raw: %.300s\n",
                            provider->last_response ? provider->last_response : "(null)");
        response->status = AGENT_ERROR_PARSE;
        return AGENT_ERROR_PARSE;
    }

    response->tool_calls = NULL;
    response->tool_call_count = 0u;
    response->status = AGENT_OK;
    return AGENT_OK;
}

static int openai_compat_cancel(void *provider_data)
{
    openai_compat_provider_t *provider = (openai_compat_provider_t *)provider_data;

    if (!provider || provider->magic != OPENAI_COMPAT_MAGIC) {
        return AGENT_ERROR_INVALID;
    }

    provider->cancel_requested = 1;
    return AGENT_OK;
}

static void openai_compat_destroy(void *provider_data)
{
    openai_compat_provider_t *provider = (openai_compat_provider_t *)provider_data;

    if (!provider) {
        return;
    }

    provider->magic = 0u;
    free_last_response(provider);
    free(provider);
}

static openai_compat_provider_t *provider_from_model(agent_model_t *model)
{
    openai_compat_provider_t *provider;

    if (!model) {
        return NULL;
    }

    provider = (openai_compat_provider_t *)model->provider;
    if (!provider || provider->magic != OPENAI_COMPAT_MAGIC ||
        model->ops.complete != openai_compat_complete) {
        return NULL;
    }

    return provider;
}

agent_model_openai_config_t agent_model_openai_config_default(void)
{
    agent_model_openai_config_t config;

    memset(&config, 0, sizeof(config));
    config.path = "/v1/chat/completions";
    config.port = "443";
    config.request_buffer_size = CAGENT_HTTP_REQUEST_BUFFER_SIZE;
    config.response_buffer_size = CAGENT_HTTP_RESPONSE_BUFFER_SIZE;

    return config;
}

agent_model_t *agent_model_openai_create(const agent_model_openai_config_t *config)
{
    agent_model_openai_config_t local;
    openai_compat_provider_t *provider;
    agent_model_ops_t ops;
    agent_model_t *model;

    local = config ? *config : agent_model_openai_config_default();
    if (!local.path) {
        local.path = "/v1/chat/completions";
    }
    if (!local.port) {
        local.port = "443";
    }

    if (!local.host || !local.model) {
        return NULL;
    }

    provider = (openai_compat_provider_t *)calloc(1u, sizeof(*provider));
    if (!provider) {
        return NULL;
    }

    provider->magic = OPENAI_COMPAT_MAGIC;
    copy_string(provider->host, sizeof(provider->host), local.host);
    copy_string(provider->path, sizeof(provider->path), local.path);
    copy_string(provider->port, sizeof(provider->port), local.port);
    copy_string(provider->api_key, sizeof(provider->api_key), local.api_key);
    copy_string(provider->model, sizeof(provider->model), local.model);
    provider->timeout_ms = local.timeout_ms;
    provider->request_buffer_size =
        effective_buffer_size(local.request_buffer_size,
                              CAGENT_HTTP_REQUEST_BUFFER_SIZE);
    provider->response_buffer_size =
        effective_buffer_size(local.response_buffer_size,
                              CAGENT_HTTP_RESPONSE_BUFFER_SIZE);

    memset(&ops, 0, sizeof(ops));
    ops.complete = openai_compat_complete;
    ops.cancel = openai_compat_cancel;
    ops.destroy = openai_compat_destroy;

    model = agent_model_create(&ops, provider);
    if (!model) {
        openai_compat_destroy(provider);
    }

    return model;
}

agent_model_t *agent_model_openai_create_simple(const char *host,
                                                 const char *api_key,
                                                 const char *model_name)
{
    agent_model_openai_config_t cfg;

    if (!host || !model_name) {
        return NULL;
    }

    cfg = agent_model_openai_config_default();
    cfg.host = host;
    cfg.api_key = api_key;
    cfg.model = model_name;

    return agent_model_openai_create(&cfg);
}

int agent_model_openai_set_backend(agent_model_t *model,
                                   const char *host,
                                   const char *path,
                                   const char *port)
{
    openai_compat_provider_t *provider = provider_from_model(model);

    if (!provider || !host || !path || !port) {
        return AGENT_ERROR_INVALID;
    }

    copy_string(provider->host, sizeof(provider->host), host);
    copy_string(provider->path, sizeof(provider->path), path);
    copy_string(provider->port, sizeof(provider->port), port);
    return AGENT_OK;
}

int agent_model_openai_set_api_key(agent_model_t *model, const char *api_key)
{
    openai_compat_provider_t *provider = provider_from_model(model);

    if (!provider) {
        return AGENT_ERROR_INVALID;
    }

    copy_string(provider->api_key, sizeof(provider->api_key), api_key);
    return AGENT_OK;
}

int agent_model_openai_set_model(agent_model_t *model, const char *model_name)
{
    openai_compat_provider_t *provider = provider_from_model(model);

    if (!provider || !model_name) {
        return AGENT_ERROR_INVALID;
    }

    copy_string(provider->model, sizeof(provider->model), model_name);
    return AGENT_OK;
}

agent_model_openai_compat_config_t agent_model_openai_compat_config_default(void)
{
    return agent_model_openai_config_default();
}

agent_model_t *agent_model_openai_compat_create(
    const agent_model_openai_compat_config_t *config)
{
    return agent_model_openai_create(config);
}

int agent_model_openai_compat_set_backend(agent_model_t *model,
                                          const char *host,
                                          const char *path,
                                          const char *port)
{
    return agent_model_openai_set_backend(model, host, path, port);
}

int agent_model_openai_compat_set_api_key(agent_model_t *model,
                                          const char *api_key)
{
    return agent_model_openai_set_api_key(model, api_key);
}

int agent_model_openai_compat_set_model(agent_model_t *model,
                                        const char *model_name)
{
    return agent_model_openai_set_model(model, model_name);
}

int agent_attach_openai(agent_t *agent,
                        const char *host,
                        const char *api_key,
                        const char *model)
{
    agent_model_t *m;

    if (!agent || !host || !model) {
        return AGENT_ERROR_INVALID;
    }

    m = agent_model_openai_create_simple(host, api_key, model);
    if (!m) {
        return AGENT_ERROR_NOMEM;
    }

    return agent_set_model_owned(agent, m);
}
