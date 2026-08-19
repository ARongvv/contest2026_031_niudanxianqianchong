/* SPDX-License-Identifier: Apache-2.0 */
#include <cagent/config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim_inplace(char *s)
{
    char *end;

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    return s;
}

static int load_file(const char *path,
                     agent_config_kv_fn callback,
                     void *user_data)
{
    FILE *fp;
    char line[512];

    if (!path || path[0] == '\0') {
        return AGENT_OK;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return AGENT_OK;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *key;
        char *value;
        char *eq;
        int ret;

        key = trim_inplace(line);
        if (key[0] == '\0' || key[0] == '#') {
            continue;
        }

        eq = strchr(key, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        value = trim_inplace(eq + 1);
        key = trim_inplace(key);

        ret = callback(key, value, user_data);
        if (ret < 0) {
            fclose(fp);
            return ret;
        }
    }

    fclose(fp);
    return AGENT_OK;
}

static void key_to_env_name(const char *prefix,
                            const char *key,
                            char *buf,
                            size_t buf_size)
{
    size_t i;

    i = 0u;

    if (prefix) {
        while (*prefix && i + 1u < buf_size) {
            buf[i++] = *prefix++;
        }
    }

    while (*key && i + 1u < buf_size) {
        if (isalnum((unsigned char)*key)) {
            buf[i++] = (char)toupper((unsigned char)*key);
        } else {
            buf[i++] = '_';
        }
        key++;
    }

    buf[i] = '\0';
}

static int load_env(const char *prefix,
                    agent_config_kv_fn callback,
                    void *user_data)
{
    static const struct {
        const char *key;
    } common_keys[] = {
        { "api_key" },
        { "host" },
        { "path" },
        { "port" },
        { "model" },
        { "timeout_ms" },
    };

    size_t i;

    if (!prefix || prefix[0] == '\0') {
        return AGENT_OK;
    }

    for (i = 0u; i < sizeof(common_keys) / sizeof(common_keys[0]); i++) {
        char env_name[128];
        const char *val;

        key_to_env_name(prefix, common_keys[i].key, env_name, sizeof(env_name));
        val = getenv(env_name);
        if (val && val[0]) {
            int ret = callback(common_keys[i].key, val, user_data);
            if (ret < 0) {
                return ret;
            }
        }
    }

    return AGENT_OK;
}

int agent_config_load(const char *path,
                      const char *env_prefix,
                      agent_config_kv_fn callback,
                      void *user_data)
{
    int ret;

    if (!callback) {
        return AGENT_ERROR_INVALID;
    }

    ret = load_file(path, callback, user_data);
    if (ret < 0) {
        return ret;
    }

    ret = load_env(env_prefix, callback, user_data);
    if (ret < 0) {
        return ret;
    }

    return AGENT_OK;
}
