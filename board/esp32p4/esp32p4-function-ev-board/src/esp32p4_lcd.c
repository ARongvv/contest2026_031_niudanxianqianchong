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
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/signal.h>
#include <nuttx/video/mipi_dsi.h>

#include <arch/board/board.h>
#include <arch/chip/esp_mipi_dsi.h>

#include "espressif/esp_gpio.h"

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
#  include "esp_cache.h"
#  include "esp_heap_caps.h"
#endif

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
#define BOARD_MIPI_DSI_BYTES_PER_PIXEL         3
#define BOARD_MIPI_DSI_FRAME_BYTES \
  ((size_t)BOARD_MIPI_DSI_HACTIVE * BOARD_MIPI_DSI_VACTIVE * \
   BOARD_MIPI_DSI_BYTES_PER_PIXEL)
#define BOARD_MIPI_DSI_FRAME_ALIGNMENT        64
#define BOARD_MIPI_DSI_COLOUR_BAR_COUNT        8

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

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
static FAR uint8_t *g_board_mipi_dsi_frame_buffer;
#endif
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

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
/****************************************************************************
 * Name: board_mipi_dsi_fill_colour_bars
 ****************************************************************************/

static void board_mipi_dsi_fill_colour_bars(FAR uint8_t *frame_buffer)
{
  static const uint8_t colours[BOARD_MIPI_DSI_COLOUR_BAR_COUNT][3] =
  {
    {0xff, 0xff, 0xff}, /* White */
    {0xff, 0xff, 0x00}, /* Yellow */
    {0x00, 0xff, 0xff}, /* Cyan */
    {0x00, 0xff, 0x00}, /* Green */
    {0xff, 0x00, 0xff}, /* Magenta */
    {0xff, 0x00, 0x00}, /* Red */
    {0x00, 0x00, 0xff}, /* Blue */
    {0x00, 0x00, 0x00}, /* Black */
  };

  FAR uint8_t *pixel = frame_buffer;
  uint32_t x;
  uint32_t y;
  uint32_t bar;

  for (y = 0; y < BOARD_MIPI_DSI_VACTIVE; y++)
    {
      for (x = 0; x < BOARD_MIPI_DSI_HACTIVE; x++)
        {
          bar = x * BOARD_MIPI_DSI_COLOUR_BAR_COUNT /
                BOARD_MIPI_DSI_HACTIVE;
          pixel[0] = colours[bar][0];
          pixel[1] = colours[bar][1];
          pixel[2] = colours[bar][2];
          pixel += BOARD_MIPI_DSI_BYTES_PER_PIXEL;
        }
    }
}

/****************************************************************************
 * Name: board_mipi_dsi_allocate_frame_buffer
 ****************************************************************************/

static int board_mipi_dsi_allocate_frame_buffer(void)
{
  esp_err_t result;

  if (g_board_mipi_dsi_frame_buffer != NULL)
    {
      return OK;
    }

  g_board_mipi_dsi_frame_buffer = heap_caps_aligned_calloc(
    BOARD_MIPI_DSI_FRAME_ALIGNMENT, 1, BOARD_MIPI_DSI_FRAME_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (g_board_mipi_dsi_frame_buffer == NULL)
    {
      syslog(LOG_ERR,
             "ERROR: P4X DSI DMA frame allocation bytes=%zu failed\n",
             BOARD_MIPI_DSI_FRAME_BYTES);
      return -ENOMEM;
    }

  board_mipi_dsi_fill_colour_bars(g_board_mipi_dsi_frame_buffer);
  result = esp_cache_msync(g_board_mipi_dsi_frame_buffer,
                           BOARD_MIPI_DSI_FRAME_BYTES,
                           ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                           ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (result != ESP_OK)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI DMA frame cache sync ret=%d\n",
             result);
      heap_caps_free(g_board_mipi_dsi_frame_buffer);
      g_board_mipi_dsi_frame_buffer = NULL;
      return -EIO;
    }

  syslog(LOG_INFO,
         "INFO: P4X DSI DMA colour bars ready buffer=%p bytes=%zu\n",
         g_board_mipi_dsi_frame_buffer, BOARD_MIPI_DSI_FRAME_BYTES);
  return OK;
}

