/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log a diagnostic pointer without relying on board-private memory headers. */
void ov_mem_region_log(const char *label, const void *pointer);

/* Allocate a bounded non-DMA payload buffer. On ESP32-S3 with a configured
 * bulk PSRAM pool, this never falls back to internal SRAM.
 */
void *ov_mem_bulk_alloc(size_t size);
void ov_mem_bulk_free(void *pointer);
void ov_mem_bulk_diag(const char *point);

#ifdef __cplusplus
}
#endif
