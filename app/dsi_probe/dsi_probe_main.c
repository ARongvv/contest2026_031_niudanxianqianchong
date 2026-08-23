/****************************************************************************
 * app/dsi_probe/dsi_probe_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Staged MIPI-DSI validation for the ESP32-P4X Function EV Board.
 * The optional video command creates an EK79007-owned DPI panel, submits one
 * RGB565 colour-bar frame through draw_bitmap() and deliberately does not
 * start LVGL.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/signal.h>
#include <nuttx/video/mipi_display.h>
#include <nuttx/video/mipi_dsi.h>

#include <arch/board/board.h>
#include <arch/chip/esp_mipi_dsi_dpi_panel.h>

#include "ek79007.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DSI_PROBE_LANES        2
#define DSI_PROBE_HS_RATE_HZ   1000000000
#define DSI_PROBE_LP_RATE_HZ   10000000

#define DSI_PROBE_VIDEO_SECONDS_DEFAULT 30
#define DSI_PROBE_VIDEO_SECONDS_MAX    600

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The panel driver owns the EK79007 DCS sequence.  The probe only supplies
 * the P4X link parameters and calls the panel lifecycle in production order.
 */

static const struct ek79007_panel_config_s g_dsi_probe_panel_config =
{
  .timing     = NULL,
  .init_cmds  = NULL,
  .ninit_cmds = 0,
  .mode_flags = MIPI_DSI_MODE_LPM,
  .hs_rate    = DSI_PROBE_HS_RATE_HZ,
  .lp_rate    = DSI_PROBE_LP_RATE_HZ,
  .lanes      = DSI_PROBE_LANES,
  .format     = MIPI_DSI_FMT_RGB565,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dsi_probe_fail
 ****************************************************************************/

static int dsi_probe_fail(FAR const char *step, int ret)
{
  printf("dsi_probe: FAIL step=%s ret=%d (%s)\n", step, ret,
         strerror(-ret));
  return EXIT_FAILURE;
}

static int dsi_probe_parse_video_request(int argc, FAR char *argv[],
                                         FAR unsigned int *seconds)
{
  FAR char *endptr;
  unsigned long value;

  *seconds = DSI_PROBE_VIDEO_SECONDS_DEFAULT;
  if (argc == 1)
    {
      return OK;
    }

  if (strcmp(argv[1], "video") != 0 || argc > 3)
    {
      return -EINVAL;
    }

  if (argc == 3)
    {
      value = strtoul(argv[2], &endptr, 10);
      if (*argv[2] == '\0' || *endptr != '\0' || value == 0 ||
          value > DSI_PROBE_VIDEO_SECONDS_MAX)
        {
          return -EINVAL;
        }

      *seconds = (unsigned int)value;
    }

  return 1;
}

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN

/****************************************************************************
 * Name: dsi_probe_panel_shutdown
 ****************************************************************************/

static int dsi_probe_panel_shutdown(FAR struct ek79007_panel_s *panel)
{
  int display_ret;
  int ret;

  ret = board_mipi_dsi_backlight_set(false);
  display_ret = ek79007_panel_shutdown(panel);
  return ret < 0 ? ret : display_ret;
}

/****************************************************************************
 * Name: dsi_probe_fill_rgb565_colour_bars
 ****************************************************************************/

static void dsi_probe_fill_rgb565_colour_bars(FAR uint16_t *frame_buffer,
                                              uint16_t width,
                                              uint16_t height)
{
  static const uint16_t g_colours[] =
  {
    0xffff, /* White */
    0xffe0, /* Yellow */
    0x07ff, /* Cyan */
    0x07e0, /* Green */
    0xf81f, /* Magenta */
    0xf800, /* Red */
    0x001f, /* Blue */
    0x0000, /* Black */
  };

  uint32_t x;
  uint32_t y;
  uint32_t bar;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          bar = x * (sizeof(g_colours) / sizeof(g_colours[0])) / width;
          frame_buffer[y * width + x] = g_colours[bar];
        }
    }
}

/****************************************************************************
 * Name: dsi_probe_run_video_pattern
 ****************************************************************************/