/****************************************************************************
 * Name: board_mipi_dsi_release_frame_buffer
 ****************************************************************************/

static void board_mipi_dsi_release_frame_buffer(void)
{
  if (g_board_mipi_dsi_frame_buffer != NULL)
    {
      heap_caps_free(g_board_mipi_dsi_frame_buffer);
      g_board_mipi_dsi_frame_buffer = NULL;
    }
}
#endif

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

  /* The LCD adapter accepts a static high level on PWM for DMA bring-up.
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
#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
  struct esp_mipi_dsi_video_dma_config_s dma_config;
#endif
  int ret;

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
  ret = board_mipi_dsi_allocate_frame_buffer();
  if (ret < 0)
    {
      return ret;
    }

  memset(&dma_config, 0, sizeof(dma_config));
  dma_config.channel = g_board_mipi_dsi_video_pattern_config.channel;
  dma_config.hactive = g_board_mipi_dsi_video_pattern_config.hactive;
  dma_config.hsync = g_board_mipi_dsi_video_pattern_config.hsync;
  dma_config.hback_porch =
    g_board_mipi_dsi_video_pattern_config.hback_porch;
  dma_config.hfront_porch =
    g_board_mipi_dsi_video_pattern_config.hfront_porch;
  dma_config.vactive = g_board_mipi_dsi_video_pattern_config.vactive;
  dma_config.vsync = g_board_mipi_dsi_video_pattern_config.vsync;
  dma_config.vback_porch =
    g_board_mipi_dsi_video_pattern_config.vback_porch;
  dma_config.vfront_porch =
    g_board_mipi_dsi_video_pattern_config.vfront_porch;
  dma_config.pixel_clock_hz =
    g_board_mipi_dsi_video_pattern_config.pixel_clock_hz;
  dma_config.hsync_active_low =
    g_board_mipi_dsi_video_pattern_config.hsync_active_low;
  dma_config.vsync_active_low =
    g_board_mipi_dsi_video_pattern_config.vsync_active_low;
  dma_config.frame_buffer = g_board_mipi_dsi_frame_buffer;
  dma_config.frame_buffer_bytes = BOARD_MIPI_DSI_FRAME_BYTES;
  ret = esp_mipi_dsi_video_dma_start(host, &dma_config);
#else
  ret = esp_mipi_dsi_video_pattern_start(
    host, &g_board_mipi_dsi_video_pattern_config);
#endif
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: P4X DSI video start ret=%d\n", ret);
#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
      board_mipi_dsi_release_frame_buffer();
#endif
      return ret;
    }

  ret = board_mipi_dsi_backlight_set(true);
  if (ret < 0)
    {
      esp_mipi_dsi_video_stop(host);
      return ret;
    }

  syslog(LOG_INFO,
         "INFO: P4X DSI video scanout active %ux%u pixel_clock_hz=%u\n",
         BOARD_MIPI_DSI_HACTIVE, BOARD_MIPI_DSI_VACTIVE,
         BOARD_MIPI_DSI_PIXEL_CLOCK_HZ);
  return OK;
}

/****************************************************************************
 * Name: board_mipi_dsi_video_stop
 ****************************************************************************/

int board_mipi_dsi_video_stop(FAR struct mipi_dsi_host *host)
{
  int video_ret;
  int ret;

  ret = board_mipi_dsi_backlight_set(false);
  video_ret = esp_mipi_dsi_video_stop(host);
#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
  board_mipi_dsi_release_frame_buffer();
#endif
  return ret < 0 ? ret : video_ret;
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
  int ret = esp_mipi_dsi_host_shutdown(host);

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_VIDEO_DMA
  board_mipi_dsi_release_frame_buffer();
#endif
  return ret;
}

#endif /* CONFIG_ESPRESSIF_MIPI_DSI */
