/****************************************************************************
 * drivers/lcd/ek79007.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * EK79007 MIPI-DSI panel controller driver.
 *
 * The EK79007 receives DCS and vendor commands through the generic NuttX
 * MIPI-DSI framework.  Pixel scanout is intentionally not implemented here:
 * a MIPI-DPI/video host owns the framebuffer, DMA and timing registers.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/signal.h>
#include <nuttx/video/mipi_display.h>

#include "ek79007.h"

#ifdef CONFIG_LCD_EK79007

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
  {MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0, 120},
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

  dsi->lanes = config->lanes;
  dsi->format = config->format;
  dsi->mode_flags = config->mode_flags;
  dsi->hs_rate = config->hs_rate;
  dsi->lp_rate = config->lp_rate;
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

  lanes = panel->config->lanes == 2 ? EK79007_DCS_PAD_2_LANES :
                                      EK79007_DCS_PAD_4_LANES;
  ret = ek79007_write(panel, EK79007_DCS_PAD_CONTROL, &lanes,
                      sizeof(lanes));
  if (ret < 0)
    {
      return ret;
    }

  cmds = panel->config->init_cmds;
  ncmds = panel->config->ninit_cmds;
  if (cmds == NULL)
    {
      cmds = g_ek79007_default_init_cmds;
      ncmds = sizeof(g_ek79007_default_init_cmds) /
              sizeof(g_ek79007_default_init_cmds[0]);
    }
  else if (ncmds == 0)
    {
      return -EINVAL;
    }

  ret = ek79007_send_sequence(panel, cmds, ncmds);
  if (ret < 0)
    {
      return ret;
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

#endif /* CONFIG_LCD_EK79007 */
