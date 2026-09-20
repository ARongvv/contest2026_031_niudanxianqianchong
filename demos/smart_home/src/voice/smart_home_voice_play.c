/****************************************************************************
 * demos/smart_home/src/voice/smart_home_voice_play.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "smart_home_voice_play.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../smart_home_memory.h"
#include "smart_home_tts.h"
#include "smart_home_voice_player.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_SMART_HOME_VOICE_TEXT_MAX
#define CONFIG_SMART_HOME_VOICE_TEXT_MAX 512
#endif

/* worker 栈走 bulk（PSRAM）堆，与 agent worker 同策略；mbedTLS 合成
 * 需要 32 KiB 量级。 */
#ifndef CONFIG_SMART_HOME_VOICE_WORKER_STACKSIZE
#define CONFIG_SMART_HOME_VOICE_WORKER_STACKSIZE 32768
#endif

#define VOICE_WORKER_PRIORITY 120  /* 低于 LVGL(100) 与 demo 主任务 */

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct voice_job_s
{
  struct voice_job_s *next;
  char text[CONFIG_SMART_HOME_VOICE_TEXT_MAX];
} voice_job_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_voice_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_voice_cond = PTHREAD_COND_INITIALIZER;
static voice_job_t *g_voice_queue;
static voice_job_t *g_voice_queue_tail;
static voice_play_state_t g_voice_state = VOICE_PLAY_IDLE;
static smart_home_voice_abort_t g_voice_abort;
static pthread_t g_voice_worker;
static void *g_voice_worker_stack;
static bool g_voice_started;
static bool g_voice_stop_requested;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *voice_worker_main(void *arg)
{
  voice_job_t *job;
  uint8_t *pcm;
  size_t bytes;
  uint32_t rate;
  int ret;

  (void)arg;
  pthread_mutex_lock(&g_voice_mutex);
  while (!g_voice_stop_requested)
    {
      while (g_voice_queue == NULL && !g_voice_stop_requested)
        {
          pthread_cond_wait(&g_voice_cond, &g_voice_mutex);
        }

      if (g_voice_stop_requested)
        {
          break;
        }

      job = g_voice_queue;
      g_voice_queue = job->next;
      if (g_voice_queue == NULL)
        {
          g_voice_queue_tail = NULL;
        }

      g_voice_state = VOICE_PLAY_SYNTHESIZING;
      g_voice_abort.abort_flag = 0;
      pthread_mutex_unlock(&g_voice_mutex);

      pcm = NULL;
      bytes = 0;
      ret = smart_home_tts_synth(job->text, &pcm, &bytes, &rate);
      if (ret == 0)
        {
          pthread_mutex_lock(&g_voice_mutex);
          bool cancelled = g_voice_abort.abort_flag != 0;
          g_voice_state = VOICE_PLAY_PLAYING;
          pthread_mutex_unlock(&g_voice_mutex);

          if (!cancelled)
            {
              smart_home_voice_player_play_mem(pcm, bytes, rate,
                                               &g_voice_abort);
            }

          smart_home_bulk_free(pcm);
        }
      else
        {
          fprintf(stderr, "voice: tts synth failed: %d\n", ret);
        }

      free(job);

      pthread_mutex_lock(&g_voice_mutex);
      g_voice_state = VOICE_PLAY_IDLE;
    }

  pthread_mutex_unlock(&g_voice_mutex);
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int voice_play_init(void)
{
  pthread_attr_t attr;
  int attr_ready = 0;
  int ret;

  pthread_mutex_lock(&g_voice_mutex);
  if (g_voice_started)
    {
      pthread_mutex_unlock(&g_voice_mutex);
      return 0;
    }

  /* 预热配置缓存，播报开关立即可查询。 */
  smart_home_tts_config_t config;
  smart_home_tts_config_load(&config);

  g_voice_stop_requested = false;
  g_voice_queue = NULL;
  g_voice_queue_tail = NULL;
  g_voice_state = VOICE_PLAY_IDLE;
  g_voice_abort.abort_flag = 0;
  pthread_mutex_unlock(&g_voice_mutex);

  g_voice_worker_stack = smart_home_bulk_alloc(
      CONFIG_SMART_HOME_VOICE_WORKER_STACKSIZE + 64u);
  if (g_voice_worker_stack == NULL)
    {
      return -ENOMEM;
    }

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      attr_ready = 1;
      ret = pthread_attr_setstack(&attr, g_voice_worker_stack,
                                  CONFIG_SMART_HOME_VOICE_WORKER_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedparam(&attr,
          &(struct sched_param)
          {
            .sched_priority = VOICE_WORKER_PRIORITY
          });
    }

  if (ret == 0)
    {
      ret = pthread_create(&g_voice_worker, &attr, voice_worker_main, NULL);
    }

  if (attr_ready)
    {
      pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      smart_home_bulk_free(g_voice_worker_stack);
      g_voice_worker_stack = NULL;
      return -ret;
    }

  pthread_mutex_lock(&g_voice_mutex);
  g_voice_started = true;
  pthread_mutex_unlock(&g_voice_mutex);
  return 0;
}

void voice_play_deinit(void)
{
  voice_job_t *job;
  voice_job_t *next;

  pthread_mutex_lock(&g_voice_mutex);
  if (!g_voice_started)
    {
      pthread_mutex_unlock(&g_voice_mutex);
      return;
    }

  g_voice_stop_requested = true;
  g_voice_abort.abort_flag = 1;
  pthread_cond_broadcast(&g_voice_cond);
  pthread_mutex_unlock(&g_voice_mutex);

  pthread_join(g_voice_worker, NULL);

  pthread_mutex_lock(&g_voice_mutex);
  job = g_voice_queue;
  g_voice_queue = NULL;
  g_voice_queue_tail = NULL;
  g_voice_started = false;
  g_voice_state = VOICE_PLAY_IDLE;
  pthread_mutex_unlock(&g_voice_mutex);

  while (job != NULL)
    {
      next = job->next;
      free(job);
      job = next;
    }

  smart_home_bulk_free(g_voice_worker_stack);
  g_voice_worker_stack = NULL;
}

int voice_play_text(const char *text)
{
  voice_job_t *job;
  size_t length;

  if (text == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  length = strlen(text);
  if (length >= CONFIG_SMART_HOME_VOICE_TEXT_MAX)
    {
      length = CONFIG_SMART_HOME_VOICE_TEXT_MAX - 1;
    }

  pthread_mutex_lock(&g_voice_mutex);
  if (!g_voice_started)
    {
      pthread_mutex_unlock(&g_voice_mutex);
      return -EAGAIN;
    }

  job = calloc(1, sizeof(*job));
  if (job == NULL)
    {
      pthread_mutex_unlock(&g_voice_mutex);
      return -ENOMEM;
    }

  memcpy(job->text, text, length);
  job->text[length] = '\0';

  job->next = NULL;
  if (g_voice_queue_tail != NULL)
    {
      g_voice_queue_tail->next = job;
    }
  else
    {
      g_voice_queue = job;
    }

  g_voice_queue_tail = job;
  pthread_cond_signal(&g_voice_cond);
  pthread_mutex_unlock(&g_voice_mutex);
  return 0;
}

void voice_play_stop(void)
{
  voice_job_t *job;
  voice_job_t *next;

  pthread_mutex_lock(&g_voice_mutex);
  if (!g_voice_started)
    {
      pthread_mutex_unlock(&g_voice_mutex);
      return;
    }

  g_voice_abort.abort_flag = 1;
  job = g_voice_queue;
  g_voice_queue = NULL;
  g_voice_queue_tail = NULL;
  pthread_cond_broadcast(&g_voice_cond);
  pthread_mutex_unlock(&g_voice_mutex);

  while (job != NULL)
    {
      next = job->next;
      free(job);
      job = next;
    }
}

voice_play_state_t voice_play_state(void)
{
  return g_voice_state;
}

int voice_play_announce_enabled(void)
{
  smart_home_tts_config_t config;

  smart_home_tts_config_load(&config);
  return config.enabled;
}
