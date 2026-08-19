/****************************************************************************
 * smart_home/src/net/smart_home_network.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/socket.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <syslog.h>

#include "netutils/netlib.h"

#include "smart_home_network.h"
#include "smart_home_wifi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SMART_HOME_ETH_IFNAME     "eth0"
#define SMART_HOME_WIFI_IFNAME    "wlan0"
#define SMART_HOME_DNS_HOSTNAME   "api.deepseek.com"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool smart_home_network_is_wifi_platform(void)
{
#if defined(CONFIG_ARCH_CHIP_ESP32S3) || defined(CONFIG_ESP32S3_WIFI)
  return true;
#else
  return false;
#endif
}

static bool smart_home_network_is_simulator_platform(void)
{
#if defined(CONFIG_ARCH_CHIP_GOLDFISH_ARM64) || \
    defined(CONFIG_ARCH_CHIP_GOLDFISH_ARM) || \
    defined(CONFIG_ARCH_CHIP_GOLDFISH_X86_64)
  return true;
#else
  return false;
#endif
}

static bool smart_home_network_has_ip(FAR const char *ifname)
{
  struct in_addr addr;

  if (ifname == NULL)
    {
      return false;
    }

  memset(&addr, 0, sizeof(addr));
  netlib_get_ipv4addr(ifname, &addr);
  return addr.s_addr != 0;
}

static void smart_home_network_log_link_state(FAR const char *phase,
                                              FAR const char *ifname,
                                              FAR const smart_home_network_status_t *status,
                                              int result)
{
  struct in_addr ipaddr;
  struct in_addr gateway;
  char iptext[INET_ADDRSTRLEN] = "none";
  char gatewaytext[INET_ADDRSTRLEN] = "none";

  memset(&ipaddr, 0, sizeof(ipaddr));
  memset(&gateway, 0, sizeof(gateway));
  if (ifname != NULL && netlib_get_ipv4addr(ifname, &ipaddr) == 0 &&
      ipaddr.s_addr != 0)
    {
      (void)inet_ntop(AF_INET, &ipaddr, iptext, sizeof(iptext));
    }

  if (ifname != NULL && netlib_get_dripv4addr(ifname, &gateway) == 0 &&
      gateway.s_addr != 0)
    {
      (void)inet_ntop(AF_INET, &gateway, gatewaytext, sizeof(gatewaytext));
    }

  syslog(LOG_INFO,
         "Network %s: if=%s ip=%s gateway=%s result=%d init=%d ip_status=%d "
         "dns=%d online=%d\n",
         phase, ifname ? ifname : "none", iptext, gatewaytext, result,
         status ? status->init_status : SMART_HOME_NETWORK_STATUS_UNKNOWN,
         status ? status->ip_status : SMART_HOME_NETWORK_STATUS_UNKNOWN,
         status ? status->dns_status : SMART_HOME_NETWORK_STATUS_UNKNOWN,
         status && status->online ? 1 : 0);
  fprintf(stderr,
          "[smart_home_net] %s if=%s ip=%s gateway=%s result=%d init=%d "
          "ip_status=%d dns=%d online=%d\n",
          phase, ifname ? ifname : "none", iptext, gatewaytext, result,
          status ? status->init_status : SMART_HOME_NETWORK_STATUS_UNKNOWN,
          status ? status->ip_status : SMART_HOME_NETWORK_STATUS_UNKNOWN,
          status ? status->dns_status : SMART_HOME_NETWORK_STATUS_UNKNOWN,
          status && status->online ? 1 : 0);
}

static int smart_home_network_verify_dns(void)
{
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(SMART_HOME_DNS_HOSTNAME, NULL, &hints, &result);
  if (ret != 0)
    {
      syslog(LOG_WARNING, "Network: DNS verify failed for %s: %d\n",
             SMART_HOME_DNS_HOSTNAME, ret);
      return SMART_HOME_NETWORK_ERR_DNS;
    }

  freeaddrinfo(result);
  syslog(LOG_INFO, "Network: DNS verify OK\n");
  return SMART_HOME_NETWORK_OK;
}

static int smart_home_network_init_ethernet(
  FAR smart_home_network_status_t *status)
{
  int ret;

  ret = netlib_ifup(status->ifname);
  status->init_status = ret < 0 ? SMART_HOME_NETWORK_ERR_IFUP :
                                  SMART_HOME_NETWORK_OK;
  if (ret < 0)
    {
      syslog(LOG_WARNING, "Network: ifup %s failed: %d\n",
             status->ifname, ret);
    }

  if (!smart_home_network_has_ip(status->ifname))
    {
      ret = netlib_obtain_ipv4addr(status->ifname);
      status->ip_status = ret < 0 ? SMART_HOME_NETWORK_ERR_DHCP :
                                    SMART_HOME_NETWORK_OK;
      if (ret < 0)
        {
          syslog(LOG_WARNING, "Network: DHCP on %s failed: %d\n",
                 status->ifname, ret);
        }
    }
  else
    {
      status->ip_status = SMART_HOME_NETWORK_OK;
    }

  return smart_home_network_probe(status);
}

static int smart_home_network_init_wifi(FAR smart_home_network_status_t *status)
{
  int ret;

  /*
   * A smart_home restart must not disrupt an already usable Wi-Fi session.
   * In particular, reassociating and restarting DHCP can briefly invalidate
   * sockets owned by the UI, model, or MCP workers.  Reuse the current
   * address when DNS proves that it is usable; reconnect only as recovery.
   */

  if (smart_home_wifi_is_connected())
    {
      syslog(LOG_INFO,
             "Network: WiFi already has an IP; probing existing connection\n");

      status->init_status = SMART_HOME_NETWORK_OK;
      ret = smart_home_network_probe(status);
      if (ret == SMART_HOME_NETWORK_OK)
        {
          syslog(LOG_INFO, "Network: reusing existing WiFi connection\n");
          return ret;
        }

      syslog(LOG_WARNING,
             "Network: existing WiFi probe failed: %d; reconnecting\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "Network: WiFi has no IP; connecting\n");
    }

  status->init_status = smart_home_wifi_connect();
  if (status->init_status < 0)
    {
      syslog(LOG_WARNING, "Network: WiFi auto-connect failed: %d\n",
             status->init_status);
    }

  return smart_home_network_probe(status);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void smart_home_network_status_init(FAR smart_home_network_status_t *status)
{
  if (status == NULL)
    {
      return;
    }

  memset(status, 0, sizeof(*status));
  status->platform = SMART_HOME_NETWORK_PLATFORM_UNKNOWN;
  status->backend = SMART_HOME_NETWORK_BACKEND_UNKNOWN;
  status->ifname = NULL;
  status->init_status = SMART_HOME_NETWORK_STATUS_UNKNOWN;
  status->ip_status = SMART_HOME_NETWORK_STATUS_UNKNOWN;
  status->dns_status = SMART_HOME_NETWORK_STATUS_UNKNOWN;
  status->online = false;
}

int smart_home_network_init(FAR smart_home_network_status_t *status)
{
  int ret;

  if (status == NULL)
    {
      return SMART_HOME_NETWORK_ERR_NO_IP;
    }

  smart_home_network_status_init(status);

  if (smart_home_network_is_wifi_platform())
    {
      status->platform = SMART_HOME_NETWORK_PLATFORM_DEVICE_WIFI;
      status->backend = SMART_HOME_NETWORK_BACKEND_WIFI;
      status->ifname = SMART_HOME_WIFI_IFNAME;
      smart_home_network_log_link_state("init-begin", status->ifname,
                                        status, SMART_HOME_NETWORK_OK);
      ret = smart_home_network_init_wifi(status);
      smart_home_network_log_link_state("init-end", status->ifname,
                                        status, ret);
      return ret;
    }

  if (smart_home_network_is_simulator_platform())
    {
      status->platform = SMART_HOME_NETWORK_PLATFORM_SIMULATOR;
    }
  else
    {
      status->platform = SMART_HOME_NETWORK_PLATFORM_ETHERNET;
    }

  status->backend = SMART_HOME_NETWORK_BACKEND_ETHERNET;
  status->ifname = SMART_HOME_ETH_IFNAME;
  smart_home_network_log_link_state("init-begin", status->ifname,
                                    status, SMART_HOME_NETWORK_OK);
  ret = smart_home_network_init_ethernet(status);
  smart_home_network_log_link_state("init-end", status->ifname,
                                    status, ret);
  return ret;
}

int smart_home_network_probe(FAR smart_home_network_status_t *status)
{
  int ret;

  if (status == NULL || status->ifname == NULL)
    {
      return SMART_HOME_NETWORK_ERR_NO_IP;
    }

  if (!smart_home_network_has_ip(status->ifname))
    {
      status->ip_status = SMART_HOME_NETWORK_ERR_NO_IP;
      status->dns_status = SMART_HOME_NETWORK_STATUS_UNKNOWN;
      status->online = false;
      return SMART_HOME_NETWORK_ERR_NO_IP;
    }

  status->ip_status = SMART_HOME_NETWORK_OK;

  ret = smart_home_network_verify_dns();
  status->dns_status = ret;
  status->online = ret == SMART_HOME_NETWORK_OK;
  return status->online ? SMART_HOME_NETWORK_OK : ret;
}

const char *smart_home_network_platform_name(
  smart_home_network_platform_t platform)
{
  switch (platform)
    {
    case SMART_HOME_NETWORK_PLATFORM_SIMULATOR:
      return "Simulator";
    case SMART_HOME_NETWORK_PLATFORM_DEVICE_WIFI:
      return "ESP32-S3 Wi-Fi";
    case SMART_HOME_NETWORK_PLATFORM_ETHERNET:
      return "Ethernet";
    default:
      return "Unknown";
    }
}
