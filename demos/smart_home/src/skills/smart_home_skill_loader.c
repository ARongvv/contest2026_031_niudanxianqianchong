#include "smart_home_skill_loader.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMART_HOME_SKILL_READ_MAX 4096u

static int has_md_suffix(const char *name)
{
    size_t len;

    if (!name) {
        return 0;
    }

    len = strlen(name);
    return len > 3u && strcmp(name + len - 3u, ".md") == 0;
}

static char *trim(char *text)
{
    char *end;

    if (!text) {
        return text;
    }

    while (*text == ' ' || *text == '\t' ||
           *text == '\r' || *text == '\n') {
        text++;
    }

    end = text + strlen(text);
    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t' ||
            end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }

    return text;
}

static int parse_flags(const char *text)
{
    uint32_t flags = 0u;

    if (!text) {
        return AGENT_ERROR_INVALID;
    }

    if (strstr(text, "enabled")) {
        flags |= AGENT_SKILL_FLAG_ENABLED;
    }
    if (strstr(text, "llm_visible")) {
        flags |= AGENT_SKILL_FLAG_LLM_VISIBLE;
    }
    if (strstr(text, "summary_only")) {
        flags |= AGENT_SKILL_FLAG_SUMMARY_ONLY;
    }

    return (int)flags;
}

