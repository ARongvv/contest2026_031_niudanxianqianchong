/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* openvela installs cJSON below apps/include/netutils; host tests use upstream. */
#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h"
#endif
