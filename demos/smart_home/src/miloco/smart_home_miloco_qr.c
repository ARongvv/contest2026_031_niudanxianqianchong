/* SPDX-License-Identifier: Apache-2.0 */

#include "smart_home_miloco_qr.h"

#include "../../third_party/qrcodegen/qrcodegen.h"

#include <string.h>

/* qrcodegen 需要两块临时缓冲（码字区 + 输出区）。绑定页只在线程安全的
 * LVGL 上下文调用，静态分配避免占用调用者栈。 */
static uint8_t g_qr_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];
static uint8_t g_qr_code[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];

int smart_home_miloco_qr_encode(const char *text,
                                uint8_t *matrix,
                                int *modules)
{
    int size;
    int x;
    int y;

    if (!text || !text[0] || !matrix || !modules) {
        return -1;
    }
    if (!qrcodegen_encodeText(text, g_qr_temp, g_qr_code,
                              qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true)) {
        return -1;
    }
    size = qrcodegen_getSize(g_qr_code);
    if (size <= 0 || size > SMART_HOME_MILOCO_QR_MAX_MODULES) {
        return -1;
    }
    memset(matrix, 0,
           (size_t)SMART_HOME_MILOCO_QR_MAX_MODULES *
               SMART_HOME_MILOCO_QR_MAX_MODULES);
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            matrix[y * SMART_HOME_MILOCO_QR_MAX_MODULES + x] =
                qrcodegen_getModule(g_qr_code, x, y) ? 1 : 0;
        }
    }
    *modules = size;
    return 0;
}
