/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_lcd.c
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

#ifdef CONFIG_ESPRESSIF_MIPI_DSI

#include <errno.h>

#include <nuttx/signal.h>
#include <nuttx/video/mipi_dsi.h>

#include <board.h>

#include "espressif/esp_gpio.h"
#include "espressif/esp_mipi_dsi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ESP32-P4 Function EV Board MIPI DSI configuration, taken from the
 * Espressif board configuration: D-PHY LDO channel 3 at 2.5 V, two data
 * lanes at 1000 Mbps.  The command Host itself supports one or two lanes.
 */

#define BOARD_MIPI_DSI_PHY_LDO_CHANNEL     3
#define BOARD_MIPI_DSI_LANE_COUNT           2
#define BOARD_MIPI_DSI_LANE_BIT_RATE_MBPS   1000
#define BOARD_MIPI_DSI_PHY_REF_CLOCK_HZ     40000000
#define BOARD_MIPI_DSI_RESET_ASSERT_US      (20 * 1000)
#define BOARD_MIPI_DSI_RESET_RELEASE_US     (120 * 1000)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct esp_mipi_dsi_host_config_s g_board_mipi_dsi_config =
{
  .bus                = ESP_MIPI_DSI_BUS0,
  .lane_num           = BOARD_MIPI_DSI_LANE_COUNT,
  .lane_bit_rate_mbps = BOARD_MIPI_DSI_LANE_BIT_RATE_MBPS,
  .phy_ref_clock_hz   = BOARD_MIPI_DSI_PHY_REF_CLOCK_HZ,
  .timeout_ms         = 0,
  .phy_ldo =
    {
      .channel_id  = BOARD_MIPI_DSI_PHY_LDO_CHANNEL,
      .voltage_mv  = ESP_MIPI_DSI_DPHY_VOLTAGE_MV,
      .adjustable  = true,
      .owned_by_hw = false,
    },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_mipi_dsi_panel_reset
 ****************************************************************************/

int board_mipi_dsi_panel_reset(void)
{
  int ret;

  ret = esp_configgpio(BOARD_MIPI_DSI_PANEL_RESET_GPIO, OUTPUT);
  if (ret < 0)
    {
      return -EIO;
    }

  /* RST_LCD is active low on the P4X LCD adapter. */

  esp_gpiowrite(BOARD_MIPI_DSI_PANEL_RESET_GPIO, false);
  nxsig_usleep(BOARD_MIPI_DSI_RESET_ASSERT_US);
  esp_gpiowrite(BOARD_MIPI_DSI_PANEL_RESET_GPIO, true);
  return nxsig_usleep(BOARD_MIPI_DSI_RESET_RELEASE_US);
}

/****************************************************************************
 * Name: board_mipi_dsi_initialize
 ****************************************************************************/

int board_mipi_dsi_initialize(FAR struct mipi_dsi_host **host)
{
  int ret;

  if (host == NULL)
    {
      return -EINVAL;
    }

  *host = NULL;
  ret = esp_mipi_dsi_host_initialize(&g_board_mipi_dsi_config, host);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_mipi_dsi_panel_reset();
  if (ret < 0)
    {
      esp_mipi_dsi_host_shutdown(*host);
      *host = NULL;
    }

  return ret;
}

/****************************************************************************
 * Name: board_mipi_dsi_shutdown
 ****************************************************************************/

int board_mipi_dsi_shutdown(FAR struct mipi_dsi_host *host)
{
  return esp_mipi_dsi_host_shutdown(host);
}

#endif /* CONFIG_ESPRESSIF_MIPI_DSI */
