/****************************************************************************
 * app/dsi_probe/dsi_probe_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Command-only MIPI-DSI validation for the ESP32-P4X Function EV Board.
 * No video timing, DMA, framebuffer, panel init table or backlight is used.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/video/mipi_display.h>
#include <nuttx/video/mipi_dsi.h>

#include <board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DSI_PROBE_LANES        2
#define DSI_PROBE_HS_RATE_HZ   1000000000
#define DSI_PROBE_LP_RATE_HZ   10000000

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
  ssize_t transferred;
  int ret;

  (void)argc;
  (void)argv;

  printf("=== ESP32-P4X MIPI-DSI command Host probe ===\n");
  printf("link: 2 lanes, 1000 Mbps; panel: reset only; video: disabled\n");

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

  /* Generic packets are intentionally used for transport validation.  They
   * do not enable panel output or apply the EK79007 vendor init sequence.
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

  power_mode = 0;
  ret = mipi_dsi_dcs_get_power_mode(&device, &power_mode);
  if (ret < 0)
    {
      mipi_dsi_detach(&device);
      board_mipi_dsi_shutdown(host);
      return dsi_probe_fail("dcs_get_power_mode", ret);
    }

  printf("dsi_probe: DCS power mode=0x%02x\n", power_mode);
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

  printf("dsi_probe: PASS command Host validation completed\n");
  return EXIT_SUCCESS;
}
