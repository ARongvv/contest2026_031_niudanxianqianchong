# 文档索引

本目录的当前主题是 **ESP32-P4 Function EV Board 的 openvela Route A 移植**。
最小 `usbconsole` 配置已在实板进入 `nsh>`；MIPI-DSI Host Probe 已完成
M1 命令写链路验收。下一阶段是 DPI video 扫屏与帧缓冲验证，当前仍不包含
LVGL 或触摸功能验收。

## 目录骨架

```text
docs/
├── 硬件适配/    # P4 架构、板级适配和构建说明
├── 开发日志/    # 已发生问题、命令和结论的历史记录
│   └── 编译/    # Kconfig 与构建链路排障记录
├── 开发计划/    # 当前阶段、验收条件和下一步
└── archive/     # 已退出当前范围的历史材料
```

## 当前文档

| 文档 | 用途 | 阅读时机 |
| --- | --- | --- |
| [ESP32-P4 Function EV Board 适配文档](硬件适配/esp32p4-ev-board-adaptation.md) | 适配范围、目录映射、构建与排障信息 | 开始移植或定位构建错误时 |
| [Route A 移植方案](硬件适配/esp32p4-function-ev-board-route-a-porting.md) | custom chip / custom board 架构、阶段目标和风险 | 评审架构或新增 P4 外设前 |
| [P4 最小 NSH 操作与测试](硬件适配/esp32p4-nsh-operation-and-test.md) | P4 构建、烧录及最小 NSH 上板验收 | 上板测试时 |
| [P4 构建与 Kconfig 排障](开发日志/编译/README.md) | 本次构建链路、Kconfig 阻塞与复测顺序 | 配置生成或编译失败时 |
| [P4X DSI Host Probe 排障](开发日志/编译/2026-08-21-DSI-Host-Probe排障记录.md) | DSI Host、Probe 注册与 USB Console 专项排障 | 验证 DSI 命令链路时 |
| [P4 移植开发记录](开发日志/dev.md) | 已发生问题的历史记录 | 复现相同错误时；不代表当前构建结论 |
| [当前开发计划](开发计划/README.md) | P4 最小 bring-up 的阶段与验收条件 | 安排或切换工作项时 |
| [上游成熟适配吸收计划](开发计划/ESP32-P4上游成熟适配吸收计划.md) | 上游 P4 基线的选择性同步边界、步骤和回归矩阵 | 计划同步 Apache NuttX / OpenVela P4 改动时 |
| [第三方依赖与许可证声明](../THIRD_PARTY_NOTICES.md) | P4 HAL 与工作区依赖的许可证信息 | 发布或交付前 |

## 文档边界

- 当前源码和配置以 `board/esp32p4/`、`chips/esp32p4/` 与根目录 README 为准。
- 不再维护 ESP32-S3 板级配置或上层应用集成说明；它们不属于 P4 最小 bring-up。
- 文中若出现尚未存在的配置、烧录命令或应用方案，应视为待验证建议，不能作为
  已完成能力的证明。

## 历史材料

[archive/](archive/) 保存旧 SmartHome 交付稿和 ESP32-S3-BOX-3 硬件笔记，仅供
追溯；其中的目录、构建命令、硬件结论和依赖关系均不适用于当前 P4 工作。
