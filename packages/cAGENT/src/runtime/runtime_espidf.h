/* SPDX-License-Identifier: Apache-2.0 */
/**
 * ESP-IDF runtime adapter header.
 *
 * When the build enables CAGENT_RUNTIME_ESPIDF, application code may call
 * agent_runtime_espidf_fill() to obtain a pre-populated agent_runtime_t.
 * All ESP-IDF specific includes are isolated in runtime_espidf.c.
 *
 * The portable core works without this file (POSIX fallback via fill_defaults).
 */

#pragma once

#include <cagent/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill agent_runtime_t with ESP-IDF / FreeRTOS platform callbacks.
 *
 * Already-set callbacks are NOT overwritten (application may set partial
 * callbacks before calling this function).  Unused fields stay NULL and
 * are later filled by agent_runtime_fill_defaults() with POSIX fallback.
 *
 * Implemented callbacks:
 *   - malloc_fn / free_fn:          standard C (heap_caps_malloc optional)
 *   - now_ms:                       esp_timer_get_time() / 1000 (monotonic)
 *   - sleep_ms:                     vTaskDelay (FreeRTOS tick conversion)
 *   - log:                          ESP_LOGI / ESP_LOGW / ESP_LOGE
 *   - http_post:                    mbedTLS HTTPS (needs CAGENT_RUNTIME_ESPIDF_TLS)
 *   - mutex_create/destroy/lock/unlock: FreeRTOS SemaphoreHandle_t
 *   - enter/exit_critical:          portENTER_CRITICAL / portEXIT_CRITICAL
 */
void agent_runtime_espidf_fill(agent_runtime_t *runtime);

#ifdef __cplusplus
}
#endif
