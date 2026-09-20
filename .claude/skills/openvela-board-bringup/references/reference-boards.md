# 参考板索引(按外设域)

接入某类外设前,先读同域参考板的实现再动手。路径相对 openvela 工作区根(本仓的上一级)。用法:先 `ls` 看板目录结构 → `grep` 找外设注册点(见 tree-layouts.md)→ 对比 `configs/` 找最小可用 defconfig 组合。

## 目录

- [外设域索引表](#外设域索引表)
- [厂商树总览](#厂商树总览)
- [使用规则](#使用规则)

## 外设域索引表

| 外设域 | 首选参考 | 路径 | 说明 |
|---|---|---|---|
| 摄像头(CSI) | esp32s3-eye | `vendor/espressif/boards/esp32s3/esp32s3-eye` | 带摄像头+LCD 的完整板,SC2336/视频链路可对标 |
| 显示/触摸 | esp32s3-box | `vendor/espressif/boards/esp32s3/esp32s3-box` | 显示+输入完整组合 |
| 音频(codec) | esp32-lyrat | `nuttx/boards/xtensa/esp32/esp32-lyrat` | 经典音频板,codec+I2S 链路 |
| 音频(备选) | esp32-audio-kit | `nuttx/boards/xtensa/esp32/esp32-audio-kit` | 另一套 codec 方案 |
| 以太网 | esp32-ethernet-kit | `nuttx/boards/xtensa/esp32/esp32-ethernet-kit` | MAC/PHY 直连参考 |
| 视频 SoC(横向) | artinchip d12x | `vendor/artinchip/boards/d12x/demo68-nor` | camera+display 富 SoC,含 pack 资源打包 |
| ESP32-P4 芯片层 | espressif p4 | `vendor/espressif/chips/`、`vendor/espressif/boards/esp32p4` | 与本仓 Route A 的 chips/esp32p4 对照,排查"副本 vs 上游"差异 |
| configs 矩阵范例 | esp32-devkitc | `nuttx/boards/xtensa/esp32/esp32-devkitc` | 54 个 configs,学演示配置如何组织 |

## 厂商树总览

`vendor/` 下还有:allwinnertech、beken、bes、flagchip、gigadevice、gravityxr、infineon、rockchip、st 等,各有 `chips/`+`boards/` 结构。选参考优先级:**同芯片 > 同外设域 > 同厂商 > 其他**。

## 使用规则

- 参考实现用来定"注册点 + Kconfig + 最小 defconfig 组合",不是抄全部代码;驱动代码写法走 `nuttx-driver-development` 技能。
- 对比时先看 `configs/` 差集:目标 demo 缺哪个 CONFIG,逐项打开验证,不要整块搬。
- 参考板路径失效时(工作区裁剪),退回 `find vendor nuttx/boards -name "*<外设>*"` 全局搜。
