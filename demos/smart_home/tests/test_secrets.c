/* 专用 secrets loader 的 host 回归测试：只读、失败清空、按 backend 隔离。 */

#include "smart_home_secrets.h"

#include <cagent/types.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *text)
{
    FILE *file = fopen(CONFIG_SMART_HOME_MODEL_SECRETS_PATH, "wb");

    if (!file) {
        assert(mkdir("/tmp/smh_cfg_test_path", 0700) == 0 ||
               errno == EEXIST);
        file = fopen(CONFIG_SMART_HOME_MODEL_SECRETS_PATH, "wb");
    }
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void expect_cleared(const char *buffer, size_t size)
{
    size_t i;

    for (i = 0u; i < size; i++) {
        assert(buffer[i] == '\0');
    }
}

static void test_valid_lookup_and_isolation(void)
{
    char key[64];

    write_text("{\"version\":1,\"model_api_keys\":{\"deepseek\":\"deep-key\",\"mimo\":\"mimo-key\"}}");
    assert(smart_home_secrets_get_model_api_key("mimo", key, sizeof(key)) ==
           AGENT_OK);
    assert(strcmp(key, "mimo-key") == 0);
    assert(smart_home_secrets_model_api_key_status("deepseek") == AGENT_OK);
    assert(smart_home_secrets_model_api_key_status("qwen") ==
           AGENT_ERROR_NOTFOUND);
}

static void test_missing_or_empty_key_fails_closed(void)
{
    char key[32];

    write_text("{\"version\":1,\"model_api_keys\":{\"mimo\":\"\"}}");
    memset(key, 0xa5, sizeof(key));
    assert(smart_home_secrets_get_model_api_key("mimo", key, sizeof(key)) ==
           AGENT_ERROR_NOTFOUND);
    expect_cleared(key, sizeof(key));
}

static void test_invalid_content_fails_closed(void)
{
    char key[32];

    write_text("{not json");
    memset(key, 0xa5, sizeof(key));
    assert(smart_home_secrets_get_model_api_key("mimo", key, sizeof(key)) ==
           AGENT_ERROR_PARSE);
    expect_cleared(key, sizeof(key));

    write_text("{\"version\":2,\"model_api_keys\":{\"mimo\":\"key\"}}");
    assert(smart_home_secrets_model_api_key_status("mimo") == AGENT_ERROR_PARSE);

    write_text("{\"version\":1,\"model_api_keys\":{\"mimo\":\"bad\\nkey\"}}");
    assert(smart_home_secrets_model_api_key_status("mimo") == AGENT_ERROR_PARSE);
}

static void test_limits_and_missing_file(void)
{
    char key[4];

    write_text("{\"version\":1,\"model_api_keys\":{\"mimo\":\"long-key\"}}");
    assert(smart_home_secrets_get_model_api_key("mimo", key, sizeof(key)) ==
           AGENT_ERROR_LIMIT);
    expect_cleared(key, sizeof(key));

    assert(unlink(CONFIG_SMART_HOME_MODEL_SECRETS_PATH) == 0);
    assert(smart_home_secrets_model_api_key_status("mimo") ==
           AGENT_ERROR_NOTFOUND);
}

int main(void)
{
    test_valid_lookup_and_isolation();
    test_missing_or_empty_key_fails_closed();
    test_invalid_content_fails_closed();
    test_limits_and_missing_file();
    puts("test_secrets: PASS");
    return 0;
}
