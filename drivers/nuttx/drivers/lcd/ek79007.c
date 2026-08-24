/****************************************************************************
 * drivers/lcd/ek79007.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * EK79007 MIPI-DSI panel controller driver.
 *
 * The EK79007 receives DCS and vendor commands through the generic NuttX
 * MIPI-DSI framework.  A caller may attach a P4 DPI panel object at setup;
 * initialization then follows the ESP-IDF order: DCS setup first, continuous
 * DPI scanout second.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/signal.h>
#include <nuttx/video/mipi_display.h>

#include <arch/chip/esp_mipi_dsi_dpi_panel.h>

#include "ek79007.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define EK79007_RESET_DELAY_US       (20 * 1000)
#define EK79007_SLEEP_EXIT_DELAY_US  (120 * 1000)
#define EK79007_SLEEP_ENTER_DELAY_US (120 * 1000)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The controller vendor sequence is intentionally small.  Panel suppliers
 * may require a different sequence, supplied in config->init_cmds.  The
 * default comes from the EK79007 reference implementation and is limited to
 * controller-level setup; display timing remains a DSI host responsibility.
 */

static const uint8_t g_ek79007_cmd_80[] =
{
  0x8b
};

static const uint8_t g_ek79007_cmd_81[] =
{
  0x78
};

static const uint8_t g_ek79007_cmd_82[] =
{
  0x84
};

static const uint8_t g_ek79007_cmd_83[] =
{
  0x88
};

static const uint8_t g_ek79007_cmd_84[] =
{
  0xa8
};

static const uint8_t g_ek79007_cmd_85[] =
{
  0xe3
};

static const uint8_t g_ek79007_cmd_86[] =
{
  0x88
};

