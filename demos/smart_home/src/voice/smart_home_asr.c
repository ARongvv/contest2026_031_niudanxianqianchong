/****************************************************************************
 * demos/smart_home/src/voice/smart_home_asr.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "smart_home_asr.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config/cjson_compat.h"
#include "../config/smart_home_config_store.h"
#include "../config/smart_home_secrets.h"
#include "../smart_home_memory.h"

#include <cagent/runtime.h>
#include "runtime_openvela.h"

#include "mbedtls/base64.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ASR_CONFIG_PATH  SMART_HOME_CONFIG_DIR "/asr.json"
#define ASR_KEY_MAX      256
#define ASR_TIMEOUT_MS   30000u

/* MiMo ASR 固定端点；与 LLM/TTS 的 mimo 预设同 host 同 key。 */
#define ASR_HOST         "api.xiaomimimo.com"
#define ASR_PATH         "/v1/chat/completions"
#define ASR_PORT         "443"
#define ASR_MODEL        "mimo-v2.5-asr"
#define ASR_BACKEND_ID   "mimo"

/* 请求体前后缀（base64 音频之外的固定部分）。 */
#define ASR_BODY_PREFIX \
  "{\"model\":\"" ASR_MODEL "\"," \
  "\"messages\":[{\"role\":\"user\",\"content\":[" \
  "{\"type\":\"input_audio\",\"input_audio\":{" \
  "\"data\":\"data:audio/wav;base64,"

#define ASR_BODY_SUFFIX \
  "\",\"format\":\"wav\"}}]}]," \
  "\"asr_options\":{\"language\":\""

/* 响应文本缓冲：错误详情按 512 字节截断打印。 */
#define ASR_RESPONSE_MAX 4096

#ifndef CONFIG_SMART_HOME_ASR_AUDIO_MAX_SECONDS
#define CONFIG_SMART_HOME_ASR_AUDIO_MAX_SECONDS 12
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static smart_home_asr_config_t g_asr_config;
static pthread_mutex_t g_asr_config_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_asr_config_loaded;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static cJSON *asr_config_make_default(void *user_data)
{
  cJSON *root;

  (void)user_data;
  root = cJSON_CreateObject();
  if (root == NULL ||
      !cJSON_AddNumberToObject(root, "version", 1) ||
      !cJSON_AddBoolToObject(root, "enabled", 0) ||
      !cJSON_AddStringToObject(root, "language", "zh"))
    {
      cJSON_Delete(root);
      return NULL;
    }

  return root;
}

static void asr_config_read(const cJSON *root,
                            smart_home_asr_config_t *config)
{
  const cJSON *item;

  memset(config, 0, sizeof(*config));
  strcpy(config->language, "zh");

  item = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  config->enabled = cJSON_IsTrue(item);

  item = cJSON_GetObjectItemCaseSensitive(root, "language");
  if (cJSON_IsString(item) && item->valuestring[0])
    {
      strncpy(config->language, item->valuestring,
              sizeof(config->language) - 1);
    }
}

static int asr_config_validate(const cJSON *root, void *user_data)
{
  smart_home_asr_config_t config;

  (void)user_data;
  asr_config_read(root, &config);
  if (strcmp(config.language, "zh") != 0 &&
      strcmp(config.language, "en") != 0 &&
      strcmp(config.language, "auto") != 0)
    {
      return AGENT_ERROR_PARSE;
    }

  return AGENT_OK;
}

static void asr_config_ensure_loaded(void)
{
  cJSON *root = NULL;
  char error[96];

  pthread_mutex_lock(&g_asr_config_mutex);
  if (g_asr_config_loaded)
    {
      pthread_mutex_unlock(&g_asr_config_mutex);
      return;
    }

  if (smart_home_config_store_load(ASR_CONFIG_PATH,
                                   asr_config_make_default,
                                   SMART_HOME_CONFIG_RECOVER_DEFAULT,
                                   asr_config_validate, NULL,
                                   &root, error, sizeof(error)) == AGENT_OK)
    {
      asr_config_read(root, &g_asr_config);
      cJSON_Delete(root);
    }
  else
    {
      fprintf(stderr, "asr: config load failed: %s\n", error);
      g_asr_config.enabled = 0;
      strcpy(g_asr_config.language, "zh");
    }

  g_asr_config_loaded = 1;
  pthread_mutex_unlock(&g_asr_config_mutex);
}

