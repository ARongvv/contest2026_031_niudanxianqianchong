/* SPDX-License-Identifier: Apache-2.0 */
/*
 * qrcodegen 薄封装：文本 → 单色 QR 矩阵。
 * 隔离第三方 API，绑定页只依赖本接口。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 支持的最大版本（version 20 = 97×97 模块）。绑定 URL 约 25 字符，
 * ECC-M 下 version 2~3 足够；余量留给更长内容。 */
#define SMART_HOME_MILOCO_QR_MAX_MODULES 97

/*
 * 将 text 编码为 QR 矩阵。
 * matrix 需提供 SMART_HOME_MILOCO_QR_MAX_MODULES² 字节；1 = 黑点。
 * 成功返回 0，*modules 输出实际边长；失败返回 -1。
 */
int smart_home_miloco_qr_encode(const char *text,
                                uint8_t *matrix,
                                int *modules);

#ifdef __cplusplus
}
#endif
