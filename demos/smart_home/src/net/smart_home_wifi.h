/****************************************************************************
 * smart_home/src/net/smart_home_wifi.h
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

#ifndef SMART_HOME_WIFI_H
#define SMART_HOME_WIFI_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SMART_HOME_WIFI_OK           0
#define SMART_HOME_WIFI_ERR_CONFIG  -1
#define SMART_HOME_WIFI_ERR_ASSOC   -2
#define SMART_HOME_WIFI_ERR_DHCP    -3
#define SMART_HOME_WIFI_ERR_DNS     -4

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: smart_home_wifi_connect
 *
 * Description:
 *   Connect to WiFi using WAPI config file or Kconfig defaults.
 *   Blocking call that performs: load config -> associate -> DHCP -> DNS check.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   SMART_HOME_WIFI_OK on success, negative error code on failure.
 *
 ****************************************************************************/

int smart_home_wifi_connect(void);

/****************************************************************************
 * Name: smart_home_wifi_is_connected
 *
 * Description:
 *   Check if WiFi interface is up and has an IP address.
 *
 * Returned Value:
 *   true if connected, false otherwise.
 *
 ****************************************************************************/

bool smart_home_wifi_is_connected(void);

#endif /* SMART_HOME_WIFI_H */