static const struct ek79007_init_cmd_s g_ek79007_default_init_cmds[] =
{
  {0x80, g_ek79007_cmd_80, sizeof(g_ek79007_cmd_80), 0},
  {0x81, g_ek79007_cmd_81, sizeof(g_ek79007_cmd_81), 0},
  {0x82, g_ek79007_cmd_82, sizeof(g_ek79007_cmd_82), 0},
  {0x83, g_ek79007_cmd_83, sizeof(g_ek79007_cmd_83), 0},
  {0x84, g_ek79007_cmd_84, sizeof(g_ek79007_cmd_84), 0},
  {0x85, g_ek79007_cmd_85, sizeof(g_ek79007_cmd_85), 0},
  {0x86, g_ek79007_cmd_86, sizeof(g_ek79007_cmd_86), 0},
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ek79007_write
 ****************************************************************************/

static int ek79007_write(FAR struct ek79007_panel_s *panel, uint8_t cmd,
                         FAR const uint8_t *data, size_t data_len)
{
  ssize_t ret;

  ret = mipi_dsi_dcs_write(panel->dsi, cmd, data, data_len);
  return ret < 0 ? (int)ret : OK;
}

/****************************************************************************
 * Name: ek79007_delay
 ****************************************************************************/

static int ek79007_delay(uint16_t delay_ms)
{
  if (delay_ms == 0)
    {
      return OK;
    }

  return nxsig_usleep((useconds_t)delay_ms * 1000);
}

/****************************************************************************
 * Name: ek79007_check_panel
 ****************************************************************************/

static int ek79007_check_panel(FAR const struct ek79007_panel_s *panel)
{
  if (panel == NULL || panel->dsi == NULL || panel->config == NULL)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: ek79007_send_sequence
 ****************************************************************************/

static int ek79007_send_sequence(
  FAR struct ek79007_panel_s *panel,
  FAR const struct ek79007_init_cmd_s *cmds, size_t ncmds)
{
  size_t i;
  int ret;

  for (i = 0; i < ncmds; i++)
    {
      if (cmds[i].data_len > 0 && cmds[i].data == NULL)
        {
          return -EINVAL;
        }

      ret = ek79007_write(panel, cmds[i].cmd, cmds[i].data,
                          cmds[i].data_len);
      if (ret < 0)
        {
          return ret;
        }

      if (cmds[i].cmd == MIPI_DCS_SET_ADDRESS_MODE &&
          cmds[i].data_len == 1)
        {
          panel->madctl = cmds[i].data[0];
        }

      ret = ek79007_delay(cmds[i].delay_ms);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ek79007_panel_setup
 ****************************************************************************/

int ek79007_panel_setup(FAR struct ek79007_panel_s *panel,
                        FAR struct mipi_dsi_device *dsi,
                        FAR const struct ek79007_panel_config_s *config)
{
  int ret;

  if (panel == NULL || dsi == NULL || config == NULL)
    {
      return -EINVAL;
    }

  if (config->lanes != 2 && config->lanes != 4)
    {
      return -EINVAL;
    }

  if ((config->dpi_panel == NULL) != (config->dpi_config == NULL))
    {
      return -EINVAL;
    }

  ret = mipi_dsi_pixel_format_to_bpp(config->format);
  if (ret < 0)
    {
      return ret;
    }

  panel->dsi = dsi;
  panel->config = config;
  panel->madctl = EK79007_MADCTL_DEFAULT;
  panel->initialized = false;
  panel->display_on = false;
  panel->sleeping = false;
  panel->dpi_panel = config->dpi_panel;

  dsi->lanes = config->lanes;
  dsi->format = config->format;
  dsi->mode_flags = config->mode_flags;
  dsi->hs_rate = config->hs_rate;
  dsi->lp_rate = config->lp_rate;

  if (panel->dpi_panel != NULL)
    {
#ifdef CONFIG_ESPRESSIF_MIPI_DSI_DPI_PANEL
      ret = esp_mipi_dsi_dpi_panel_create(panel->dpi_panel, dsi->host,
                                           config->dpi_config);
      if (ret < 0)
        {
          panel->dpi_panel = NULL;
          return ret;
        }
#else
      panel->dpi_panel = NULL;
      return -ENOTSUP;
#endif
    }

  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_reset
 ****************************************************************************/

int ek79007_panel_reset(FAR struct ek79007_panel_s *panel)
{
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  ret = mipi_dsi_dcs_soft_reset(panel->dsi);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxsig_usleep(EK79007_RESET_DELAY_US);
  if (ret < 0)
    {
      return ret;
    }

  panel->madctl = EK79007_MADCTL_DEFAULT;
  panel->initialized = false;
  panel->display_on = false;
  panel->sleeping = false;
  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_initialize
 ****************************************************************************/

int ek79007_panel_initialize(FAR struct ek79007_panel_s *panel)
{
  FAR const struct ek79007_init_cmd_s *cmds;
  size_t ncmds;
  uint8_t lanes;
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (panel->initialized)
    {
      return panel->sleeping ? -EBUSY : OK;
    }

  if (!panel->config->noinit)
    {
      cmds = panel->config->init_cmds;
      ncmds = panel->config->ninit_cmds;
      if (cmds == NULL)
        {
          /* Keep the default path byte-for-byte ordered like the EK79007
           * ESP-IDF component used to validate the P4X board:
           *
           *   0x80 .. 0x86 -> 0xb2 -> 0x11 -> 120 ms.
           *
           * In particular, moving PAD_CONTROL behind the vendor registers is
           * intentional.  A custom sequence retains the historical contract
           * below: PAD_CONTROL is emitted before caller-provided commands.
           */

          cmds = g_ek79007_default_init_cmds;
          ncmds = sizeof(g_ek79007_default_init_cmds) /
                  sizeof(g_ek79007_default_init_cmds[0]);

          ret = ek79007_send_sequence(panel, cmds, ncmds);
          if (ret < 0)
            {
              return ret;
            }

          lanes = panel->config->lanes == 2 ? EK79007_DCS_PAD_2_LANES :
                                              EK79007_DCS_PAD_4_LANES;
          ret = ek79007_write(panel, EK79007_DCS_PAD_CONTROL, &lanes,
                              sizeof(lanes));
          if (ret < 0)
            {
              return ret;
            }

          ret = ek79007_write(panel, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0);
          if (ret < 0)
            {
              return ret;
            }

          ret = ek79007_delay(120);
          if (ret < 0)
            {
              return ret;
            }
        }
      else
        {
          if (ncmds == 0)
            {
              return -EINVAL;
            }

          lanes = panel->config->lanes == 2 ? EK79007_DCS_PAD_2_LANES :
                                              EK79007_DCS_PAD_4_LANES;
          ret = ek79007_write(panel, EK79007_DCS_PAD_CONTROL, &lanes,
                              sizeof(lanes));
          if (ret < 0)
            {
              return ret;
            }

          ret = ek79007_send_sequence(panel, cmds, ncmds);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  if (panel->dpi_panel != NULL)
    {
#ifdef CONFIG_ESPRESSIF_MIPI_DSI_DPI_PANEL
      ret = esp_mipi_dsi_dpi_panel_initialize(panel->dpi_panel);
      if (ret < 0)
        {
          return ret;
        }
#else
      return -ENOTSUP;
#endif
    }

  panel->initialized = true;
  panel->display_on = false;
  panel->sleeping = false;
  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_set_display
 ****************************************************************************/

int ek79007_panel_set_display(FAR struct ek79007_panel_s *panel, bool on)
{
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (!panel->initialized || panel->sleeping)
    {
      return -EPIPE;
    }

  if (panel->display_on == on)
    {
      return OK;
    }

  ret = on ? mipi_dsi_dcs_set_display_on(panel->dsi) :
             mipi_dsi_dcs_set_display_off(panel->dsi);
  if (ret < 0)
    {
      return ret;
    }

  panel->display_on = on;
  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_draw_bitmap
 ****************************************************************************/

int ek79007_panel_draw_bitmap(FAR struct ek79007_panel_s *panel,
                              FAR const void *color_data,
                              size_t color_data_bytes)
{
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (!panel->initialized || panel->sleeping || panel->dpi_panel == NULL)
    {
      return -EPIPE;
    }

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_DPI_PANEL
  return esp_mipi_dsi_dpi_panel_draw_bitmap(
    panel->dpi_panel, 0, 0, panel->dpi_panel->config.hactive,
    panel->dpi_panel->config.vactive, color_data, color_data_bytes);
#else
  return -ENOTSUP;
#endif
}

/****************************************************************************
 * Name: ek79007_panel_shutdown
 ****************************************************************************/

int ek79007_panel_shutdown(FAR struct ek79007_panel_s *panel)
{
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (panel->display_on)
    {
      ret = ek79007_panel_set_display(panel, false);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (panel->dpi_panel != NULL)
    {
#ifdef CONFIG_ESPRESSIF_MIPI_DSI_DPI_PANEL
      ret = esp_mipi_dsi_dpi_panel_stop(panel->dpi_panel);
      if (ret < 0)
        {
          return ret;
        }

      esp_mipi_dsi_dpi_panel_destroy(panel->dpi_panel);
#endif
      panel->dpi_panel = NULL;
    }

  panel->initialized = false;
  panel->sleeping = false;
  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_set_sleep
 ****************************************************************************/

int ek79007_panel_set_sleep(FAR struct ek79007_panel_s *panel, bool sleep)
{
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (!panel->initialized)
    {
      return -EPIPE;
    }

  if (panel->sleeping == sleep)
    {
      return OK;
    }

  if (sleep)
    {
      ret = ek79007_panel_set_display(panel, false);
      if (ret < 0)
        {
          return ret;
        }

      ret = mipi_dsi_dcs_enter_sleep_mode(panel->dsi);
      if (ret < 0)
        {
          return ret;
        }

      ret = nxsig_usleep(EK79007_SLEEP_ENTER_DELAY_US);
      if (ret < 0)
        {
          return ret;
        }

      panel->sleeping = true;
      return OK;
    }

  ret = mipi_dsi_dcs_exit_sleep_mode(panel->dsi);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxsig_usleep(EK79007_SLEEP_EXIT_DELAY_US);
  if (ret < 0)
    {
      return ret;
    }

  panel->sleeping = false;
  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_set_mirror
 ****************************************************************************/

int ek79007_panel_set_mirror(FAR struct ek79007_panel_s *panel,
                             bool mirror_x, bool mirror_y)
{
  uint8_t madctl;
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (!panel->initialized || panel->sleeping)
    {
      return -EPIPE;
    }

  madctl = panel->madctl;
  if (mirror_x)
    {
      madctl |= EK79007_MADCTL_SHLR;
    }
  else
    {
      madctl &= ~EK79007_MADCTL_SHLR;
    }

  if (mirror_y)
    {
      madctl |= EK79007_MADCTL_UPDN;
    }
  else
    {
      madctl &= ~EK79007_MADCTL_UPDN;
    }

  ret = ek79007_write(panel, MIPI_DCS_SET_ADDRESS_MODE, &madctl,
                      sizeof(madctl));
  if (ret < 0)
    {
      return ret;
    }

  panel->madctl = madctl;
  return OK;
}

/****************************************************************************
 * Name: ek79007_panel_set_invert
 ****************************************************************************/

int ek79007_panel_set_invert(FAR struct ek79007_panel_s *panel,
                             bool invert)
{
  int ret;

  ret = ek79007_check_panel(panel);
  if (ret < 0)
    {
      return ret;
    }

  if (!panel->initialized || panel->sleeping)
    {
      return -EPIPE;
    }

  return ek79007_write(panel,
                       invert ? MIPI_DCS_ENTER_INVERT_MODE :
                                MIPI_DCS_EXIT_INVERT_MODE,
                       NULL, 0);
}
