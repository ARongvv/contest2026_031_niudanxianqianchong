/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Stable addon errors. Glue maps these to cAGENT errors in one place. */
typedef enum {
    CADDONS_OK = 0,
    CADDONS_MORE = 1,
    CADDONS_ERR_INVALID = -1,
    CADDONS_ERR_PARSE = -2,
    CADDONS_ERR_LIMIT = -3,
    CADDONS_ERR_VERSION = -4,
    CADDONS_ERR_PROTOCOL = -5,
    CADDONS_ERR_NETWORK = -6,
    CADDONS_ERR_TIMEOUT = -7,
    CADDONS_ERR_OFFLINE = -8,
    CADDONS_ERR_DENIED = -9,
    CADDONS_ERR_BUSY = -10,
    CADDONS_ERR_NOT_FOUND = -11,
    CADDONS_ERR_NOMEM = -12,
    CADDONS_ERR_INTERNAL = -13,
    CADDONS_ERR_UNSUPPORTED = -14,
} caddons_error_t;

const char *caddons_error_code(int error);

#ifdef __cplusplus
}
#endif
