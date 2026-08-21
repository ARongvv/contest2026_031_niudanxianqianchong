/****************************************************************************
 * chips/esp32p4/common/espressif/esp_mipi_dsi.c
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

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/video/mipi_display.h>
#include <nuttx/video/mipi_dsi.h>

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <syslog.h>

#include "esp_clk_tree.h"
#include "esp_err.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_private/periph_ctrl.h"
#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_host_ll.h"
#include "hal/mipi_dsi_ll.h"
#include "hal/mipi_dsi_phy_ll.h"

#include "esp_mipi_dsi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ESP_MIPI_DSI_MIN_RATE_MBPS      80
#define ESP_MIPI_DSI_MAX_RATE_MBPS      1500
#define ESP_MIPI_DSI_MIN_PHY_REF_HZ      5000000
#define ESP_MIPI_DSI_MAX_PHY_REF_HZ     40000000
#define ESP_MIPI_DSI_DEFAULT_TIMEOUT_MS  CONFIG_ESPRESSIF_MIPI_DSI_TIMEOUT_MS
#define ESP_MIPI_DSI_POLL_US              100
#define ESP_MIPI_DSI_TIMEOUT_CLOCK_MHZ     10
#define ESP_MIPI_DSI_ESCAPE_CLOCK_MHZ      18
#define ESP_MIPI_DSI_MAX_READ_TIME          6000
#define ESP_MIPI_DSI_STOP_WAIT_TIME         0x3f

/* ESP32-P4 revision 3.0 and later use a different D-PHY PLL reference
 * clock mux.  The P4X board uses a revision 3.x chip, which requires XTAL
 * instead of the legacy PLL_F20M selection.
 */

#if defined(CONFIG_ESP32P4_REV_MIN_301)
#  define ESP_MIPI_DSI_PHY_PLLREF_CLK_SRC \
  MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT
#else
#  define ESP_MIPI_DSI_PHY_PLLREF_CLK_SRC \
  MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY
#endif

/* MIPI DSI protocol data type: Set Maximum Return Packet Size. */

