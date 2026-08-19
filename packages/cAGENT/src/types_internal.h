/* SPDX-License-Identifier: Apache-2.0 */
/**
 * cAGENT 内部共享类型。
 *
 * 仅被 src/ 使用，不暴露给应用。子模块特定内部类型放在各自的
 * *_internal.h 中（如 tools_internal.h、memory_internal.h）。
 * 此文件只放多模块真正共享的定义。
 */

#pragma once

#include <agent.h>

/** 抑制未使用参数警告 */
#define CAGENT_UNUSED(x) ((void)(x))
