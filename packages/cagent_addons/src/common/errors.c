/* SPDX-License-Identifier: Apache-2.0 */
#include "cagent_addons/errors.h"

const char *caddons_error_code(int error)
{
    switch (error) {
    case CADDONS_OK: return "ok";
    case CADDONS_MORE: return "more";
    case CADDONS_ERR_INVALID: return "invalid";
    case CADDONS_ERR_PARSE: return "parse";
    case CADDONS_ERR_LIMIT: return "limit";
    case CADDONS_ERR_VERSION: return "version";
    case CADDONS_ERR_PROTOCOL: return "protocol";
    case CADDONS_ERR_NETWORK: return "network";
    case CADDONS_ERR_TIMEOUT: return "timeout";
    case CADDONS_ERR_OFFLINE: return "offline";
    case CADDONS_ERR_DENIED: return "denied";
    case CADDONS_ERR_BUSY: return "busy";
    case CADDONS_ERR_NOT_FOUND: return "not_found";
    case CADDONS_ERR_NOMEM: return "nomem";
    case CADDONS_ERR_UNSUPPORTED: return "unsupported";
    default: return "internal";
    }
}