static int dsi_probe_run_video_pattern(FAR struct mipi_dsi_host *host,
                                       FAR struct ek79007_panel_s *panel,
                                       unsigned int seconds)
{
  FAR struct esp_mipi_dsi_dpi_panel_s *dpi_panel;
  FAR void *frame_buffer;
  size_t frame_buffer_bytes;
  unsigned int elapsed;
  int display_ret;
  int status_ret;
  int ret;

  dpi_panel = panel->dpi_panel;
  if (dpi_panel == NULL)
    {
      return -EPIPE;
    }

  ret = esp_mipi_dsi_dpi_panel_get_frame_buffer(
    dpi_panel, &frame_buffer, &frame_buffer_bytes);
  if (ret < 0)
    {
      return ret;
    }

  dsi_probe_fill_rgb565_colour_bars(frame_buffer,
                                    dpi_panel->config.hactive,
                                    dpi_panel->config.vactive);

  ret = ek79007_panel_draw_bitmap(panel, frame_buffer, frame_buffer_bytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = board_mipi_dsi_backlight_set(true);
  if (ret < 0)
    {
      return ret;
    }

  printf("dsi_probe: EK79007 DPI RGB565 vertical colour bars active for "
         "%u seconds; "
         "LVGL is not involved\n", seconds);
  printf("dsi_probe: draw_bitmap() submitted; visually confirm colour bars "
         "before recording a display PASS\n");
  status_ret = board_mipi_dsi_video_dump_status(host, "probe-start");
  if (status_ret < 0)
    {
      printf("dsi_probe: DMA status snapshot unavailable ret=%d\n",
             status_ret);
    }

  for (elapsed = 0; elapsed < seconds; elapsed++)
    {
      ret = nxsig_usleep(1000 * 1000);
      if (ret < 0)
        {
          break;
        }

      if (elapsed == 0)
        {
          status_ret = board_mipi_dsi_video_dump_status(host,
                                                         "probe-after-1s");
          if (status_ret < 0)
            {
              printf("dsi_probe: DMA status snapshot unavailable ret=%d\n",
                     status_ret);
            }
        }
    }

  status_ret = board_mipi_dsi_video_dump_status(host, "probe-before-stop");
  if (status_ret < 0)
    {
      printf("dsi_probe: DMA status snapshot unavailable ret=%d\n",
             status_ret);
    }

  display_ret = dsi_probe_panel_shutdown(panel);
  if (display_ret < 0 && ret == OK)
    {
      ret = display_ret;
    }

  return ret;
}

#endif /* CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct mipi_dsi_host *host;
  struct mipi_dsi_device device;
  struct ek79007_panel_s panel;
  struct ek79007_panel_config_s panel_config;
  struct esp_mipi_dsi_dpi_panel_s dpi_panel;
  uint8_t short_payload = 0;
  uint8_t long_payload[] =
  {
    0, 0, 0
  };

  uint8_t power_mode;
  unsigned int video_seconds;
  bool dcs_read_available;
  bool video_requested;
  ssize_t transferred;
  int ret;

  ret = dsi_probe_parse_video_request(argc, argv, &video_seconds);
  if (ret < 0)
    {
      printf("usage: dsi_probe [video [seconds]]\n");
      return EXIT_FAILURE;
    }

  video_requested = ret > 0;

  printf("=== ESP32-P4X MIPI-DSI Host probe ===\n");
  printf("link: 2 lanes, 1000 Mbps; panel: EK79007; video: %s\n",
         video_requested ? "DMA colour-bar request" : "disabled");

  ret = board_mipi_dsi_initialize(&host);
  if (ret < 0)
    {
      return dsi_probe_fail("host_initialize", ret);
    }

  printf("dsi_probe: host bus=%d initialized\n", host->bus);

  memset(&device, 0, sizeof(device));
  memset(&panel, 0, sizeof(panel));
  memset(&dpi_panel, 0, sizeof(dpi_panel));
  panel_config = g_dsi_probe_panel_config;

  if (video_requested)
    {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN
      panel_config.dpi_panel = &dpi_panel;
      panel_config.dpi_config = board_mipi_dsi_dpi_panel_config_get();
#else
      printf("dsi_probe: video command is disabled; enable "
             "CONFIG_LVX_USE_DEMO_CONTEST2026_031_"
             "DSI_PROBE_VIDEO_PATTERN\n");
      board_mipi_dsi_shutdown(host);
      return EXIT_FAILURE;
#endif
    }

  device.host = host;
  device.channel = 0;
  snprintf(device.name, sizeof(device.name), "ek79007-probe");

  ret = ek79007_panel_setup(&panel, &device, &panel_config);
  if (ret < 0)
    {
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("ek79007_panel_setup", ret);
    }

  ret = mipi_dsi_attach(&device);
  if (ret < 0)
    {
      ek79007_panel_shutdown(&panel);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("device_attach", ret);
    }

  /* The hardware reset belongs after the panel object and DBI path exist,
   * matching esp_lcd_panel_reset() in the ESP-IDF EK79007 lifecycle.
   */

  ret = board_mipi_dsi_panel_reset();
  if (ret < 0)
    {
      ek79007_panel_shutdown(&panel);
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("panel_hardware_reset", ret);
    }

  /* Generic packets are intentionally kept for the command-only Host test.
   * The video test omits them so its panel path matches the ESP-IDF colour
   * bar example without non-panel DSI traffic before initialisation.
   */

  if (!video_requested)
    {
      transferred = mipi_dsi_generic_write(&device, &short_payload,
                                           sizeof(short_payload));
      if (transferred < 0)
        {
          ek79007_panel_shutdown(&panel);
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("generic_short_write", (int)transferred);
        }

      printf("dsi_probe: generic short packet accepted\n");

      transferred = mipi_dsi_generic_write(&device, long_payload,
                                           sizeof(long_payload));
      if (transferred < 0)
        {
          ek79007_panel_shutdown(&panel);
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("generic_long_write", (int)transferred);
        }

      printf("dsi_probe: generic long packet accepted\n");
    }

  ret = ek79007_panel_initialize(&panel);
  if (ret < 0)
    {
      ek79007_panel_shutdown(&panel);
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("ek79007_panel_initialize", ret);
    }

  printf("dsi_probe: EK79007 panel driver initialisation accepted\n");

  /* esp_lcd_ek79007 documents display-on immediately after panel init.  It
   * precedes the application's draw_bitmap() submission; backlight remains
   * off until the video probe has filled and synchronized its frame.
   */

  ret = ek79007_panel_set_display(&panel, true);
  if (ret < 0)
    {
      ek79007_panel_shutdown(&panel);
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("panel_display_on", ret);
    }

  printf("dsi_probe: EK79007 display enabled\n");

  if (video_requested)
    {
      dcs_read_available = false;
      printf("dsi_probe: skip DCS power-mode read while DPI video is "
             "active\n");
    }
  else
    {
      power_mode = 0;
      ret = mipi_dsi_dcs_get_power_mode(&device, &power_mode);
      if (ret < 0)
        {
          dcs_read_available = false;
          printf("dsi_probe: DCS power-mode read unavailable ret=%d (%s); "
                 "continuing because this panel may be write-only in M1\n",
                 ret, strerror(-ret));
        }
      else
        {
          dcs_read_available = true;
          printf("dsi_probe: DCS power mode=0x%02x\n", power_mode);
        }
    }

  if (video_requested)
    {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN
      ret = dsi_probe_run_video_pattern(host, &panel, video_seconds);
      if (ret < 0)
        {
          ek79007_panel_shutdown(&panel);
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("video_dma_scanout", ret);
        }
#else
      ek79007_panel_shutdown(&panel);
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return EXIT_FAILURE;
#endif
    }

  ret = mipi_dsi_detach(&device);
  if (ret < 0)
    {
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("device_detach", ret);
    }

  ret = board_mipi_dsi_shutdown(host);
  if (ret < 0)
    {
      return dsi_probe_fail("host_shutdown", ret);
    }

  if (video_requested)
    {
      printf("dsi_probe: HOST PASS DPI DMA sequence completed; visual "
             "display result pending (DCS read=skipped in video mode)\n");
    }
  else
    {
      printf("dsi_probe: PASS command Host validation completed "
             "(DCS read=%s)\n",
             dcs_read_available ? "available" : "unavailable");
    }

  return EXIT_SUCCESS;
}
