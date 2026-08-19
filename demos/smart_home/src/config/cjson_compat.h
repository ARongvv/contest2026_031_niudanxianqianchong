/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cJSON 平台兼容头（对齐 packages/cagent_addons/include/cagent_addons/cjson_compat.h）。
 *
 * openvela 安装 cJSON 到 apps/include/netutils（固件构建走 <netutils/cJSON.h>）；
 * host 测试直接 include 上游源码 cJSON.h。按 __NuttX__ 转发，两边各自可用。
 */

#pragma once

#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h"
#endif
