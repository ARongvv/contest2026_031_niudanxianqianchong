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
#include <nuttx/video/mipi_dsi.h>

#include <errno.h>
#include <string.h>

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
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool esp_mipi_dsi_timeout(FAR struct esp_mipi_dsi_s *priv,
                                 clock_t start)
{
  return (clock_systime_ticks() - start) >= priv->timeout_ticks;
}

static int esp_mipi_dsi_wait_while(FAR struct esp_mipi_dsi_s *priv,
                                   bool (*predicate)(dsi_host_dev_t *))
{
  clock_t start = clock_systime_ticks();

  while (predicate(priv->hal.host))
    {
      if (esp_mipi_dsi_timeout(priv, start))
        {
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
            priv, mipi_dsi_host_ll_gen_is_write_fifo_full);
          if (ret < 0)
            {
              return ret;
            }

          mipi_dsi_host_ll_gen_write_payload_fifo(priv->hal.host, word);
          payload += bytes;
          remaining -= bytes;
        }
    }

  ret = esp_mipi_dsi_wait_while(priv, mipi_dsi_host_ll_gen_is_cmd_fifo_full);
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

  ret = esp_mipi_dsi_wait_while(priv, mipi_dsi_host_ll_gen_is_cmd_fifo_full);
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
    priv, mipi_dsi_host_ll_gen_is_read_cmd_busy);
  if (ret < 0)
    {
      return ret;
    }

  ret = esp_mipi_dsi_wait_while(
    priv, mipi_dsi_host_ll_gen_is_read_fifo_empty);
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
      mipi_dsi_ll_enable_phy_config_clock(bus, enable);
      mipi_dsi_ll_enable_phy_pllref_clock(bus, enable);

      if (enable)
        {
          mipi_dsi_ll_reset_register(bus);
        }
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
  ssize_t ret;

  if (host != &priv->host || msg == NULL || msg->channel > 3 ||
      (msg->tx_len > 0 && msg->tx_buf == NULL) ||
      (msg->rx_len > 0 && msg->rx_buf == NULL))
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
  else if (msg->rx_len > 0)
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
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  timeout_ms = config->timeout_ms == 0 ? ESP_MIPI_DSI_DEFAULT_TIMEOUT_MS :
                                         config->timeout_ms;
  priv->timeout_ticks = MSEC2TICK(timeout_ms);
  if (priv->timeout_ticks == 0)
    {
      priv->timeout_ticks = 1;
    }

  esp_mipi_dsi_enable_clocks(config->bus, true);
  memset(&hal_config, 0, sizeof(hal_config));
  hal_config.bus_id = config->bus;
  hal_config.lane_bit_rate_mbps = config->lane_bit_rate_mbps;
  hal_config.num_data_lanes = config->lane_num;
  mipi_dsi_hal_init(&priv->hal, &hal_config);
  mipi_dsi_hal_configure_phy_pll(&priv->hal, config->phy_ref_clock_hz,
                                 config->lane_bit_rate_mbps);

  ret = esp_mipi_dsi_wait_pll(priv);
  if (ret < 0)
    {
      mipi_dsi_hal_deinit(&priv->hal);
      esp_mipi_dsi_enable_clocks(config->bus, false);
      esp_ldo_release(priv->phy_ldo);
      priv->phy_ldo = NULL;
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  mipi_dsi_host_ll_enable_video_mode(priv->hal.host, false);
  mipi_dsi_host_ll_enable_tx_eotp(priv->hal.host, true, true);
  mipi_dsi_host_ll_enable_rx_eotp(priv->hal.host, true);
  priv->ready = true;

  if (!priv->registered)
    {
      ret = mipi_dsi_host_register(&priv->host);
      if (ret < 0)
        {
          priv->ready = false;
          mipi_dsi_hal_deinit(&priv->hal);
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
  esp_mipi_dsi_enable_clocks(ESP_MIPI_DSI_BUS0, false);
  priv->ready = false;
  ret = esp_ldo_release(priv->phy_ldo);
  priv->phy_ldo = NULL;
  nxmutex_unlock(&priv->lock);
  return ret;
}
