/****************************************************************************
 * chips/esp32p4/common/espressif/esp_mipi_dsi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

#ifndef __CHIPS_ESP32P4_COMMON_ESPRESSIF_ESP_MIPI_DSI_H
#define __CHIPS_ESP32P4_COMMON_ESPRESSIF_ESP_MIPI_DSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/video/mipi_dsi.h>

#include <stdint.h>

#include "esp_ldo.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ESP_MIPI_DSI_BUS0                0
#define ESP_MIPI_DSI_MAX_DATA_LANES       2
#define ESP_MIPI_DSI_DPHY_VOLTAGE_MV      2500

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The P4X board obtains the D-PHY supply from an internal LDO.  The exact
 * channel is a board decision, therefore it remains a host configuration.
 */

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

/****************************************************************************
 * Name: esp_mipi_dsi_host_initialize
 *
 * Description:
 *   Initialize the ESP32-P4 command-mode MIPI DSI host and register it with
 *   the generic NuttX MIPI DSI framework.  Only bus zero is implemented by
 *   the ESP32-P4 hardware.
 *
 *   The registered generic host is static because the generic MIPI framework
 *   has no unregister operation.  Call esp_mipi_dsi_host_shutdown() only to
 *   power down hardware; a later initialize call reuses that same host.
 *
 ****************************************************************************/

int esp_mipi_dsi_host_initialize(
  FAR const struct esp_mipi_dsi_host_config_s *config,
  FAR struct mipi_dsi_host **host);

/****************************************************************************
 * Name: esp_mipi_dsi_host_shutdown
 *
 * Description:
 *   Stop the ESP32-P4 DSI hardware and release its PHY LDO reservation.
 *
 ****************************************************************************/

int esp_mipi_dsi_host_shutdown(FAR struct mipi_dsi_host *host);

#endif /* __CHIPS_ESP32P4_COMMON_ESPRESSIF_ESP_MIPI_DSI_H */
