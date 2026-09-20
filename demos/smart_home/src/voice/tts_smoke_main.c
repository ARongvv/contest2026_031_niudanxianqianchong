/****************************************************************************
 * demos/smart_home/src/voice/tts_smoke_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/* tts_smoke：语音播报链路的 NSH 冒烟命令。
 *
 * play  用共享播放封装播放一段原始 PCM16 单声道文件，验证
 *       /dev/audio/pcm0 在任意采样率下的出声基线。
 * speak 按 voice.json + secrets.json 的配置走一次真实云端合成
 *       （OpenAI 兼容 /v1/audio/speech）并播放，验证 TTS 全链路。
 *       不经过播报服务队列，便于单独定位合成问题。
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smart_home_tts.h"
#include "smart_home_voice_player.h"

#include "../smart_home_memory.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TTS_SMOKE_DEFAULT_RATE 16000
#define TTS_SMOKE_MAX_RATE     96000

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tts_smoke_usage(FAR const char *progname)
{
  printf("Usage:\n"
         "  %s play <pcm_path> [sample_rate]\n"
         "  %s speak <text>\n"
         "play: raw PCM16 mono file via the shared voice player "
         "(default rate=%d)\n"
         "speak: cloud TTS synthesis (voice.json + secrets.json) "
         "and playback\n",
         progname, progname, TTS_SMOKE_DEFAULT_RATE);
}

static int tts_smoke_parse_rate(FAR const char *text, uint32_t *rate)
{
  FAR char *end;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno != 0 || *text == '\0' || *end != '\0' || value == 0 ||
      value > TTS_SMOKE_MAX_RATE)
    {
      return -EINVAL;
    }

  *rate = (uint32_t)value;
  return 0;
}

static int tts_smoke_speak(FAR const char *text)
{
  uint8_t *pcm = NULL;
  size_t bytes = 0;
  uint32_t rate = 0;
  int ret;

  printf("tts_smoke: speak: %.60s\n", text);
  ret = smart_home_tts_synth(text, &pcm, &bytes, &rate);
  if (ret != 0)
    {
      fprintf(stderr, "tts_smoke: synth failed: %d\n", ret);
      return EXIT_FAILURE;
    }

    printf("tts_smoke: synth ok: bytes=%zu rate=%lu\n",
           bytes, (unsigned long)rate);

    ret = smart_home_voice_player_play_mem(pcm, bytes, rate, NULL);
    smart_home_bulk_free(pcm);
    if (ret == 0)
      {
        printf("tts_smoke: speak complete\n");
        return EXIT_SUCCESS;
      }

    fprintf(stderr, "tts_smoke: playback failed: %d (%s)\n",
            ret, strerror(-ret));
    return EXIT_FAILURE;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  uint32_t rate = TTS_SMOKE_DEFAULT_RATE;

  if (argc >= 3 && strcmp(argv[1], "speak") == 0)
    {
      return tts_smoke_speak(argv[2]);
    }

  if (argc == 4)
    {
      if (tts_smoke_parse_rate(argv[3], &rate) < 0)
        {
          tts_smoke_usage(argv[0]);
          return EXIT_FAILURE;
        }
    }
  else if (argc != 3 || strcmp(argv[1], "play") != 0)
    {
      tts_smoke_usage(argv[0]);
      return EXIT_FAILURE;
    }

  printf("tts_smoke: play %s at %lu Hz PCM16 mono\n",
         argv[2], (unsigned long)rate);
  if (smart_home_voice_player_play_file(argv[2], rate, NULL) == 0)
    {
      printf("tts_smoke: play complete\n");
      return EXIT_SUCCESS;
    }

  return EXIT_FAILURE;
}
