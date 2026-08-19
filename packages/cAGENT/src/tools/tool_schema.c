/* SPDX-License-Identifier: Apache-2.0 */
/**
 * Tool schema builder.
 *
 * Converts enabled and LLM-visible registered tools into an
 * OpenAI-compatible tools JSON array. The generated schema is cached inside
 * agent_t and invalidated by registry mutations.
 */

#include "tools_internal.h"

#include "../core/agent_internal.h"
#include "../types_internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define JSON_MAX_DEPTH 24

typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} json_writer_t;

static int writer_putc(json_writer_t *writer, char c)
{
    if (!writer || !writer->buffer || writer->size == 0u) {
        return AGENT_ERROR_INVALID;
    }
    if (writer->used + 1u >= writer->size) {
        return AGENT_ERROR_LIMIT;
    }

    writer->buffer[writer->used++] = c;
    writer->buffer[writer->used] = '\0';
    return AGENT_OK;
}

static int writer_append(json_writer_t *writer, const char *text)
{
    size_t n;

    if (!text) {
        return AGENT_OK;
    }

    n = strlen(text);
    if (!writer || !writer->buffer || writer->used + n >= writer->size) {
        return AGENT_ERROR_LIMIT;
    }

    memcpy(writer->buffer + writer->used, text, n);
    writer->used += n;
    writer->buffer[writer->used] = '\0';
    return AGENT_OK;
}

static int writer_printf(json_writer_t *writer, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!writer || !writer->buffer || writer->size == 0u) {
        return AGENT_ERROR_INVALID;
    }
    if (writer->used >= writer->size) {
        return AGENT_ERROR_LIMIT;
    }

    va_start(ap, fmt);
    n = vsnprintf(writer->buffer + writer->used,
                  writer->size - writer->used,
                  fmt,
                  ap);
    va_end(ap);

    if (n < 0) {
        return AGENT_ERROR;
    }
    if ((size_t)n >= writer->size - writer->used) {
        writer->buffer[writer->size - 1u] = '\0';
        return AGENT_ERROR_LIMIT;
    }

    writer->used += (size_t)n;
    return AGENT_OK;
}

static int writer_append_json_string(json_writer_t *writer, const char *text)
{
    int ret = writer_putc(writer, '"');

    if (ret != AGENT_OK) {
        return ret;
    }

    if (text) {
        while (*text) {
            unsigned char c = (unsigned char)*text++;
            switch (c) {
            case '"':
                ret = writer_append(writer, "\\\"");
                break;
            case '\\':
                ret = writer_append(writer, "\\\\");
                break;
            case '\n':
                ret = writer_append(writer, "\\n");
                break;
            case '\r':
                ret = writer_append(writer, "\\r");
                break;
            case '\t':
                ret = writer_append(writer, "\\t");
                break;
            default:
                if (c < 0x20u) {
                    ret = writer_printf(writer, "\\u%04x", c);
                } else {
                    ret = writer_putc(writer, (char)c);
                }
                break;
            }
            if (ret != AGENT_OK) {
                return ret;
            }
        }
    }

    return writer_putc(writer, '"');
}

