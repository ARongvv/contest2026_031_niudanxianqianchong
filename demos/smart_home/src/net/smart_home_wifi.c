/****************************************************************************
 * smart_home/src/net/smart_home_wifi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/socket.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <nuttx/net/dns.h>
#include <nuttx/wireless/wireless.h>

#include <syslog.h>

#include "netutils/netlib.h"
#include "wireless/wapi.h"

#include "smart_home_wifi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WIFI_IFNAME       "wlan0"
#define WAPI_CONFIG_PATH  "/data/wapi.conf"

/* Default values matching the current manual setup:
 *   wapi mode wlan0 2       -> WAPI_MODE_MANAGED
 *   wapi psk wlan0 xxx 3    -> WPA_ALG_CCMP
 *   wapi essid wlan0 xxx 1  -> WAPI_ESSID_ON
 */

#ifndef CONFIG_SMART_HOME_WIFI_SSID
#  define CONFIG_SMART_HOME_WIFI_SSID      "123"
#endif

#ifndef CONFIG_SMART_HOME_WIFI_PASSWORD
#  define CONFIG_SMART_HOME_WIFI_PASSWORD  "88888888"
#endif

#ifndef CONFIG_SMART_HOME_WIFI_RETRY_COUNT
#  define CONFIG_SMART_HOME_WIFI_RETRY_COUNT 3
#endif

#define DNS_VERIFY_HOSTNAME  "api.deepseek.com"
#define RETRY_DELAY_SEC      2

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wifi_verify_dns
 *
 * Description:
 *   Verify DNS resolution works by resolving a known hostname.
 *
 ****************************************************************************/

static int wifi_verify_dns(void)
{
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(DNS_VERIFY_HOSTNAME, NULL, &hints, &result);
  if (ret != 0)
    {
      syslog(LOG_WARNING, "WiFi: DNS verify failed for %s: %d\n",
             DNS_VERIFY_HOSTNAME, ret);
      return SMART_HOME_WIFI_ERR_DNS;
    }

  freeaddrinfo(result);
  syslog(LOG_INFO, "WiFi: DNS verify OK\n");
  return SMART_HOME_WIFI_OK;
}

/****************************************************************************
 * Name: wifi_connect_once
 *
 * Description:
 *   Single attempt: associate + DHCP + DNS verify.
 *
 ****************************************************************************/

static int wifi_connect_once(FAR const struct wpa_wconfig_s *conf)
{
  int ret;

  /* Step 0: Bring interface up */

  ret = netlib_ifup(WIFI_IFNAME);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "WiFi: ifup %s failed: %d (may already be up)\n",
             WIFI_IFNAME, ret);
    }

  /* Step 1: Associate with AP */

  syslog(LOG_INFO, "WiFi: connecting to \"%s\"...\n", conf->ssid);

  ret = wpa_driver_wext_associate((FAR struct wpa_wconfig_s *)conf);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WiFi: associate failed: %d\n", ret);
      return SMART_HOME_WIFI_ERR_ASSOC;
    }

  syslog(LOG_INFO, "WiFi: associated, obtaining IP...\n");

  /* Step 2: DHCP to get IP address */

  ret = netlib_obtain_ipv4addr(WIFI_IFNAME);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WiFi: DHCP failed: %d\n", ret);
      return SMART_HOME_WIFI_ERR_DHCP;
    }

  syslog(LOG_INFO, "WiFi: DHCP done\n");

  /* Step 3: Verify DNS works */

  ret = wifi_verify_dns();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "WiFi: DNS not ready, retrying...\n");
      return ret;
    }

  return SMART_HOME_WIFI_OK;
}

/****************************************************************************
 * Name: wifi_fill_default_config
 *
 * Description:
 *   Fill wpa_wconfig_s with Kconfig compile-time defaults.
 *
 ****************************************************************************/

static void wifi_fill_default_config(FAR struct wpa_wconfig_s *conf)
{
  memset(conf, 0, sizeof(*conf));

  conf->sta_mode    = WAPI_MODE_MANAGED;
  conf->auth_wpa    = IW_AUTH_WPA_VERSION_WPA2;
  conf->cipher_mode = IW_AUTH_CIPHER_CCMP;
  conf->alg         = WPA_ALG_CCMP;
  conf->ifname      = WIFI_IFNAME;
  conf->ssid        = CONFIG_SMART_HOME_WIFI_SSID;
  conf->ssidlen     = strlen(CONFIG_SMART_HOME_WIFI_SSID);
  conf->passphrase  = CONFIG_SMART_HOME_WIFI_PASSWORD;
  conf->phraselen   = strlen(CONFIG_SMART_HOME_WIFI_PASSWORD);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: smart_home_wifi_connect
 ****************************************************************************/

int smart_home_wifi_connect(void)
{
  struct wpa_wconfig_s conf;
  FAR void *load = NULL;
  int retry;
  int ret;

  /* Try to load config from /data/wapi.conf first */

#ifdef CONFIG_WIRELESS_WAPI_INITCONF
  load = wapi_load_config(WIFI_IFNAME, WAPI_CONFIG_PATH, &conf);
#endif

  if (load != NULL)
    {
      syslog(LOG_INFO, "WiFi: using config from %s\n", WAPI_CONFIG_PATH);
    }
  else
    {
      syslog(LOG_INFO, "WiFi: using Kconfig defaults (SSID=\"%s\")\n",
             CONFIG_SMART_HOME_WIFI_SSID);
      wifi_fill_default_config(&conf);
    }

  /* Retry loop */

  for (retry = 0; retry < CONFIG_SMART_HOME_WIFI_RETRY_COUNT; retry++)
    {
      if (retry > 0)
        {
          syslog(LOG_INFO, "WiFi: retry %d/%d in %d sec...\n",
                 retry + 1, CONFIG_SMART_HOME_WIFI_RETRY_COUNT,
                 RETRY_DELAY_SEC);
          sleep(RETRY_DELAY_SEC);
        }

      ret = wifi_connect_once(&conf);
      if (ret == SMART_HOME_WIFI_OK)
        {
          syslog(LOG_INFO, "WiFi: connected!\n");
          break;
        }
    }

  /* Cleanup WAPI config load handle */

#ifdef CONFIG_WIRELESS_WAPI_INITCONF
  if (load != NULL)
    {
      wapi_unload_config(load);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: smart_home_wifi_is_connected
 ****************************************************************************/

bool smart_home_wifi_is_connected(void)
{
  struct in_addr addr;

  memset(&addr, 0, sizeof(addr));
  netlib_get_ipv4addr(WIFI_IFNAME, &addr);

  return addr.s_addr != 0;
}
