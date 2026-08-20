/****************************************************************************
 * drivers/lcd/ek79007.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * EK79007 MIPI-DSI panel controller interface.
 *
 * This is a panel-control driver.  It configures the DSI peripheral and
 * sends DCS/vendor commands, but it does not implement framebuffer scanout.
 * The SoC DSI host and board code own DMA, video timing programming, reset
 * GPIO and backlight control.
 ****************************************************************************/

#ifndef __DRIVERS_LCD_EK79007_H
#define __DRIVERS_LCD_EK79007_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/compiler.h>
#include <nuttx/video/mipi_dsi.h>
#include <nuttx/video/videomode.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define EK79007_DCS_PAD_CONTROL       0xb2
#define EK79007_DCS_PAD_2_LANES       0x10
#define EK79007_DCS_PAD_4_LANES       0x00

#define EK79007_MADCTL_SHLR           (1 << 0)
#define EK79007_MADCTL_UPDN           (1 << 1)
#define EK79007_MADCTL_DEFAULT        0x01

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* A vendor DCS command used during panel initialization.  A command with a
 * non-zero data_len must have a non-NULL data pointer.  delay_ms is applied
 * after the command has been accepted by the DSI host.
 */

struct ek79007_init_cmd_s
{
  uint8_t cmd;
  FAR const uint8_t *data;
  size_t data_len;
  uint16_t delay_ms;
};

/* Panel and DSI link configuration.  The timing pointer is descriptive
 * metadata for the future DSI host.  The panel-control driver does not
 * program timing registers, so it may be NULL during command-only tests.
 *
 * init_cmds replaces the default EK79007 sequence when non-NULL.  A custom
 * sequence is responsible for including its own sleep-exit command and any
 * required delay.
 */

struct ek79007_panel_config_s
{
  FAR const struct videomode_s *timing;
  FAR const struct ek79007_init_cmd_s *init_cmds;
  size_t ninit_cmds;
  uint32_t mode_flags;
  uint32_t hs_rate;
  uint32_t lp_rate;
  uint8_t lanes;
  uint8_t format;
};

/* Per-panel state.  The caller owns this object and the configuration passed
 * to ek79007_panel_setup() must remain valid while the panel is in use.
 */

struct ek79007_panel_s
{
  FAR struct mipi_dsi_device *dsi;
  FAR const struct ek79007_panel_config_s *config;
  uint8_t madctl;
  bool initialized;
  bool display_on;
  bool sleeping;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ek79007_panel_setup
 *
 * Description:
 *   Bind a caller-owned EK79007 panel object to a registered MIPI-DSI
 *   peripheral and apply the DSI link fields needed before
 *   mipi_dsi_attach().
 *
 * Input Parameters:
 *   panel  - Caller-owned panel state
 *   dsi    - MIPI-DSI peripheral returned by mipi_dsi_device_register()
 *   config - Panel and DSI link configuration
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_setup(FAR struct ek79007_panel_s *panel,
                        FAR struct mipi_dsi_device *dsi,
                        FAR const struct ek79007_panel_config_s *config);

/****************************************************************************
 * Name: ek79007_panel_reset
 *
 * Description:
 *   Issue the DCS software reset command.  Hardware reset GPIO sequencing is
 *   board-specific and must be completed before this function is called.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_reset(FAR struct ek79007_panel_s *panel);

/****************************************************************************
 * Name: ek79007_panel_initialize
 *
 * Description:
 *   Send EK79007 lane selection and initialization commands.  A successful
 *   call is idempotent.  The panel display remains disabled; call
 *   ek79007_panel_set_display() only after the DSI host starts video output.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_initialize(FAR struct ek79007_panel_s *panel);

/****************************************************************************
 * Name: ek79007_panel_set_display
 *
 * Description:
 *   Enable or disable display output using standard DCS commands.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_set_display(FAR struct ek79007_panel_s *panel, bool on);

/****************************************************************************
 * Name: ek79007_panel_set_sleep
 *
 * Description:
 *   Enter or leave panel sleep mode.  Leaving sleep mode does not turn on
 *   display output; call ek79007_panel_set_display() separately.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_set_sleep(FAR struct ek79007_panel_s *panel, bool sleep);

/****************************************************************************
 * Name: ek79007_panel_set_mirror
 *
 * Description:
 *   Control the EK79007 horizontal and vertical scan direction.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_set_mirror(FAR struct ek79007_panel_s *panel,
                             bool mirror_x, bool mirror_y);

/****************************************************************************
 * Name: ek79007_panel_set_invert
 *
 * Description:
 *   Enable or disable DCS color inversion.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 ****************************************************************************/

int ek79007_panel_set_invert(FAR struct ek79007_panel_s *panel,
                             bool invert);

#endif /* __DRIVERS_LCD_EK79007_H */
