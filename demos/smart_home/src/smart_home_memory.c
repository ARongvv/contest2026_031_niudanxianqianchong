/* SPDX-License-Identifier: Apache-2.0 */

#include "smart_home_memory.h"

#include <cagent/runtime_openvela.h>

void *smart_home_bulk_alloc(size_t size)
{
    return ov_mem_bulk_alloc(size);
}

void smart_home_bulk_free(void *pointer)
{
    ov_mem_bulk_free(pointer);
}

void smart_home_bulk_diag(const char *point)
{
    ov_mem_bulk_diag(point);
}
