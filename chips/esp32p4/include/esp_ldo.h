/****************************************************************************
 * chips/esp32p4/include/esp_ldo.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

#ifndef __ARCH_RISCV_SRC_ESP32P4_INCLUDE_ESP_LDO_H
#define __ARCH_RISCV_SRC_ESP32P4_INCLUDE_ESP_LDO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Keep the ESP HAL handle private so board code and generic display drivers
 * do not depend on Espressif component headers.
 */

struct esp_ldo_channel_s;

struct esp_ldo_config_s
{
  int  channel_id;
  int  voltage_mv;
  bool adjustable;
  bool owned_by_hw;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int esp_ldo_acquire(FAR const struct esp_ldo_config_s *config,
                    FAR struct esp_ldo_channel_s **channel);
int esp_ldo_release(FAR struct esp_ldo_channel_s *channel);
int esp_ldo_set_voltage(FAR struct esp_ldo_channel_s *channel,
                        int voltage_mv);

#endif /* __ARCH_RISCV_SRC_ESP32P4_INCLUDE_ESP_LDO_H */
