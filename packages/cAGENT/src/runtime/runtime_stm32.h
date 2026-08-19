/* SPDX-License-Identifier: Apache-2.0 */
/**
 * STM32 runtime adapter header.
 *
 * When the build enables CAGENT_RUNTIME_STM32, application code may call
 * agent_runtime_stm32_fill() to obtain a pre-populated agent_runtime_t.
 * All STM32 specific includes are isolated in runtime_stm32.c.
 *
 * The portable core works without this file (POSIX fallback via fill_defaults).
 *
 * Two profiles are supported:
 *   - FreeRTOS + LWIP: multi-threaded with optional mbedTLS HTTPS
 *   - Bare-metal: single-threaded, no network (http_post returns NOTSUP)
 */

#pragma once

#include <cagent/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill agent_runtime_t with STM32 platform callbacks.
 *
 * Callback selection depends on build-time defines:
 *   CAGENT_RUNTIME_STM32_FREERTOS — use FreeRTOS primitives
 *   CAGENT_RUNTIME_STM32_TLS     — use mbedTLS HTTPS (requires LWIP)
 *
 * Without these defines, bare-metal single-threaded fallback is used.
 * Already-set callbacks are NOT overwritten.
 *
 * Implemented callbacks (FreeRTOS profile):
 *   - malloc_fn / free_fn:          standard C (pvPortMalloc optional)
 *   - now_ms:                       xTaskGetTickCount * portTICK_PERIOD_MS
 *   - sleep_ms:                     HAL_Delay
 *   - log:                          none / UART printf (bare-metal)
 *   - http_post:                    mbedTLS HTTPS (needs CAGENT_RUNTIME_STM32_TLS)
 *   - mutex_*:                      FreeRTOS SemaphoreHandle_t
 *   - enter/exit_critical:          taskENTER_CRITICAL / taskEXIT_CRITICAL
 *
 * Implemented callbacks (bare-metal profile):
 *   - malloc_fn / free_fn:          standard C
 *   - now_ms:                       HAL_GetTick / DWT cycle counter
 *   - sleep_ms:                     HAL_Delay
 *   - log:                          no-op (no stdio available)
 *   - http_post:                    NOTSUP
 *   - mutex_*:                      no-op (single threaded)
 *   - enter/exit_critical:          __disable_irq / __enable_irq (ARM CMSIS)
 */
void agent_runtime_stm32_fill(agent_runtime_t *runtime);

#ifdef __cplusplus
}
#endif