static void asr_put_u32le(uint8_t *dst, uint32_t value)
{
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

static void asr_put_u16le(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
}

/* 组装 44 字节 PCM16 单声道 WAV 头（fmt 在 data 前的单块布局）。 */
static void asr_make_wav_header(uint8_t *header, uint32_t data_bytes,
                                uint32_t rate)
{
  memcpy(header + 0, "RIFF", 4);
  asr_put_u32le(header + 4, 36u + data_bytes);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  asr_put_u32le(header + 16, 16u);
  asr_put_u16le(header + 20, 1u);           /* PCM */
  asr_put_u16le(header + 22, 1u);           /* mono */
  asr_put_u32le(header + 24, rate);
  asr_put_u32le(header + 28, rate * 2u);    /* byte rate */
  asr_put_u16le(header + 32, 2u);           /* block align */
  asr_put_u16le(header + 34, 16u);          /* bits */
  memcpy(header + 36, "data", 4);
  asr_put_u32le(header + 40, data_bytes);
}

/* 响应解析：choices[0].message.content 为转写文本。 */
static int asr_parse_response(const char *body, size_t body_len,
                              char *out_text, size_t out_size)
{
  const cJSON *choices;
  const cJSON *choice;
  const cJSON *message;
  const cJSON *content;
  cJSON *root;

  if (body_len == 0 || out_size == 0)
    {
      return -EINVAL;
    }

  root = cJSON_Parse(body);
  if (root == NULL)
    {
      return -EINVAL;
    }

  choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  choice = cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0
               ? cJSON_GetArrayItem(choices, 0)
               : NULL;
  message = choice != NULL
                ? cJSON_GetObjectItemCaseSensitive(choice, "message")
                : NULL;
  content = message != NULL
                ? cJSON_GetObjectItemCaseSensitive(message, "content")
                : NULL;

  if (!cJSON_IsString(content) || content->valuestring == NULL ||
      content->valuestring[0] == '\0')
    {
      fprintf(stderr, "asr: response has no message.content\n");
      cJSON_Delete(root);
      return -EINVAL;
    }

  strncpy(out_text, content->valuestring, out_size - 1);
  out_text[out_size - 1] = '\0';
  cJSON_Delete(root);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void smart_home_asr_config_load(smart_home_asr_config_t *config)
{
  if (config == NULL)
    {
      return;
    }

  asr_config_ensure_loaded();
  pthread_mutex_lock(&g_asr_config_mutex);
  memcpy(config, &g_asr_config, sizeof(*config));
  pthread_mutex_unlock(&g_asr_config_mutex);
}

int smart_home_asr_config_save(const smart_home_asr_config_t *config)
{
  cJSON *root;
  int ret;

  if (config == NULL)
    {
      return AGENT_ERROR_INVALID;
    }

  root = cJSON_CreateObject();
  if (root == NULL ||
      !cJSON_AddNumberToObject(root, "version", 1) ||
      !cJSON_AddBoolToObject(root, "enabled", config->enabled ? 1 : 0) ||
      !cJSON_AddStringToObject(root, "language", config->language))
    {
      cJSON_Delete(root);
      return AGENT_ERROR_NOMEM;
    }

  ret = smart_home_config_store_save(ASR_CONFIG_PATH, root);
  cJSON_Delete(root);

  if (ret == AGENT_OK)
    {
      asr_config_ensure_loaded();
      pthread_mutex_lock(&g_asr_config_mutex);
      memcpy(&g_asr_config, config, sizeof(g_asr_config));
      pthread_mutex_unlock(&g_asr_config_mutex);
    }

  return ret;
}

int smart_home_asr_recognize(const uint8_t *pcm, size_t bytes,
                             char *out_text, size_t out_size)
{
  smart_home_asr_config_t config;
  agent_runtime_t runtime;
  agent_http_request_t request;
  agent_http_response_t response;
  char api_key[ASR_KEY_MAX];
  char auth_header[ASR_KEY_MAX + 32];
  uint8_t *wav = NULL;
  char *body = NULL;
  uint8_t *response_buf = NULL;
  size_t prefix_len;
  size_t suffix_len;
  size_t b64_len;
  size_t olen = 0;
  int ret;

  if (pcm == NULL || bytes == 0 || out_text == NULL || out_size == 0)
    {
      return AGENT_ERROR_INVALID;
    }

  out_text[0] = '\0';
  smart_home_asr_config_load(&config);

  ret = smart_home_secrets_get_model_api_key(ASR_BACKEND_ID,
                                             api_key, sizeof(api_key));
  if (ret != AGENT_OK)
    {
      fprintf(stderr, "asr: no api key for backend '%s' in secrets.json\n",
              ASR_BACKEND_ID);
      return ret;
    }

  /* WAV 组装：44 字节头 + 原始 PCM。 */
  wav = smart_home_bulk_alloc(bytes + 44u);
  if (wav == NULL)
    {
      return AGENT_ERROR_NOMEM;
    }

  asr_make_wav_header(wav, (uint32_t)bytes, 16000u);
  memcpy(wav + 44, pcm, bytes);

  /* 请求体 = 前缀 + base64(WAV) + 语言后缀 + 收尾。
   * 大字段手工拼接而非 cJSON，避免 4 倍峰值内存。 */
  prefix_len = strlen(ASR_BODY_PREFIX);
  b64_len = ((bytes + 44u + 2u) / 3u) * 4u + 4u;
  suffix_len = strlen(ASR_BODY_SUFFIX) + strlen(config.language) + 4u;

  body = smart_home_bulk_alloc(prefix_len + b64_len + suffix_len + 1u);
  if (body == NULL)
    {
      smart_home_bulk_free(wav);
      return AGENT_ERROR_NOMEM;
    }

  memcpy(body, ASR_BODY_PREFIX, prefix_len);
  ret = mbedtls_base64_encode((unsigned char *)body + prefix_len, b64_len,
                              &olen, wav, bytes + 44u);
  smart_home_bulk_free(wav);
  if (ret != 0)
    {
      fprintf(stderr, "asr: base64 encode failed: -0x%04x\n", -ret);
      smart_home_bulk_free(body);
      return AGENT_ERROR_NOMEM;
    }

  sprintf(body + prefix_len + olen, ASR_BODY_SUFFIX "%s\"}}",
          config.language);

  snprintf(auth_header, sizeof(auth_header),
           "Authorization: Bearer %s\r\n"
           "Content-Type: application/json\r\n",
           api_key);

  memset(&runtime, 0, sizeof(runtime));
  agent_runtime_openvela_fill(&runtime);
  if (runtime.http_post == NULL)
    {
      fprintf(stderr, "asr: runtime has no http_post (no TLS profile)\n");
      smart_home_bulk_free(body);
      return AGENT_ERROR_NOTSUP;
    }

  response_buf = smart_home_bulk_alloc(ASR_RESPONSE_MAX);
  if (response_buf == NULL)
    {
      smart_home_bulk_free(body);
      return AGENT_ERROR_NOMEM;
    }

  memset(&request, 0, sizeof(request));
  request.method = "POST";
  request.host = ASR_HOST;
  request.path = ASR_PATH;
  request.port = ASR_PORT;
  request.headers = auth_header;
  request.body = body;
  request.body_size = prefix_len + olen +
                      strlen(body + prefix_len + olen);
  request.timeout_ms = ASR_TIMEOUT_MS;

  memset(&response, 0, sizeof(response));
  response.body = (char *)response_buf;
  response.body_size = ASR_RESPONSE_MAX;

  ret = runtime.http_post(&request, &response, runtime.user_data);
  smart_home_bulk_free(body);

  if (ret != AGENT_OK)
    {
      fprintf(stderr, "asr: http failed: %d\n", ret);
      goto out;
    }

  if (response.status_code < 200 || response.status_code >= 300)
    {
      fprintf(stderr, "asr: http status=%d body: %.200s\n",
              response.status_code,
              response.bytes_written > 0 ? (const char *)response_buf :
                                           "(empty)");
      ret = AGENT_ERROR_MODEL;
      goto out;
    }

  if (response.bytes_written >= ASR_RESPONSE_MAX)
    {
      response.bytes_written = ASR_RESPONSE_MAX - 1;
    }

  response_buf[response.bytes_written] = '\0';
  ret = asr_parse_response((const char *)response_buf,
                           response.bytes_written, out_text, out_size);
  if (ret != 0)
    {
      ret = AGENT_ERROR_PARSE;
    }

out:
  smart_home_bulk_free(response_buf);
  return ret;
}
