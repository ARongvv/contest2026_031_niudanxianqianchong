/****************************************************************************
 * app/dsi_probe/dsi_probe_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Staged MIPI-DSI validation for the ESP32-P4X Function EV Board.
 * The optional video command creates an EK79007-owned DPI panel, submits one
 * RGB565 colour-bar frame through draw_bitmap() and deliberately does not
 * start LVGL.  The optional pattern command drives the same DPI timing from
 * the Host built-in pattern generator while the Bridge pixel output is
 * disconnected, matching esp_lcd_dpi_panel_set_pattern().
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
#include <arch/chip/esp_mipi_dsi.h>
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
#define DSI_PROBE_PHY_SAMPLE_COUNT     2000
#define DSI_PROBE_PHY_SAMPLE_INTERVAL_US 50

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum dsi_probe_mode_e
{
  DSI_PROBE_MODE_COMMAND = 0,
  DSI_PROBE_MODE_VIDEO,
  DSI_PROBE_MODE_PATTERN,
};

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
  .noinit     = false,
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

static int dsi_probe_parse_request(int argc, FAR char *argv[],
                                   FAR enum dsi_probe_mode_e *mode,
                                   FAR unsigned int *seconds,
                                   FAR bool *noinit)
{
  FAR char *endptr;
  FAR char *seconds_arg;
  unsigned long value;
  int arg_index;

  *mode = DSI_PROBE_MODE_COMMAND;
  *seconds = DSI_PROBE_VIDEO_SECONDS_DEFAULT;
  *noinit = false;
  if (argc == 1)
    {
      return OK;
    }

  if (argc > 4)
    {
      return -EINVAL;
    }

  if (strcmp(argv[1], "video") == 0)
    {
      *mode = DSI_PROBE_MODE_VIDEO;
    }
  else if (strcmp(argv[1], "pattern") == 0)
    {
      *mode = DSI_PROBE_MODE_PATTERN;
    }
  else
    {
      return -EINVAL;
    }

  /* Accept "noinit" and the seconds value in either order, e.g.
   * "pattern noinit 10" and "pattern 10 noinit".
   */

  seconds_arg = NULL;
  for (arg_index = 2; arg_index < argc; arg_index++)
    {
      if (strcmp(argv[arg_index], "noinit") == 0)
        {
          *noinit = true;
        }
      else if (seconds_arg == NULL)
        {
          seconds_arg = argv[arg_index];
        }
      else
        {
          return -EINVAL;
        }
    }

  if (seconds_arg != NULL)
    {
      value = strtoul(seconds_arg, &endptr, 10);
      if (*seconds_arg == '\0' || *endptr != '\0' || value == 0 ||
          value > DSI_PROBE_VIDEO_SECONDS_MAX)
        {
          return -EINVAL;
        }

      *seconds = (unsigned int)value;
    }

  return OK;
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

  /* The known-good ESP-IDF sample enables brightness before its first
   * draw_bitmap().  Preserve that order for this one-to-one P4X experiment.
   */

  ret = board_mipi_dsi_backlight_set(true);
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
      board_mipi_dsi_backlight_set(false);
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

  status_ret = board_mipi_dsi_video_sample_phy_status(
    host, "probe-start", DSI_PROBE_PHY_SAMPLE_COUNT,
    DSI_PROBE_PHY_SAMPLE_INTERVAL_US);
  if (status_ret < 0)
    {
      printf("dsi_probe: PHY activity sample unavailable ret=%d\n",
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

          status_ret = board_mipi_dsi_video_sample_phy_status(
            host, "probe-after-1s", DSI_PROBE_PHY_SAMPLE_COUNT,
            DSI_PROBE_PHY_SAMPLE_INTERVAL_US);
          if (status_ret < 0)
            {
              printf("dsi_probe: PHY activity sample unavailable ret=%d\n",
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

/****************************************************************************
 * Name: dsi_probe_run_host_pattern
 ****************************************************************************/

static int dsi_probe_run_host_pattern(FAR struct mipi_dsi_host *host,
                                      FAR struct ek79007_panel_s *panel,
                                      unsigned int seconds)
{
  unsigned int elapsed;
  int display_ret;
  int status_ret;
  int ret;

  /* Match esp_lcd_dpi_panel_set_pattern(): keep the EK79007-owned DPI panel
   * and its validated GDMA lifecycle running, disconnect Bridge DPI output,
   * then select the Host's built-in pattern generator.
   */

  if (panel->dpi_panel == NULL)
    {
      return -EPIPE;
    }

  ret = board_mipi_dsi_backlight_set(true);
  if (ret < 0)
    {
      return ret;
    }

  ret = esp_mipi_dsi_video_pattern_set(
    host, ESP_MIPI_DSI_VIDEO_PATTERN_VERTICAL_BARS);
  if (ret < 0)
    {
      board_mipi_dsi_backlight_set(false);
      return ret;
    }

  printf("dsi_probe: DSI Host built-in vertical colour bars active for "
         "%u seconds\n", seconds);
  printf("dsi_probe: Bridge DPI output is disabled while the Host pattern "
         "is selected; framebuffer contents are bypassed\n");

  status_ret = board_mipi_dsi_video_dump_status(host, "pattern-start");
  if (status_ret < 0)
    {
      printf("dsi_probe: DMA status snapshot unavailable ret=%d\n",
             status_ret);
    }

  status_ret = board_mipi_dsi_video_sample_phy_status(
    host, "pattern-start", DSI_PROBE_PHY_SAMPLE_COUNT,
    DSI_PROBE_PHY_SAMPLE_INTERVAL_US);
  if (status_ret < 0)
    {
      printf("dsi_probe: PHY activity sample unavailable ret=%d\n",
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
          status_ret = board_mipi_dsi_video_dump_status(
            host, "pattern-after-1s");
          if (status_ret < 0)
            {
              printf("dsi_probe: DMA status snapshot unavailable ret=%d\n",
                     status_ret);
            }

          status_ret = board_mipi_dsi_video_sample_phy_status(
            host, "pattern-after-1s", DSI_PROBE_PHY_SAMPLE_COUNT,
            DSI_PROBE_PHY_SAMPLE_INTERVAL_US);
          if (status_ret < 0)
            {
              printf("dsi_probe: PHY activity sample unavailable ret=%d\n",
                     status_ret);
            }
        }
    }

  status_ret = board_mipi_dsi_video_dump_status(host,
                                                 "pattern-before-stop");
  if (status_ret < 0)
    {
      printf("dsi_probe: DMA status snapshot unavailable ret=%d\n",
             status_ret);
    }

  display_ret = esp_mipi_dsi_video_pattern_set(
    host, ESP_MIPI_DSI_VIDEO_PATTERN_NONE);
  if (display_ret < 0 && ret == OK)
    {
      ret = display_ret;
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
  enum dsi_probe_mode_e mode;
  bool dcs_read_available;
  bool noinit;
  bool pattern_requested;
  bool video_requested;
  ssize_t transferred;
  int ret;

  ret = dsi_probe_parse_request(argc, argv, &mode, &video_seconds, &noinit);
  if (ret < 0)
    {
      printf("usage: dsi_probe [video|pattern [seconds] [noinit]]\n");
      return EXIT_FAILURE;
    }

  video_requested = mode == DSI_PROBE_MODE_VIDEO;
  pattern_requested = mode == DSI_PROBE_MODE_PATTERN;

  printf("=== ESP32-P4X MIPI-DSI Host probe ===\n");
  printf("link: 2 lanes, 1000 Mbps; panel: EK79007; source: %s%s\n",
         video_requested ? "DMA colour-bar request" :
         pattern_requested ? "Host built-in pattern request" :
                             "command validation",
         noinit ? " (noinit: skip reset and init)" : "");

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
  panel_config.noinit = noinit;

  if (video_requested || pattern_requested)
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
   * noinit deliberately skips it: the panel must already be initialised
   * (IDF hot-swap bisect) and reset would erase that state.
   */

  if (!noinit)
    {
      ret = board_mipi_dsi_panel_reset();
      if (ret < 0)
        {
          ek79007_panel_shutdown(&panel);
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("panel_hardware_reset", ret);
        }
    }
  else
    {
      printf("dsi_probe: noinit: panel reset skipped; previous panel state "
             "is preserved\n");
    }

  /* Generic packets are intentionally kept for the command-only Host test.
   * Video and pattern tests omit them so their panel path matches the
   * ESP-IDF colour bar examples without non-panel DSI traffic before
   * initialisation.
   */

  if (!video_requested && !pattern_requested)
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

  if (noinit)
    {
      printf("dsi_probe: noinit: panel DCS init sequence skipped; "
             "display timing and scanout follow the active profile\n");
    }
  else
    {
      printf("dsi_probe: EK79007 panel driver initialisation accepted\n");
    }

  if (video_requested || pattern_requested)
    {
      /* The ESP-IDF EK79007 component intentionally reports the standard
       * DCS display_on/off operation as unsupported.  The validated sample
       * therefore starts DPI and draws without sending DCS 0x29.
       */

      printf("dsi_probe: strict ESP-IDF video profile: skip DCS display "
             "on (0x29)\n");
    }
  else
    {
      ret = ek79007_panel_set_display(&panel, true);
      if (ret < 0)
        {
          ek79007_panel_shutdown(&panel);
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("panel_display_on", ret);
        }

      printf("dsi_probe: EK79007 display enabled\n");
    }

  if (video_requested || pattern_requested || noinit)
    {
      dcs_read_available = false;
      printf("dsi_probe: skip DCS power-mode read%s\n",
             noinit ? " (noinit: panel state comes from the IDF side)" :
                      " while DPI video is active");
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

  if (pattern_requested)
    {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN
      ret = dsi_probe_run_host_pattern(host, &panel, video_seconds);
      if (ret < 0)
        {
          ek79007_panel_shutdown(&panel);
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("host_pattern", ret);
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

  if (video_requested || pattern_requested)
    {
      printf("dsi_probe: HOST PASS DPI %s sequence completed; visual "
             "display result pending (DCS read=skipped%s)\n",
             pattern_requested ? "built-in pattern" : "DMA",
             noinit ? ", noinit" : "");
    }
  else
    {
      printf("dsi_probe: PASS command Host validation completed "
             "(DCS read=%s)\n",
             dcs_read_available ? "available" : "unavailable");
    }

  return EXIT_SUCCESS;
}
