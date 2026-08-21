/****************************************************************************
 * chips/esp32p4/common/espressif/esp_ldo.c
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

#include <errno.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_ldo_regulator.h"

#include "esp_ldo.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct esp_ldo_channel_s
{
  esp_ldo_channel_handle_t handle;
  bool                     adjustable;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int esp_ldo_result(int ret)
{
  switch (ret)
    {
      case ESP_OK:
        return OK;

      case ESP_ERR_NO_MEM:
        return -ENOMEM;

      case ESP_ERR_INVALID_ARG:
        return -EINVAL;

      case ESP_ERR_INVALID_STATE:
        return -EALREADY;

      case ESP_ERR_NOT_FOUND:
        return -ENODEV;

      case ESP_ERR_NOT_SUPPORTED:
        return -ENOTSUP;

      case ESP_ERR_TIMEOUT:
        return -ETIMEDOUT;

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp_ldo_acquire(FAR const struct esp_ldo_config_s *config,
                    FAR struct esp_ldo_channel_s **channel)
{
  struct esp_ldo_channel_s *result;
  esp_ldo_channel_config_t  hal_config;
  int ret;

  if (config == NULL || channel == NULL || config->channel_id < 0 ||
      config->voltage_mv <= 0)
    {
      return -EINVAL;
    }

  result = calloc(1, sizeof(*result));
  if (result == NULL)
    {
      return -ENOMEM;
    }

  hal_config = (esp_ldo_channel_config_t)
    {
      .chan_id = config->channel_id,
      .voltage_mv = config->voltage_mv,
      .flags =
        {
          .adjustable = config->adjustable,
          .owned_by_hw = config->owned_by_hw,
        },
    };

  ret = esp_ldo_acquire_channel(&hal_config, &result->handle);
  if (ret != ESP_OK)
    {
      free(result);
      return esp_ldo_result(ret);
    }

  result->adjustable = config->adjustable;
  *channel = result;
  return OK;
}

int esp_ldo_release(FAR struct esp_ldo_channel_s *channel)
{
  int ret;

  if (channel == NULL)
    {
      return -EINVAL;
    }

  ret = esp_ldo_release_channel(channel->handle);
  if (ret != ESP_OK)
    {
      return esp_ldo_result(ret);
    }

  free(channel);
  return OK;
}

int esp_ldo_set_voltage(FAR struct esp_ldo_channel_s *channel,
                        int voltage_mv)
{
  int ret;

  if (channel == NULL || voltage_mv <= 0)
    {
      return -EINVAL;
    }

  if (!channel->adjustable)
    {
      return -EPERM;
    }

  ret = esp_ldo_channel_adjust_voltage(channel->handle, voltage_mv);
  return esp_ldo_result(ret);
}
