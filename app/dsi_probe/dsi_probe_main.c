/****************************************************************************
 * app/dsi_probe/dsi_probe_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Staged MIPI-DSI validation for the ESP32-P4X Function EV Board.
 * The optional video command uses the Host's internal pattern generator and
 * never allocates a framebuffer or starts LVGL.
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

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DSI_PROBE_LANES        2
#define DSI_PROBE_HS_RATE_HZ   1000000000
#define DSI_PROBE_LP_RATE_HZ   10000000

#define DSI_PROBE_SLEEP_EXIT_DELAY_MS 120
#define DSI_PROBE_DISPLAY_ON_DELAY_MS   20
#define DSI_PROBE_VIDEO_SECONDS_DEFAULT 30
#define DSI_PROBE_VIDEO_SECONDS_MAX    600

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct dsi_probe_dcs_command_s
{
  uint8_t command;
  FAR const uint8_t *data;
  size_t data_len;
  uint16_t delay_ms;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This write-only sequence mirrors the current EK79007 panel driver's
 * default initialisation path.  It is deliberately local to the probe so
 * M1 can exercise the real command stream without enabling the production
 * panel driver, DPI video output or framebuffer allocation.
 */

static const uint8_t g_dsi_probe_cmd_b2[] =
{
  0x10
};

static const uint8_t g_dsi_probe_cmd_80[] =
{
  0x8b
};

static const uint8_t g_dsi_probe_cmd_81[] =
{
  0x78
};

static const uint8_t g_dsi_probe_cmd_82[] =
{
  0x84
};

static const uint8_t g_dsi_probe_cmd_83[] =
{
  0x88
};

static const uint8_t g_dsi_probe_cmd_84[] =
{
  0xa8
};

static const uint8_t g_dsi_probe_cmd_85[] =
{
  0xe3
};

static const uint8_t g_dsi_probe_cmd_86[] =
{
  0x88
};

static const struct dsi_probe_dcs_command_s g_dsi_probe_init_cmds[] =
{
  {0xb2, g_dsi_probe_cmd_b2, sizeof(g_dsi_probe_cmd_b2), 0},
  {0x80, g_dsi_probe_cmd_80, sizeof(g_dsi_probe_cmd_80), 0},
  {0x81, g_dsi_probe_cmd_81, sizeof(g_dsi_probe_cmd_81), 0},
  {0x82, g_dsi_probe_cmd_82, sizeof(g_dsi_probe_cmd_82), 0},
  {0x83, g_dsi_probe_cmd_83, sizeof(g_dsi_probe_cmd_83), 0},
  {0x84, g_dsi_probe_cmd_84, sizeof(g_dsi_probe_cmd_84), 0},
  {0x85, g_dsi_probe_cmd_85, sizeof(g_dsi_probe_cmd_85), 0},
  {0x86, g_dsi_probe_cmd_86, sizeof(g_dsi_probe_cmd_86), 0},
  {MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0, DSI_PROBE_SLEEP_EXIT_DELAY_MS},
  {MIPI_DCS_SET_DISPLAY_ON, NULL, 0, DSI_PROBE_DISPLAY_ON_DELAY_MS},
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

/****************************************************************************
 * Name: dsi_probe_send_init_sequence
 ****************************************************************************/

static int dsi_probe_send_init_sequence(FAR struct mipi_dsi_device *device)
{
  size_t i;
  ssize_t ret;

  for (i = 0; i < sizeof(g_dsi_probe_init_cmds) /
                  sizeof(g_dsi_probe_init_cmds[0]); i++)
    {
      FAR const struct dsi_probe_dcs_command_s *cmd =
        &g_dsi_probe_init_cmds[i];

      ret = mipi_dsi_dcs_write(device, cmd->command, cmd->data,
                               cmd->data_len);
      if (ret < 0)
        {
          printf("dsi_probe: DCS command 0x%02x failed ret=%zd\n",
                 cmd->command, ret);
          return (int)ret;
        }

      if (cmd->delay_ms > 0)
        {
          ret = nxsig_usleep((useconds_t)cmd->delay_ms * 1000);
          if (ret < 0)
            {
              return (int)ret;
            }
        }
    }

  return OK;
}

/****************************************************************************
 * Name: dsi_probe_parse_video_request
 ****************************************************************************/

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
 * Name: dsi_probe_run_video_pattern
 ****************************************************************************/

static int dsi_probe_run_video_pattern(FAR struct mipi_dsi_host *host,
                                       unsigned int seconds)
{
  unsigned int elapsed;
  int ret;

  ret = board_mipi_dsi_video_pattern_start(host);
  if (ret < 0)
    {
      return ret;
    }

  printf("dsi_probe: vertical colour bars active for %u seconds; "
         "no framebuffer or LVGL is involved\n", seconds);
  for (elapsed = 0; elapsed < seconds; elapsed++)
    {
      ret = nxsig_usleep(1000 * 1000);
      if (ret < 0)
        {
          break;
        }
    }

  if (board_mipi_dsi_video_stop(host) < 0 && ret == OK)
    {
      ret = -EIO;
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
  uint8_t short_payload = 0;
  uint8_t long_payload[] = {0, 0, 0};
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
         video_requested ? "pattern request" : "disabled");

  ret = board_mipi_dsi_initialize(&host);
  if (ret < 0)
    {
      return dsi_probe_fail("host_initialize", ret);
    }

  printf("dsi_probe: host bus=%d initialized\n", host->bus);

  memset(&device, 0, sizeof(device));
  device.host = host;
  device.channel = 0;
  device.lanes = DSI_PROBE_LANES;
  device.format = MIPI_DSI_FMT_RGB888;
  device.mode_flags = MIPI_DSI_MODE_LPM;
  device.hs_rate = DSI_PROBE_HS_RATE_HZ;
  device.lp_rate = DSI_PROBE_LP_RATE_HZ;
  snprintf(device.name, sizeof(device.name), "ek79007-probe");

  ret = mipi_dsi_attach(&device);
  if (ret < 0)
    {
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("device_attach", ret);
    }

  /* Generic packets validate host FIFO submission before sending the real
   * EK79007 write-only initialisation sequence.  Neither operation starts
   * video scanout or turns on the display.
   */

  transferred = mipi_dsi_generic_write(&device, &short_payload,
                                       sizeof(short_payload));
  if (transferred < 0)
    {
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("generic_short_write", (int)transferred);
    }

  printf("dsi_probe: generic short packet accepted\n");

  transferred = mipi_dsi_generic_write(&device, long_payload,
                                       sizeof(long_payload));
  if (transferred < 0)
    {
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("generic_long_write", (int)transferred);
    }

  printf("dsi_probe: generic long packet accepted\n");

  ret = dsi_probe_send_init_sequence(&device);
  if (ret < 0)
    {
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("ek79007_init_sequence", ret);
    }

  printf("dsi_probe: EK79007 DCS initialisation writes accepted\n");

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

  if (video_requested)
    {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN
      ret = dsi_probe_run_video_pattern(host, video_seconds);
      if (ret < 0)
        {
          mipi_dsi_detach(&device);
          board_mipi_dsi_shutdown(host);
          return dsi_probe_fail("video_pattern", ret);
        }
#else
      printf("dsi_probe: video command is disabled; enable "
             "CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN\n");
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

  printf("dsi_probe: PASS %s validation completed (DCS read=%s)\n",
         video_requested ? "DPI pattern" : "command Host",
         dcs_read_available ? "available" : "unavailable");
  return EXIT_SUCCESS;
}
