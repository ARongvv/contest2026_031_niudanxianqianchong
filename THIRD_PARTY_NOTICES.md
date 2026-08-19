# 第三方依赖与许可证声明

最后核对：2026-08-19。

本文件记录本仓 ESP32-P4 Function EV Board 移植的已识别依赖。它不替代各
依赖随源代码分发的 `LICENSE`、`NOTICE` 或版权声明；再分发固件或源代码时，
应一并保留适用的上游声明。

## P4 最小 NSH 构建

| 组件 | 用途与版本 | 许可证 | 来源/声明 |
| --- | --- | --- | --- |
| Apache NuttX / openvela | 本仓 P4 芯片、共享层和板级代码的上游基础 | Apache-2.0 | 工作区 `nuttx/LICENSE`、`nuttx/NOTICE` 与 `apps/LICENSE`、`apps/NOTICE` |
| Espressif `esp-hal-3rdparty` | ESP32-P4 HAL、SDK 头文件及库 | Apache-2.0 | `https://github.com/espressif/esp-hal-3rdparty`，固定提交 `b90b1837cb5ad24747deb4c895246037cc206ce5`；许可证随该依赖提供 |

本仓 `chips/esp32p4/` 与 `board/esp32p4/` 中来自 Apache NuttX 的文件保留了
原有 SPDX、版权和 NOTICE 声明。`esp-hal-3rdparty` 是构建时拉取的依赖，不作为
本仓受跟踪源代码提交；其兼容补丁位于
`chips/esp32p4/common/espressif/patches/`，补丁本身随本仓以 Apache-2.0 发布。

## 可选 audio_event 应用

以下依赖只在 manifest 启用 `app/audio_event`，且相应 Kconfig 选项被选择时进入
构建；它们不属于 P4 最小 `nsh` bring-up。

| 组件 | 用途与版本 | 许可证 | 来源/声明 |
| --- | --- | --- | --- |
| TensorFlow Lite for Microcontrollers | 推理运行时；提交 `cfa4c91d1b36c37c7c104b9c664615e59f1abfe3` | Apache-2.0 | `apps/mlearning/tflite-micro/tflite-micro/LICENSE` |
| Kiss FFT | 音频特征的 FFT；v130 | BSD-3-Clause | `apps/math/kissfft/kissfft/COPYING`，Copyright (c) 2003-2010 Mark Borgerding |
| gemmlowp | TFLM 量化矩阵运算；提交 `719139ce755a0f31cbf1c37f7f98adcc7fc9f425` | Apache-2.0 | `apps/math/gemmlowp/gemmlowp/LICENSE` |
| ruy | TFLM 矩阵乘法；提交 `d37128311b445e758136b8602d1bbd2a755e115d` | Apache-2.0 | `apps/math/ruy/ruy/LICENSE` |
| LVGL | 仅在 `CONFIG_EXAMPLES_AUDIO_EVENT_UI=y` 时使用；v9.2.1 | MIT | 由 `apps/graphics/lvgl/Makefile` 获取；须随发布物保留其上游许可证 |

`app/audio_event/model/model_int8.tflite`、训练数据和生成的字体文件的权利来源
不由上述软件许可证自动覆盖。发布含有这些材料的版本前，必须补充数据集、模型、
字体及图标的来源、版权人和许可证明；无法确认时不得将其声明为 Apache-2.0。

## 本仓许可证范围

除保留自身许可证或版权声明的第三方材料外，本仓新增的原创代码、构建配置、
兼容补丁和文档以根目录 [LICENSE](LICENSE) 的 Apache License 2.0 发布。上游
衍生文件继续受其文件头及上游 NOTICE 的约束。
