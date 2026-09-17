/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_es8311.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_ESP32P4_FUNCTION_EV_BOARD_AUDIO_ES8311

#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <syslog.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/es8311.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/audio/pcm.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/kmalloc.h>

#include "espressif/esp_gpio.h"
#include "espressif/esp_i2c.h"
#include "espressif/esp_i2s.h"

#include "esp32p4-function-ev-board.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct es8311_lower_s g_es8311_lower[2];
static bool g_es8311_initialized;

/****************************************************************************
 * Name: board_es8311_log_kheap
 ****************************************************************************/

static void board_es8311_log_kheap(FAR const char *stage)
{
  struct mallinfo info = kmm_mallinfo();

  syslog(LOG_INFO, "INFO: ES8311 kheap: stage=%s total=%d used=%d"
         " free=%d largest=%d\n",
         stage, info.arena, info.uordblks, info.fordblks, info.mxordblk);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_es8311_initialize
 *
 * Description:
 *   Initialize the board ES8311 codec and register its PCM output/input
 *   endpoints.  I2C0 may already be held by GT911; esp_i2cbus_initialize()
 *   returns the shared, reference-counted master instance in that case.
 *
 ****************************************************************************/

int board_es8311_initialize(void)
{
  FAR struct audio_lowerhalf_s *codec;
  FAR struct audio_lowerhalf_s *pcm;
  FAR struct i2c_master_s *i2c;
  FAR struct i2s_dev_s *i2s;
  int ret;

  if (g_es8311_initialized)
    {
      return OK;
    }

  ret = esp_configgpio(BOARD_AUDIO_PA_ENABLE_GPIO, OUTPUT);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to configure audio PA GPIO: %d\n", ret);
      return ret;
    }

  /* Keep the amplifier disabled until both codec endpoints are registered. */

  esp_gpiowrite(BOARD_AUDIO_PA_ENABLE_GPIO, false);

  board_es8311_log_kheap("i2s-before");
  i2s = esp_i2sbus_initialize(BOARD_ES8311_I2S_PORT);
  board_es8311_log_kheap("i2s-after");
  if (i2s == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize I2S%d for ES8311\n",
             BOARD_ES8311_I2S_PORT);
      return -ENODEV;
    }

  i2c = esp_i2cbus_initialize(BOARD_ES8311_I2C_PORT);
  if (i2c == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize I2C%d for ES8311\n",
             BOARD_ES8311_I2C_PORT);
      return -ENODEV;
    }

  g_es8311_lower[0].address = BOARD_ES8311_I2C_ADDR;
  g_es8311_lower[0].frequency = BOARD_ES8311_I2C_FREQUENCY;
  codec = es8311_initialize(i2c, i2s, &g_es8311_lower[0]);
  if (codec == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize ES8311 playback\n");
      return -ENODEV;
    }

  pcm = pcm_decode_initialize(codec);
  if (pcm == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to create ES8311 PCM decoder\n");
      return -ENODEV;
    }

  ret = audio_register("pcm0", pcm);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to register ES8311 playback: %d\n",
             ret);
      return ret;
    }

  g_es8311_lower[1].address = BOARD_ES8311_I2C_ADDR;
  g_es8311_lower[1].frequency = BOARD_ES8311_I2C_FREQUENCY;
  codec = es8311_initialize(i2c, i2s, &g_es8311_lower[1]);
  if (codec == NULL)
    {
      ret = -ENODEV;
      goto errout_unregister_playback;
    }

  ret = audio_register("pcm_in0", codec);
  if (ret < 0)
    {
      goto errout_unregister_playback;
    }

  g_es8311_initialized = true;
  esp_gpiowrite(BOARD_AUDIO_PA_ENABLE_GPIO, true);
  syslog(LOG_INFO,
         "INFO: ES8311 ready: i2c=%d addr=0x%02x i2s=%d "
         "out=/dev/audio/pcm0 in=/dev/audio/pcm_in0\n",
         BOARD_ES8311_I2C_PORT, BOARD_ES8311_I2C_ADDR,
         BOARD_ES8311_I2S_PORT);
  return OK;

errout_unregister_playback:
  syslog(LOG_ERR, "ERROR: Failed to register ES8311 capture: %d\n", ret);
  audio_unregister("pcm0", pcm);
  esp_gpiowrite(BOARD_AUDIO_PA_ENABLE_GPIO, false);
  return ret;
}

#endif
