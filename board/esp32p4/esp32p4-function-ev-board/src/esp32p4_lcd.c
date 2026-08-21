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
#include <syslog.h>

#include <nuttx/signal.h>
#include <nuttx/video/mipi_dsi.h>

#include <arch/board/board.h>
#include <arch/chip/esp_mipi_dsi.h>

#include "espressif/esp_gpio.h"

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

/* AML070JGI50-07403L / EK79007 timing used by the official P4X BSP. */

#define BOARD_MIPI_DSI_HACTIVE               1024
#define BOARD_MIPI_DSI_HSYNC                    10
#define BOARD_MIPI_DSI_HBACK_PORCH             160
#define BOARD_MIPI_DSI_HFRONT_PORCH            160
#define BOARD_MIPI_DSI_VACTIVE                600
#define BOARD_MIPI_DSI_VSYNC                    1
#define BOARD_MIPI_DSI_VBACK_PORCH              23
#define BOARD_MIPI_DSI_VFRONT_PORCH             12
#define BOARD_MIPI_DSI_PIXEL_CLOCK_HZ   52000000

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

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO
static const struct esp_mipi_dsi_video_pattern_config_s
  g_board_mipi_dsi_video_pattern_config =
{
  .channel          = 0,
  .hactive          = BOARD_MIPI_DSI_HACTIVE,
  .hsync            = BOARD_MIPI_DSI_HSYNC,
  .hback_porch      = BOARD_MIPI_DSI_HBACK_PORCH,
  .hfront_porch     = BOARD_MIPI_DSI_HFRONT_PORCH,
  .vactive          = BOARD_MIPI_DSI_VACTIVE,
  .vsync            = BOARD_MIPI_DSI_VSYNC,
  .vback_porch      = BOARD_MIPI_DSI_VBACK_PORCH,
  .vfront_porch     = BOARD_MIPI_DSI_VFRONT_PORCH,
  .pixel_clock_hz   = BOARD_MIPI_DSI_PIXEL_CLOCK_HZ,
  .hsync_active_low = false,
  .vsync_active_low = false,
  .pattern          = ESP_MIPI_DSI_VIDEO_PATTERN_VERTICAL_BARS,
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_mipi_dsi_panel_reset
 ****************************************************************************/

int board_mipi_dsi_panel_reset(void)
{
  int ret;

  syslog(LOG_INFO,
         "INFO: P4X DSI panel reset configure gpio=%d active_low=1\n",
         BOARD_MIPI_DSI_PANEL_RESET_GPIO);
  ret = esp_configgpio(BOARD_MIPI_DSI_PANEL_RESET_GPIO, OUTPUT);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI panel reset gpio configure ret=%d\n",
             ret);
      return -EIO;
    }

  /* RST_LCD is active low on the P4X LCD adapter. */

  syslog(LOG_INFO, "INFO: P4X DSI panel reset assert delay_us=%d\n",
         BOARD_MIPI_DSI_RESET_ASSERT_US);
  esp_gpiowrite(BOARD_MIPI_DSI_PANEL_RESET_GPIO, false);
  nxsig_usleep(BOARD_MIPI_DSI_RESET_ASSERT_US);

  syslog(LOG_INFO, "INFO: P4X DSI panel reset release delay_us=%d\n",
         BOARD_MIPI_DSI_RESET_RELEASE_US);
  esp_gpiowrite(BOARD_MIPI_DSI_PANEL_RESET_GPIO, true);
  ret = nxsig_usleep(BOARD_MIPI_DSI_RESET_RELEASE_US);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI panel reset release ret=%d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "INFO: P4X DSI panel reset complete\n");
  return OK;
}

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO

/****************************************************************************
 * Name: board_mipi_dsi_backlight_set
 ****************************************************************************/

int board_mipi_dsi_backlight_set(bool enable)
{
  int ret;

  ret = esp_configgpio(BOARD_MIPI_DSI_BACKLIGHT_GPIO, OUTPUT);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI backlight gpio configure ret=%d\n",
             ret);
      return -EIO;
    }

  /* The LCD adapter accepts a static high level on PWM for M2a bring-up.
   * Brightness duty-cycle control is a separate board-PWM integration step.
   */

  esp_gpiowrite(BOARD_MIPI_DSI_BACKLIGHT_GPIO, enable);
  syslog(LOG_INFO, "INFO: P4X DSI backlight static=%d gpio=%d\n", enable,
         BOARD_MIPI_DSI_BACKLIGHT_GPIO);
  return OK;
}

/****************************************************************************
 * Name: board_mipi_dsi_video_pattern_start
 ****************************************************************************/

int board_mipi_dsi_video_pattern_start(FAR struct mipi_dsi_host *host)
{
  int ret;

  ret = esp_mipi_dsi_video_pattern_start(
    host, &g_board_mipi_dsi_video_pattern_config);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI video pattern start ret=%d\n", ret);
      return ret;
    }

  ret = board_mipi_dsi_backlight_set(true);
  if (ret < 0)
    {
      esp_mipi_dsi_video_stop(host);
      return ret;
    }

  syslog(LOG_INFO,
         "INFO: P4X DSI video pattern active %ux%u pixel_clock_hz=%u\n",
         BOARD_MIPI_DSI_HACTIVE, BOARD_MIPI_DSI_VACTIVE,
         BOARD_MIPI_DSI_PIXEL_CLOCK_HZ);
  return OK;
}

/****************************************************************************
 * Name: board_mipi_dsi_video_stop
 ****************************************************************************/

int board_mipi_dsi_video_stop(FAR struct mipi_dsi_host *host)
{
  int ret;

  ret = board_mipi_dsi_backlight_set(false);
  if (ret < 0)
    {
      return ret;
    }

  return esp_mipi_dsi_video_stop(host);
}

#endif /* CONFIG_ESPRESSIF_MIPI_DSI_VIDEO */

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
  syslog(LOG_INFO, "INFO: P4X DSI host initialize begin\n");
  ret = esp_mipi_dsi_host_initialize(&g_board_mipi_dsi_config, host);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI host initialize ret=%d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "INFO: P4X DSI host initialize complete\n");

  ret = board_mipi_dsi_panel_reset();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI panel reset ret=%d\n", ret);
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