static const char *skip_ws(const char *s)
{
    while (s && *s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static bool is_hex4(const char *s)
{
    int i;

    if (!s) {
        return false;
    }

    for (i = 0; i < 4; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

static const char *json_parse_value(const char *s, int depth);

static const char *json_parse_string(const char *s)
{
    if (!s || *s != '"') {
        return NULL;
    }

    s++;
    while (*s) {
        unsigned char c = (unsigned char)*s++;

        if (c == '"') {
            return s;
        }
        if (c < 0x20u) {
            return NULL;
        }
        if (c == '\\') {
            char esc = *s++;
            switch (esc) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u':
                if (!is_hex4(s)) {
                    return NULL;
                }
                s += 4;
                break;
            default:
                return NULL;
            }
        }
    }

    return NULL;
}

static const char *json_parse_number(const char *s)
{
    if (!s) {
        return NULL;
    }

    if (*s == '-') {
        s++;
    }

    if (*s == '0') {
        s++;
    } else if (*s >= '1' && *s <= '9') {
        while (isdigit((unsigned char)*s)) {
            s++;
        }
    } else {
        return NULL;
    }

    if (*s == '.') {
        s++;
        if (!isdigit((unsigned char)*s)) {
            return NULL;
        }
        while (isdigit((unsigned char)*s)) {
            s++;
        }
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') {
            s++;
        }
        if (!isdigit((unsigned char)*s)) {
            return NULL;
        }
        while (isdigit((unsigned char)*s)) {
            s++;
        }
    }

    return s;
}

static const char *json_parse_array(const char *s, int depth)
{
    if (!s || *s != '[' || depth >= JSON_MAX_DEPTH) {
        return NULL;
    }

    s = skip_ws(s + 1);
    if (*s == ']') {
        return s + 1;
    }

    while (*s) {
        s = json_parse_value(s, depth + 1);
        if (!s) {
            return NULL;
        }

        s = skip_ws(s);
        if (*s == ',') {
            s = skip_ws(s + 1);
            continue;
        }
        if (*s == ']') {
            return s + 1;
        }
        return NULL;
    }

    return NULL;
}

static const char *json_parse_object(const char *s, int depth)
{
    if (!s || *s != '{' || depth >= JSON_MAX_DEPTH) {
        return NULL;
    }

    s = skip_ws(s + 1);
    if (*s == '}') {
        return s + 1;
    }

    while (*s) {
        s = json_parse_string(s);
        if (!s) {
            return NULL;
        }

        s = skip_ws(s);
        if (*s != ':') {
            return NULL;
        }

        s = skip_ws(s + 1);
        s = json_parse_value(s, depth + 1);
        if (!s) {
            return NULL;
        }

        s = skip_ws(s);
        if (*s == ',') {
            s = skip_ws(s + 1);
            continue;
        }
        if (*s == '}') {
            return s + 1;
        }
        return NULL;
    }

    return NULL;
}

static const char *json_parse_value(const char *s, int depth)
{
    s = skip_ws(s);
    if (!s || depth >= JSON_MAX_DEPTH) {
        return NULL;
    }

    switch (*s) {
    case '"':
        return json_parse_string(s);
    case '{':
        return json_parse_object(s, depth);
    case '[':
        return json_parse_array(s, depth);
    case 't':
        return strncmp(s, "true", 4) == 0 ? s + 4 : NULL;
    case 'f':
        return strncmp(s, "false", 5) == 0 ? s + 5 : NULL;
    case 'n':
        return strncmp(s, "null", 4) == 0 ? s + 4 : NULL;
    default:
        if (*s == '-' || isdigit((unsigned char)*s)) {
            return json_parse_number(s);
        }
        return NULL;
    }
}

static bool validate_schema_object(const char *schema)
{
    const char *s = skip_ws(schema);
    const char *end;

    if (!s || *s == '\0') {
        return true;
    }
    if (*s != '{') {
        return false;
    }

    end = json_parse_object(s, 0);
    if (!end) {
        return false;
    }

    end = skip_ws(end);
    return *end == '\0';
}

static int append_parameters(json_writer_t *writer, const char *schema)
{
    const char *trimmed = skip_ws(schema);

    if (!trimmed || trimmed[0] == '\0') {
        return writer_append(writer,
                             "{\"type\":\"object\",\"properties\":{},"
                             "\"additionalProperties\":false}");
    }

    if (!validate_schema_object(trimmed)) {
        return AGENT_ERROR_PARSE;
    }

    return writer_append(writer, trimmed);
}

static int build_schema_uncached(agent_t *agent,
                                 char *buffer,
                                 size_t buffer_size,
                                 size_t *written)
{
    json_writer_t writer;
    bool first = true;
    uint32_t i;
    int ret;

    writer.buffer = buffer;
    writer.size = buffer_size;
    writer.used = 0u;
    buffer[0] = '\0';

    ret = writer_putc(&writer, '[');
    if (ret != AGENT_OK) {
        return ret;
    }

    for (i = 0u; i < agent->tool_count; i++) {
        const agent_tool_entry_t *entry = &agent->tools[i];
        const agent_tool_t *tool = &entry->def;

        if (!agent_tool_entry_is_enabled(entry) ||
            !agent_tool_entry_is_llm_visible(entry)) {
            continue;
        }

        if (!first) {
            ret = writer_putc(&writer, ',');
            if (ret != AGENT_OK) {
                return ret;
            }
        }
        first = false;

        ret = writer_append(&writer, "{\"type\":\"function\",\"function\":{\"name\":");
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = writer_append_json_string(&writer, tool->name);
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = writer_append(&writer, ",\"description\":");
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = writer_append_json_string(&writer,
                                        tool->description ? tool->description : "");
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = writer_append(&writer, ",\"parameters\":");
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = append_parameters(&writer, tool->input_schema_json);
        if (ret != AGENT_OK) {
            return ret;
        }
        ret = writer_append(&writer, "}}");
        if (ret != AGENT_OK) {
            return ret;
        }
    }

    ret = writer_putc(&writer, ']');
    if (ret != AGENT_OK) {
        return ret;
    }

    if (written) {
        *written = writer.used;
    }
    return AGENT_OK;
}

void agent_tool_schema_mark_dirty(agent_t *agent)
{
    if (!agent) {
        return;
    }

    agent->tool_schema_dirty = true;
    agent->tool_schema_version++;
}

static int copy_schema_cache(agent_t *agent,
                             char *buffer,
                             size_t buffer_size,
                             size_t *written)
{
    if (agent->tool_schema_cache_len + 1u > buffer_size) {
        if (buffer_size > 0u) {
            buffer[0] = '\0';
        }
        return AGENT_ERROR_LIMIT;
    }

    memcpy(buffer, agent->tool_schema_cache, agent->tool_schema_cache_len + 1u);
    if (written) {
        *written = agent->tool_schema_cache_len;
    }
    return AGENT_OK;
}

int agent_tool_schema_build(agent_t *agent,
                            char *buffer,
                            size_t buffer_size,
                            size_t *written)
{
    int ret;
    size_t cache_len = 0u;

    if (!agent || !buffer || buffer_size == 0u) {
        return AGENT_ERROR_INVALID;
    }

    if (!agent->tool_schema_dirty && agent->tool_schema_cache_len > 0u) {
        return copy_schema_cache(agent, buffer, buffer_size, written);
    }

    ret = build_schema_uncached(agent,
                                agent->tool_schema_cache,
                                sizeof(agent->tool_schema_cache),
                                &cache_len);
    if (ret != AGENT_OK) {
        buffer[0] = '\0';
        return ret;
    }

    agent->tool_schema_cache_len = cache_len;
    agent->tool_schema_dirty = false;

    return copy_schema_cache(agent, buffer, buffer_size, written);
}