static int parse_front_matter(char *buffer,
                              smart_home_loaded_skill_t *loaded,
                              agent_skill_t *skill)
{
    char *body;
    char *meta;
    char *line;
    char *save = NULL;
    int flags_seen = 0;

    if (!buffer || !loaded || !skill || strncmp(buffer, "---", 3) != 0) {
        return AGENT_ERROR_INVALID;
    }

    meta = buffer + 3;
    if (*meta == '\r') {
        meta++;
    }
    if (*meta == '\n') {
        meta++;
    }

    body = strstr(meta, "\n---");
    if (!body) {
        return AGENT_ERROR_INVALID;
    }

    *body = '\0';
    body++;
    if (strncmp(body, "---", 3) == 0) {
        body += 3;
    }
    if (*body == '\r') {
        body++;
    }
    if (*body == '\n') {
        body++;
    }

    memset(skill, 0, sizeof(*skill));
    line = strtok_r(meta, "\n", &save);
    while (line) {
        char *colon = strchr(line, ':');

        if (colon) {
            char *key;
            char *value;

            *colon = '\0';
            key = trim(line);
            value = trim(colon + 1);

            if (strcmp(key, "name") == 0) {
                loaded->name = value;
                skill->name = value;
            } else if (strcmp(key, "description") == 0) {
                loaded->description = value;
                skill->description = value;
            } else if (strcmp(key, "priority") == 0) {
                skill->priority = (uint32_t)strtoul(value, NULL, 10);
            } else if (strcmp(key, "flags") == 0) {
                int parsed = parse_flags(value);
                if (parsed < 0) {
                    return parsed;
                }
                skill->flags = (uint32_t)parsed;
                flags_seen = 1;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }

    loaded->context_text = trim(body);
    skill->context_text = loaded->context_text;
    if (!skill->name || !skill->context_text || !skill->context_text[0]) {
        return AGENT_ERROR_INVALID;
    }
    if (!flags_seen) {
        skill->flags = AGENT_SKILL_FLAG_ENABLED | AGENT_SKILL_FLAG_LLM_VISIBLE;
    }

    return AGENT_OK;
}

static int read_file(const char *path, char **buffer_out)
{
    FILE *fp;
    long size;
    char *buffer;
    size_t nread;

    if (!path || !buffer_out) {
        return AGENT_ERROR_INVALID;
    }

    *buffer_out = NULL;
    fp = fopen(path, "rb");
    if (!fp) {
        return AGENT_ERROR_NOTFOUND;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return AGENT_ERROR;
    }
    size = ftell(fp);
    if (size <= 0 || (size_t)size > SMART_HOME_SKILL_READ_MAX) {
        fclose(fp);
        return AGENT_ERROR_LIMIT;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return AGENT_ERROR;
    }

    buffer = (char *)calloc(1u, (size_t)size + 1u);
    if (!buffer) {
        fclose(fp);
        return AGENT_ERROR_NOMEM;
    }

    nread = fread(buffer, 1u, (size_t)size, fp);
    fclose(fp);
    if (nread != (size_t)size) {
        free(buffer);
        return AGENT_ERROR;
    }

    buffer[nread] = '\0';
    *buffer_out = buffer;
    return AGENT_OK;
}

static void json_escape(const char *in, char *out, size_t out_size)
{
    size_t used = 0u;

    if (!out || out_size == 0u) {
        return;
    }

    while (in && *in && used + 1u < out_size) {
        if ((*in == '"' || *in == '\\') && used + 2u < out_size) {
            out[used++] = '\\';
            out[used++] = *in++;
        } else if (*in == '\n' && used + 2u < out_size) {
            out[used++] = '\\';
            out[used++] = 'n';
            in++;
        } else if (*in == '\r' && used + 2u < out_size) {
            out[used++] = '\\';
            out[used++] = 'r';
            in++;
        } else if (*in == '\t' && used + 2u < out_size) {
            out[used++] = '\\';
            out[used++] = 't';
            in++;
        } else {
            out[used++] = *in++;
        }
    }
    out[used] = '\0';
}

static size_t json_escaped_length(const char *text)
{
    size_t len = 0u;

    if (!text) {
        return 0u;
    }

    while (*text) {
        if (*text == '"' || *text == '\\' ||
            *text == '\n' || *text == '\r' || *text == '\t') {
            len += 2u;
        } else {
            len++;
        }
        text++;
    }

    return len;
}

static const char *find_json_value(const char *json, const char *key)
{
    const char *p;

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

    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    return p;
}

static int json_get_string(const char *json,
                           const char *key,
                           char *out,
                           size_t out_size)
{
    const char *p = find_json_value(json, key);
    size_t used = 0u;

    if (!p || !out || out_size == 0u || *p != '"') {
        return AGENT_ERROR_INVALID;
    }

    p++;
    while (*p && *p != '"' && used + 1u < out_size) {
        out[used++] = *p++;
    }
    out[used] = '\0';

    return *p == '"' ? AGENT_OK : AGENT_ERROR_LIMIT;
}

void smart_home_skill_store_init(smart_home_skill_store_t *store)
{
    if (store) {
        memset(store, 0, sizeof(*store));
    }
}

void smart_home_skill_store_deinit(smart_home_skill_store_t *store)
{
    size_t i;

    if (!store) {
        return;
    }

    for (i = 0u; i < store->count; i++) {
        free(store->skills[i].buffer);
    }
    memset(store, 0, sizeof(*store));
}

const smart_home_loaded_skill_t *smart_home_skill_store_find(
    const smart_home_skill_store_t *store,
    const char *name)
{
    size_t i;

    if (!store || !name) {
        return NULL;
    }

    for (i = 0u; i < store->count; i++) {
        if (store->skills[i].name &&
            strcmp(store->skills[i].name, name) == 0) {
            return &store->skills[i];
        }
    }

    return NULL;
}

int smart_home_skill_loader_load_dir(agent_t *agent,
                                     smart_home_skill_store_t *store,
                                     const char *dir)
{
    DIR *dp;
    struct dirent *entry;
    size_t loaded_count = 0u;

    if (!agent || !store || !dir) {
        return AGENT_ERROR_INVALID;
    }

    dp = opendir(dir);
    if (!dp) {
        return AGENT_ERROR_NOTFOUND;
    }

    while ((entry = readdir(dp)) != NULL) {
        smart_home_loaded_skill_t *loaded;
        agent_skill_t skill;
        char path[256];
        char *buffer;
        int ret;

        if (!has_md_suffix(entry->d_name)) {
            continue;
        }
        if (store->count >= SMART_HOME_SKILL_STORE_MAX) {
            closedir(dp);
            return AGENT_ERROR_LIMIT;
        }

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        ret = read_file(path, &buffer);
        if (ret != AGENT_OK) {
            closedir(dp);
            return ret;
        }

        loaded = &store->skills[store->count];
        memset(loaded, 0, sizeof(*loaded));
        loaded->buffer = buffer;
        ret = parse_front_matter(buffer, loaded, &skill);
        if (ret != AGENT_OK) {
            free(buffer);
            closedir(dp);
            return ret;
        }

        ret = agent_register_skill(agent, &skill);
        if (ret != AGENT_OK) {
            free(buffer);
            closedir(dp);
            return ret;
        }

        store->count++;
        loaded_count++;
    }

    closedir(dp);
    return loaded_count > 0u ? AGENT_OK : AGENT_ERROR_NOTFOUND;
}

int smart_home_skill_read_tool(const agent_tool_call_t *call,
                               agent_tool_result_t *result,
                               void *user_data)
{
    static char output[CAGENT_TOOL_OUTPUT_MAX_SIZE];
    char name[64] = "";
    char escaped_name[128];
    char escaped_content[CAGENT_TOOL_OUTPUT_MAX_SIZE];
    const smart_home_skill_store_t *store =
        (const smart_home_skill_store_t *)user_data;
    const smart_home_loaded_skill_t *skill;
    int ret;
    int written;

    if (!result) {
        return AGENT_ERROR_INVALID;
    }

    ret = json_get_string(call ? call->arguments_json : NULL,
                          "\"name\"",
                          name,
                          sizeof(name));
    if (ret != AGENT_OK) {
        result->status = ret;
        result->content_json = "{\"ok\":false}";
        result->error_message = "read_skill failed";
        return ret;
    }

    skill = smart_home_skill_store_find(store, name);
    if (!skill || !skill->context_text) {
        result->status = AGENT_ERROR_NOTFOUND;
        result->content_json = "{\"ok\":false,\"error\":\"skill not found\"}";
        result->error_message = "skill not found";
        return result->status;
    }

    if (json_escaped_length(skill->context_text) + 1u >
        sizeof(escaped_content)) {
        result->status = AGENT_ERROR_LIMIT;
        result->content_json = "{\"ok\":false,\"error\":\"skill too large\"}";
        result->error_message = "skill too large";
        return result->status;
    }

    json_escape(name, escaped_name, sizeof(escaped_name));
    json_escape(skill->context_text,
                escaped_content,
                sizeof(escaped_content));
    written = snprintf(output,
                       sizeof(output),
                       "{\"ok\":true,\"name\":\"%s\",\"content\":\"%s\"}",
                       escaped_name,
                       escaped_content);
    if (written < 0 || (size_t)written >= sizeof(output)) {
        result->status = AGENT_ERROR_LIMIT;
        result->content_json = "{\"ok\":false,\"error\":\"skill too large\"}";
        result->error_message = "skill too large";
        return result->status;
    }

    result->status = AGENT_OK;
    result->content_json = output;
    result->error_message = NULL;
    return AGENT_OK;
}
