# 文档索引

本目录的当前主题是 **ESP32-P4 Function EV Board 的 openvela Route A 移植**。
当前验收目标是最小 `nsh` 在实板启动；完整 build pass、烧录和串口 `nsh>`
日志尚待实际验证。

## 目录骨架

```text
docs/
├── 硬件适配/    # P4 架构、板级适配和构建说明
├── 开发日志/    # 已发生问题、命令和结论的历史记录
├── 开发计划/    # 当前阶段、验收条件和下一步
└── archive/     # 已退出当前范围的历史材料
```

## 当前文档

| 文档 | 用途 | 阅读时机 |
| --- | --- | --- |
| [ESP32-P4 Function EV Board 适配文档](硬件适配/esp32p4-ev-board-adaptation.md) | 适配范围、目录映射、构建与排障信息 | 开始移植或定位构建错误时 |
| [Route A 移植方案](硬件适配/esp32p4-function-ev-board-route-a-porting.md) | custom chip / custom board 架构、阶段目标和风险 | 评审架构或新增 P4 外设前 |
| [P4 最小 NSH 操作与测试](硬件适配/esp32p4-nsh-operation-and-test.md) | P4 构建、烧录及最小 NSH 上板验收 | 上板测试时 |
| [P4 移植开发记录](开发日志/dev.md) | 已发生问题的历史记录 | 复现相同错误时；不代表当前构建结论 |
| [当前开发计划](开发计划/README.md) | P4 最小 bring-up 的阶段与验收条件 | 安排或切换工作项时 |
| [第三方依赖与许可证声明](../THIRD_PARTY_NOTICES.md) | P4 HAL 与工作区依赖的许可证信息 | 发布或交付前 |

## 文档边界

- 当前源码和配置以 `board/esp32p4/`、`chips/esp32p4/` 与根目录 README 为准。
- 不再维护 ESP32-S3 板级配置或上层应用集成说明；它们不属于 P4 最小 bring-up。
- 文中若出现尚未存在的配置、烧录命令或应用方案，应视为待验证建议，不能作为
  已完成能力的证明。

## 历史材料

[archive/](archive/) 保存旧 SmartHome 交付稿和 ESP32-S3-BOX-3 硬件笔记，仅供
追溯；其中的目录、构建命令、硬件结论和依赖关系均不适用于当前 P4 工作。
