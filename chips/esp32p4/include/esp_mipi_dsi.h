/****************************************************************************
 * chips/esp32p4/include/esp_mipi_dsi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

#ifndef __ARCH_RISCV_SRC_ESP32P4_INCLUDE_ESP_MIPI_DSI_H
#define __ARCH_RISCV_SRC_ESP32P4_INCLUDE_ESP_MIPI_DSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/video/mipi_dsi.h>

#include <stdint.h>

#include <arch/chip/esp_ldo.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ESP_MIPI_DSI_BUS0                0
#define ESP_MIPI_DSI_MAX_DATA_LANES       2
#define ESP_MIPI_DSI_DPHY_VOLTAGE_MV      2500

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct esp_mipi_dsi_host_config_s
{
  uint8_t                 bus;
  uint8_t                 lane_num;
  uint32_t                lane_bit_rate_mbps;
  uint32_t                phy_ref_clock_hz;
  uint32_t                timeout_ms;
  struct esp_ldo_config_s phy_ldo;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int esp_mipi_dsi_host_initialize(
  FAR const struct esp_mipi_dsi_host_config_s *config,
  FAR struct mipi_dsi_host **host);
int esp_mipi_dsi_host_shutdown(FAR struct mipi_dsi_host *host);

#endif /* __ARCH_RISCV_SRC_ESP32P4_INCLUDE_ESP_MIPI_DSI_H */
