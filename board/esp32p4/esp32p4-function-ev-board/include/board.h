/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __BOARDS_RISCV_ESP32P4_ESP32P4_FUNCTION_EV_BOARD_INCLUDE_BOARD_H
#define __BOARDS_RISCV_ESP32P4_ESP32P4_FUNCTION_EV_BOARD_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/compiler.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GPIO pins used by the GPIO Subsystem */

#define BOARD_NGPIOOUT    2 /* Amount of GPIO Output pins */
#define BOARD_NGPIOINT    1 /* Amount of GPIO Input w/ Interruption pins */

/* ESP32P4-Generic GPIOs ****************************************************/

/* BOOT Button */

#define BUTTON_BOOT  35

/* MIPI-DSI panel control pins.  GPIO27 is the active-low RST_LCD signal on
 * the ESP32-P4X Function EV Board LCD adapter.  GPIO26 controls panel
 * backlight PWM and remains unused by the command-only DSI probe.
 */

#define BOARD_MIPI_DSI_PANEL_RESET_GPIO  27
#define BOARD_MIPI_DSI_BACKLIGHT_GPIO     26

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct mipi_dsi_host;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_ESPRESSIF_MIPI_DSI

/****************************************************************************
 * Name: board_mipi_dsi_initialize
 *
 * Description:
 *   Initialize the P4X command-mode MIPI-DSI host using the board PHY LDO
 *   and link parameters.  This does not enable video scanout or backlight.
 *
 ****************************************************************************/

int board_mipi_dsi_initialize(FAR struct mipi_dsi_host **host);

/****************************************************************************
 * Name: board_mipi_dsi_panel_reset
 *
 * Description:
 *   Apply the board-specific active-low hardware reset sequence for the LCD
 *   adapter.  This does not send panel DCS commands.
 *
 ****************************************************************************/

int board_mipi_dsi_panel_reset(void);

/****************************************************************************
 * Name: board_mipi_dsi_shutdown
 *
 * Description:
 *   Stop the command-mode Host and release its board-owned D-PHY LDO.
 *
 ****************************************************************************/

int board_mipi_dsi_shutdown(FAR struct mipi_dsi_host *host);

#endif /* CONFIG_ESPRESSIF_MIPI_DSI */

#endif /* __BOARDS_RISCV_ESP32P4_ESP32P4_FUNCTION_EV_BOARD_INCLUDE_BOARD_H */