#define ESP_MIPI_DSI_DT_SET_MAX_RETURN_PACKET_SIZE 0x37

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct esp_mipi_dsi_s
{
  struct mipi_dsi_host       host;
  mipi_dsi_hal_context_t     hal;
  FAR struct esp_ldo_channel_s *phy_ldo;
  soc_module_clk_t           phy_cfg_clk_src;
  soc_module_clk_t           phy_pllref_clk_src;
  mutex_t                    lock;
  clock_t                    timeout_ticks;
  bool                       registered;
  bool                       ready;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int esp_mipi_dsi_attach(FAR struct mipi_dsi_host *host,
                               FAR struct mipi_dsi_device *device);
static int esp_mipi_dsi_detach(FAR struct mipi_dsi_host *host,
                               FAR struct mipi_dsi_device *device);
static ssize_t esp_mipi_dsi_transfer(FAR struct mipi_dsi_host *host,
                                     FAR const struct mipi_dsi_msg *msg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct mipi_dsi_host_ops g_esp_mipi_dsi_ops =
{
  .attach   = esp_mipi_dsi_attach,
  .detach   = esp_mipi_dsi_detach,
  .transfer = esp_mipi_dsi_transfer,
};

static struct esp_mipi_dsi_s g_esp_mipi_dsi =
{
  .host =
    {
      .bus = ESP_MIPI_DSI_BUS0,
      .ops = &g_esp_mipi_dsi_ops,
    },
  .lock = NXMUTEX_INITIALIZER,
  .phy_cfg_clk_src = SOC_MOD_CLK_INVALID,
  .phy_pllref_clk_src = SOC_MOD_CLK_INVALID,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool esp_mipi_dsi_timeout(FAR struct esp_mipi_dsi_s *priv,
                                 clock_t start)
{
  return (clock_systime_ticks() - start) >= priv->timeout_ticks;
}

static int esp_mipi_dsi_clock_result(esp_err_t result)
{
  if (result == ESP_OK)
    {
      return OK;
    }

  if (result == ESP_ERR_INVALID_ARG)
    {
      return -EINVAL;
    }

  if (result == ESP_ERR_INVALID_STATE)
    {
      return -EALREADY;
    }

  return -EIO;
}

static void esp_mipi_dsi_dump_status(FAR struct esp_mipi_dsi_s *priv,
                                     FAR const char *stage)
{
  FAR dsi_host_dev_t *host = priv->hal.host;

  if (host == NULL)
    {
      syslog(LOG_ERR, "ERROR: MIPI-DSI timeout stage=%s host unavailable\n",
             stage);
      return;
    }

  syslog(LOG_ERR,
         "ERROR: MIPI-DSI timeout stage=%s int_st0=%08" PRIx32
         " int_st1=%08" PRIx32 " phy_status=%08" PRIx32
         " cmd_pkt_status=%08" PRIx32 "\n",
         stage, host->int_st0.val, host->int_st1.val,
         host->phy_status.val, host->cmd_pkt_status.val);
}

static int esp_mipi_dsi_wait_while(FAR struct esp_mipi_dsi_s *priv,
                                   bool (*predicate)(dsi_host_dev_t *),
                                   FAR const char *stage)
{
  clock_t start = clock_systime_ticks();

  while (predicate(priv->hal.host))
    {
      if (esp_mipi_dsi_timeout(priv, start))
        {
          esp_mipi_dsi_dump_status(priv, stage);
          return -ETIMEDOUT;
        }

      nxsig_usleep(ESP_MIPI_DSI_POLL_US);
    }

  return OK;
}

static int esp_mipi_dsi_wait_pll(FAR struct esp_mipi_dsi_s *priv)
{
  clock_t start = clock_systime_ticks();

  while (!mipi_dsi_phy_ll_is_pll_locked(priv->hal.host))
    {
      if (esp_mipi_dsi_timeout(priv, start))
        {
          esp_mipi_dsi_dump_status(priv, "phy_pll_lock");
          return -ETIMEDOUT;
        }

      nxsig_usleep(ESP_MIPI_DSI_POLL_US);
    }

  return OK;
}

static int esp_mipi_dsi_wait_lanes_stopped(
  FAR struct esp_mipi_dsi_s *priv, uint8_t lane_num)
{
  clock_t start = clock_systime_ticks();

  while (!mipi_dsi_phy_ll_are_lanes_stopped(priv->hal.host, lane_num))
    {
      if (esp_mipi_dsi_timeout(priv, start))
        {
          esp_mipi_dsi_dump_status(priv, "phy_lanes_stop");
          return -ETIMEDOUT;
        }

      nxsig_usleep(ESP_MIPI_DSI_POLL_US);
    }

  return OK;
}

static int esp_mipi_dsi_write_packet(
  FAR struct esp_mipi_dsi_s *priv,
  FAR const struct mipi_dsi_packet *packet,
  uint8_t channel, uint8_t type)
{
  FAR const uint8_t *payload = packet->payload;
  size_t remaining = packet->payload_length;
  uint32_t word;
  size_t bytes;
  int ret;

  if (mipi_dsi_packet_format_is_long(type))
    {
      while (remaining > 0)
        {
          bytes = remaining > sizeof(word) ? sizeof(word) : remaining;
          word = 0;
          memcpy(&word, payload, bytes);

          ret = esp_mipi_dsi_wait_while(
            priv, mipi_dsi_host_ll_gen_is_write_fifo_full,
            "write_payload_fifo");
          if (ret < 0)
            {
              return ret;
            }

          mipi_dsi_host_ll_gen_write_payload_fifo(priv->hal.host, word);
          payload += bytes;
          remaining -= bytes;
        }
    }

  ret = esp_mipi_dsi_wait_while(priv, mipi_dsi_host_ll_gen_is_cmd_fifo_full,
                                 "write_command_fifo");
  if (ret < 0)
    {
      return ret;
    }

  mipi_dsi_host_ll_gen_set_packet_header(priv->hal.host, channel,
    (mipi_dsi_data_type_t)type, packet->header[2], packet->header[1]);
  return OK;
}

static int esp_mipi_dsi_read_packet(FAR struct esp_mipi_dsi_s *priv,
                                    FAR const struct mipi_dsi_packet *packet,
                                    FAR const struct mipi_dsi_msg *msg)
{
  FAR uint8_t *buffer = msg->rx_buf;
  uint32_t word;
  size_t copied = 0;
  uint8_t index;
  int ret;

  if (msg->rx_len > UINT16_MAX)
    {
      return -EMSGSIZE;
    }

  /* The maximum-return-size command must complete before the read request.
   * Command mode remains selected in M1; video mode is configured in M2.
   */

  ret = esp_mipi_dsi_wait_while(priv, mipi_dsi_host_ll_gen_is_cmd_fifo_full,
                                 "set_max_return_packet_size");
  if (ret < 0)
    {
      return ret;
    }

  mipi_dsi_host_ll_gen_set_packet_header(priv->hal.host, msg->channel,
    (mipi_dsi_data_type_t)ESP_MIPI_DSI_DT_SET_MAX_RETURN_PACKET_SIZE,
    (uint8_t)(msg->rx_len >> 8), (uint8_t)msg->rx_len);
  mipi_dsi_host_ll_enable_video_mode(priv->hal.host, false);
  mipi_dsi_host_ll_enable_bta(priv->hal.host, true);
  mipi_dsi_host_ll_gen_set_rx_vcid(priv->hal.host, msg->channel);

  ret = esp_mipi_dsi_write_packet(priv, packet, msg->channel, msg->type);
  if (ret < 0)
    {
      return ret;
    }

  ret = esp_mipi_dsi_wait_while(
    priv, mipi_dsi_host_ll_gen_is_read_cmd_busy, "read_command_complete");
  if (ret < 0)
    {
      return ret;
    }

  ret = esp_mipi_dsi_wait_while(
    priv, mipi_dsi_host_ll_gen_is_read_fifo_empty, "read_response_fifo");
  if (ret < 0)
    {
      return ret;
    }

  while (!mipi_dsi_host_ll_gen_is_read_fifo_empty(priv->hal.host))
    {
      word = mipi_dsi_host_ll_gen_read_payload_fifo(priv->hal.host);
      for (index = 0; index < sizeof(word) && copied < msg->rx_len; index++)
        {
          buffer[copied++] = word & 0xff;
          word >>= 8;
        }
    }

  return copied;
}

static void esp_mipi_dsi_enable_clocks(uint8_t bus, bool enable)
{
  PERIPH_RCC_ATOMIC()
    {
      mipi_dsi_ll_enable_bus_clock(bus, enable);

      if (enable)
        {
          mipi_dsi_ll_reset_register(bus);
        }
    }
}

static int esp_mipi_dsi_enable_phy_clock_sources(
  FAR struct esp_mipi_dsi_s *priv, uint8_t bus, FAR uint32_t *ref_hz)
{
  soc_module_clk_t cfg_source =
    (soc_module_clk_t)MIPI_DSI_PHY_CFG_CLK_SRC_DEFAULT;
  soc_module_clk_t pllref_source =
    (soc_module_clk_t)ESP_MIPI_DSI_PHY_PLLREF_CLK_SRC;
  int ret;

  ret = esp_mipi_dsi_clock_result(
    esp_clk_tree_enable_src(pllref_source, true));
  if (ret < 0)
    {
      return ret;
    }

  ret = esp_mipi_dsi_clock_result(esp_clk_tree_enable_src(cfg_source, true));
  if (ret < 0)
    {
      esp_clk_tree_enable_src(pllref_source, false);
      return ret;
    }

  PERIPH_RCC_ATOMIC()
    {
      mipi_dsi_ll_set_phy_config_clock_source(
        bus, (soc_periph_mipi_dsi_phy_cfg_clk_src_t)cfg_source);
      mipi_dsi_ll_enable_phy_config_clock(bus, true);
      mipi_dsi_ll_set_phy_pllref_clock_source(
        bus, (mipi_dsi_phy_pllref_clock_source_t)pllref_source);
      mipi_dsi_ll_set_phy_pll_ref_clock_div(bus, 1);
      mipi_dsi_ll_enable_phy_pllref_clock(bus, true);
    }

  ret = esp_mipi_dsi_clock_result(esp_clk_tree_src_get_freq_hz(
    pllref_source, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, ref_hz));
  if (ret < 0)
    {
      PERIPH_RCC_ATOMIC()
        {
          mipi_dsi_ll_enable_phy_pllref_clock(bus, false);
          mipi_dsi_ll_enable_phy_config_clock(bus, false);
        }

      esp_clk_tree_enable_src(cfg_source, false);
      esp_clk_tree_enable_src(pllref_source, false);
      return ret;
    }

  priv->phy_cfg_clk_src = cfg_source;
  priv->phy_pllref_clk_src = pllref_source;
  syslog(LOG_INFO,
         "INFO: MIPI-DSI PHY clock sources cfg=%d pllref=%d ref_hz=%" PRIu32
         "\n", cfg_source, pllref_source, *ref_hz);
  return OK;
}

static void esp_mipi_dsi_disable_phy_clock_sources(
  FAR struct esp_mipi_dsi_s *priv, uint8_t bus)
{
  PERIPH_RCC_ATOMIC()
    {
      mipi_dsi_ll_enable_phy_pllref_clock(bus, false);
      mipi_dsi_ll_enable_phy_config_clock(bus, false);
    }

  if (priv->phy_cfg_clk_src != SOC_MOD_CLK_INVALID)
    {
      esp_clk_tree_enable_src(priv->phy_cfg_clk_src, false);
      priv->phy_cfg_clk_src = SOC_MOD_CLK_INVALID;
    }

  if (priv->phy_pllref_clk_src != SOC_MOD_CLK_INVALID)
    {
      esp_clk_tree_enable_src(priv->phy_pllref_clk_src, false);
      priv->phy_pllref_clk_src = SOC_MOD_CLK_INVALID;
    }
}

static void esp_mipi_dsi_configure_command_mode(
  FAR struct esp_mipi_dsi_s *priv, uint32_t lane_bit_rate_mbps)
{
  FAR dsi_host_dev_t *host = priv->hal.host;
  uint32_t lane_byte_clock_mhz = lane_bit_rate_mbps / 8;
  uint32_t escape_clock_div;

  escape_clock_div =
    (lane_byte_clock_mhz + ESP_MIPI_DSI_ESCAPE_CLOCK_MHZ - 1) /
    ESP_MIPI_DSI_ESCAPE_CLOCK_MHZ;
  if (escape_clock_div < 2)
    {
      escape_clock_div = 2;
    }

  mipi_dsi_host_ll_enable_video_mode(host, false);
  mipi_dsi_host_ll_set_clock_lane_state(
    host, MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);
  mipi_dsi_phy_ll_set_switch_time(host, 50, 104, 46, 128);
  mipi_dsi_host_ll_enable_rx_crc(host, true);
  mipi_dsi_host_ll_enable_rx_ecc(host, true);
  mipi_dsi_host_ll_enable_tx_eotp(host, true, false);
  mipi_dsi_host_ll_enable_rx_eotp(host, true);
  mipi_dsi_host_ll_set_timeout_clock_division(
    host, (lane_byte_clock_mhz + ESP_MIPI_DSI_TIMEOUT_CLOCK_MHZ - 1) /
    ESP_MIPI_DSI_TIMEOUT_CLOCK_MHZ);
  mipi_dsi_host_ll_set_escape_clock_division(host, escape_clock_div);
  mipi_dsi_host_ll_set_timeout_count(host, 0, 0, 0, 0, 0, 0, 0);
  mipi_dsi_phy_ll_set_max_read_time(host, ESP_MIPI_DSI_MAX_READ_TIME);
  mipi_dsi_phy_ll_set_stop_wait_time(host, ESP_MIPI_DSI_STOP_WAIT_TIME);
}

static bool esp_mipi_dsi_message_is_read(uint8_t type)
{
  /* A DSI data type, rather than incidental receive-buffer fields, defines
   * whether the Host must start BTA and wait for an RX payload.  In
   * particular, DCS write helpers only require transmit fields to be set.
   */

  switch (type)
    {
      case MIPI_DSI_GENERIC_READ_0_PARAM:
      case MIPI_DSI_GENERIC_READ_1_PARAM:
      case MIPI_DSI_GENERIC_READ_2_PARAM:
      case MIPI_DSI_DCS_READ_0_PARAM:
        return true;

      default:
        return false;
    }
}

static int esp_mipi_dsi_attach(FAR struct mipi_dsi_host *host,
                               FAR struct mipi_dsi_device *device)
{
  FAR struct esp_mipi_dsi_s *priv = &g_esp_mipi_dsi;
  int ret;

  if (host != &priv->host || device == NULL || device->channel > 3 ||
      device->lanes == 0 || device->lanes > ESP_MIPI_DSI_MAX_DATA_LANES)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = priv->ready ? OK : -ESHUTDOWN;
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int esp_mipi_dsi_detach(FAR struct mipi_dsi_host *host,
                               FAR struct mipi_dsi_device *device)
{
  if (host != &g_esp_mipi_dsi.host || device == NULL)
    {
      return -EINVAL;
    }

  return OK;
}

static ssize_t esp_mipi_dsi_transfer(FAR struct mipi_dsi_host *host,
                                     FAR const struct mipi_dsi_msg *msg)
{
  FAR struct esp_mipi_dsi_s *priv = &g_esp_mipi_dsi;
  struct mipi_dsi_packet packet;
  bool is_read;
  ssize_t ret;

  if (host != &priv->host || msg == NULL || msg->channel > 3 ||
      (msg->tx_len > 0 && msg->tx_buf == NULL))
    {
      return -EINVAL;
    }

  is_read = esp_mipi_dsi_message_is_read(msg->type);
  if (is_read && (msg->rx_len == 0 || msg->rx_buf == NULL))
    {
      return -EINVAL;
    }

  ret = mipi_dsi_create_packet(&packet, msg);
  if (ret < 0)
    {
      return ret;
    }

  if (!mipi_dsi_packet_format_is_short(msg->type) &&
      !mipi_dsi_packet_format_is_long(msg->type))
    {
      return -ENOTSUP;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->ready)
    {
      ret = -ESHUTDOWN;
    }
  else if (is_read)
    {
      ret = esp_mipi_dsi_read_packet(priv, &packet, msg);
    }
  else
    {
      ret = esp_mipi_dsi_write_packet(priv, &packet, msg->channel,
                                      msg->type);
      if (ret == OK)
        {
          ret = msg->tx_len;
        }
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp_mipi_dsi_host_initialize(
  FAR const struct esp_mipi_dsi_host_config_s *config,
  FAR struct mipi_dsi_host **host)
{
  FAR struct esp_mipi_dsi_s *priv = &g_esp_mipi_dsi;
  mipi_dsi_hal_config_t hal_config;
  uint32_t phy_ref_clock_hz;
  uint32_t timeout_ms;
  int ret;

  if (config == NULL || host == NULL || config->bus != ESP_MIPI_DSI_BUS0 ||
      config->lane_num == 0 ||
      config->lane_num > ESP_MIPI_DSI_MAX_DATA_LANES ||
      config->lane_bit_rate_mbps < ESP_MIPI_DSI_MIN_RATE_MBPS ||
      config->lane_bit_rate_mbps > ESP_MIPI_DSI_MAX_RATE_MBPS ||
      config->phy_ref_clock_hz < ESP_MIPI_DSI_MIN_PHY_REF_HZ ||
      config->phy_ref_clock_hz > ESP_MIPI_DSI_MAX_PHY_REF_HZ ||
      config->phy_ldo.voltage_mv != ESP_MIPI_DSI_DPHY_VOLTAGE_MV)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->ready)
    {
      *host = &priv->host;
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  ret = esp_ldo_acquire(&config->phy_ldo, &priv->phy_ldo);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: MIPI-DSI D-PHY LDO acquire failed channel=%u"
             " voltage_mv=%d ret=%d\n",
             config->phy_ldo.channel_id, config->phy_ldo.voltage_mv, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  syslog(LOG_INFO,
         "INFO: MIPI-DSI D-PHY LDO ready channel=%u voltage_mv=%d"
         " lanes=%u rate_mbps=%" PRIu32 " ref_hz=%" PRIu32 "\n",
         config->phy_ldo.channel_id, config->phy_ldo.voltage_mv,
         config->lane_num, config->lane_bit_rate_mbps,
         config->phy_ref_clock_hz);

  timeout_ms = config->timeout_ms == 0 ? ESP_MIPI_DSI_DEFAULT_TIMEOUT_MS :
                                         config->timeout_ms;
  priv->timeout_ticks = MSEC2TICK(timeout_ms);
  if (priv->timeout_ticks == 0)
    {
      priv->timeout_ticks = 1;
    }

  esp_mipi_dsi_enable_clocks(config->bus, true);
  syslog(LOG_INFO, "INFO: MIPI-DSI clocks enabled bus=%u\n", config->bus);

  ret = esp_mipi_dsi_enable_phy_clock_sources(priv, config->bus,
                                               &phy_ref_clock_hz);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: MIPI-DSI PHY clock setup ret=%d\n", ret);
      esp_mipi_dsi_enable_clocks(config->bus, false);
      esp_ldo_release(priv->phy_ldo);
      priv->phy_ldo = NULL;
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  if (phy_ref_clock_hz != config->phy_ref_clock_hz)
    {
      syslog(LOG_WARNING,
             "WARNING: MIPI-DSI configured ref_hz=%" PRIu32
             " differs from active ref_hz=%" PRIu32 "\n",
             config->phy_ref_clock_hz, phy_ref_clock_hz);
    }

  memset(&hal_config, 0, sizeof(hal_config));
  hal_config.bus_id = config->bus;
  hal_config.lane_bit_rate_mbps = config->lane_bit_rate_mbps;
  hal_config.num_data_lanes = config->lane_num;
  mipi_dsi_hal_init(&priv->hal, &hal_config);
  mipi_dsi_hal_configure_phy_pll(&priv->hal, phy_ref_clock_hz,
                                 config->lane_bit_rate_mbps);

  syslog(LOG_INFO,
         "INFO: MIPI-DSI PHY PLL configured timeout_ms=%" PRIu32 "\n",
         timeout_ms);

  ret = esp_mipi_dsi_wait_pll(priv);
  if (ret < 0)
    {
      mipi_dsi_hal_deinit(&priv->hal);
      esp_mipi_dsi_disable_phy_clock_sources(priv, config->bus);
      esp_mipi_dsi_enable_clocks(config->bus, false);
      esp_ldo_release(priv->phy_ldo);
      priv->phy_ldo = NULL;
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  ret = esp_mipi_dsi_wait_lanes_stopped(priv, config->lane_num);
  if (ret < 0)
    {
      mipi_dsi_hal_deinit(&priv->hal);
      esp_mipi_dsi_disable_phy_clock_sources(priv, config->bus);
      esp_mipi_dsi_enable_clocks(config->bus, false);
      esp_ldo_release(priv->phy_ldo);
      priv->phy_ldo = NULL;
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  esp_mipi_dsi_configure_command_mode(priv, config->lane_bit_rate_mbps);
  priv->ready = true;

  syslog(LOG_INFO, "INFO: MIPI-DSI Host ready bus=%u\n", config->bus);

  if (!priv->registered)
    {
      ret = mipi_dsi_host_register(&priv->host);
      if (ret < 0)
        {
          priv->ready = false;
          mipi_dsi_hal_deinit(&priv->hal);
          esp_mipi_dsi_disable_phy_clock_sources(priv, config->bus);
          esp_mipi_dsi_enable_clocks(config->bus, false);
          esp_ldo_release(priv->phy_ldo);
          priv->phy_ldo = NULL;
          nxmutex_unlock(&priv->lock);
          return ret;
        }

      priv->registered = true;
    }

  *host = &priv->host;
  nxmutex_unlock(&priv->lock);
  return OK;
}

int esp_mipi_dsi_host_shutdown(FAR struct mipi_dsi_host *host)
{
  FAR struct esp_mipi_dsi_s *priv = &g_esp_mipi_dsi;
  int ret;

  if (host != &priv->host)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->ready)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  mipi_dsi_hal_deinit(&priv->hal);
  esp_mipi_dsi_disable_phy_clock_sources(priv, ESP_MIPI_DSI_BUS0);
  esp_mipi_dsi_enable_clocks(ESP_MIPI_DSI_BUS0, false);
  priv->ready = false;
  ret = esp_ldo_release(priv->phy_ldo);
  priv->phy_ldo = NULL;
  nxmutex_unlock(&priv->lock);
  return ret;
}
