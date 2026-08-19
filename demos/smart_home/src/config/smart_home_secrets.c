/* SPDX-License-Identifier: Apache-2.0 */
/* 专用 secrets.json loader；刻意不复用可回退、可写回的 config_store。 */

#include "smart_home_secrets.h"

#include "cjson_compat.h"

#include <cagent/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMART_HOME_SECRETS_VERSION 1

static void secure_clear(void *memory, size_t size)
{
    volatile unsigned char *p = memory;

    while (p && size-- > 0u) {
        *p++ = 0u;
    }
}

static int valid_backend_id(const char *id)
{
    const unsigned char *p = (const unsigned char *)id;
    size_t length = 0u;

    if (!p || !*p) {
        return 0;
    }

    while (*p) {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) {
            return 0;
        }
        if (++length > 32u) {
            return 0;
        }
        p++;
    }

    return 1;
}

static int contains_control(const char *value)
{
    const unsigned char *p = (const unsigned char *)value;

    if (!p) {
        return 1;
    }
    while (*p) {
        if (*p < 0x20u || *p == 0x7fu) {
            return 1;
        }
        p++;
    }
    return 0;
}

static int read_secrets_file(char **out_text, size_t *out_size)
{
    FILE *file;
    long size = -1;
    char *text;

    *out_text = NULL;
    *out_size = 0u;
    file = fopen(CONFIG_SMART_HOME_MODEL_SECRETS_PATH, "rb");
    if (!file) {
        return errno == ENOENT ? AGENT_ERROR_NOTFOUND : AGENT_ERROR;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return AGENT_ERROR;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return AGENT_ERROR;
    }
    if ((size_t)size > CONFIG_SMART_HOME_MODEL_SECRETS_MAX_FILE_SIZE) {
        fclose(file);
        return AGENT_ERROR_LIMIT;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return AGENT_ERROR;
    }

    text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return AGENT_ERROR_NOMEM;
    }
    if (fread(text, 1u, (size_t)size, file) != (size_t)size) {
        fclose(file);
        secure_clear(text, (size_t)size + 1u);
        free(text);
        return AGENT_ERROR;
    }
    fclose(file);
    text[size] = '\0';
    *out_text = text;
    *out_size = (size_t)size;
    return AGENT_OK;
}

static int lookup_model_api_key(const char *backend_id,
                                char *out,
                                size_t out_size)
{
    char *text = NULL;
    size_t text_size = 0u;
    cJSON *root = NULL;
    cJSON *version;
    cJSON *keys;
    cJSON *value;
    int ret;

    if (!backend_id || !out || out_size == 0u || !valid_backend_id(backend_id)) {
        return AGENT_ERROR_INVALID;
    }
    out[0] = '\0';

    ret = read_secrets_file(&text, &text_size);
    if (ret != AGENT_OK) {
        return ret;
    }

    root = cJSON_ParseWithLength(text, text_size);
    secure_clear(text, text_size + 1u);
    free(text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return AGENT_ERROR_PARSE;
    }

    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    keys = cJSON_GetObjectItemCaseSensitive(root, "model_api_keys");
    value = cJSON_IsObject(keys)
                ? cJSON_GetObjectItemCaseSensitive(keys, backend_id) : NULL;
    if (!cJSON_IsNumber(version) || version->valueint != SMART_HOME_SECRETS_VERSION ||
        !cJSON_IsObject(keys) || !cJSON_IsString(value) ||
        !value->valuestring[0]) {
        ret = (!cJSON_IsNumber(version) || version->valueint != SMART_HOME_SECRETS_VERSION ||
               !cJSON_IsObject(keys)) ? AGENT_ERROR_PARSE : AGENT_ERROR_NOTFOUND;
        cJSON_Delete(root);
        return ret;
    }
    if (contains_control(value->valuestring)) {
        cJSON_Delete(root);
        return AGENT_ERROR_PARSE;
    }
    if (strlen(value->valuestring) >= out_size) {
        cJSON_Delete(root);
        return AGENT_ERROR_LIMIT;
    }

    memcpy(out, value->valuestring, strlen(value->valuestring) + 1u);
    secure_clear(value->valuestring, strlen(value->valuestring));
    cJSON_Delete(root);
    return AGENT_OK;
}

int smart_home_secrets_get_model_api_key(const char *backend_id,
                                         char *out,
                                         size_t out_size)
{
    int ret;

    if (out && out_size > 0u) {
        out[0] = '\0';
    }
    ret = lookup_model_api_key(backend_id, out, out_size);
    if (ret != AGENT_OK && out && out_size > 0u) {
        secure_clear(out, out_size);
    }
    return ret;
}

int smart_home_secrets_model_api_key_status(const char *backend_id)
{
    char key[256];
    int ret = smart_home_secrets_get_model_api_key(backend_id,
                                                   key,
                                                   sizeof(key));

    secure_clear(key, sizeof(key));
    return ret;
}
