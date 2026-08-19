# cAGENT 文档索引

本目录保存 cAGENT 的架构、API、设计方案和历史计划文档。当前实现状态以
`packages/cAGENT/include/`、`packages/cAGENT/src/`、`Kconfig`、`CMakeLists.txt`
和上级 [README.md](../README.md) 为准；较早期的计划文档用于理解演进过程，不应作为当前构建状态的唯一依据。

## 推荐阅读顺序

| 文档 | 定位 | 适合什么时候看 |
|------|------|----------------|
| [architecture.md](architecture.md) | cAGENT 当前架构和模块边界 | 想理解 core、llm、tools、skills、memory、runtime 如何协作 |
| [api_reference.md](api_reference.md) | 公共 API 参考 | 接入 `agent_run`、模型 provider、工具、Skill、context、session API |
| [context-budget-plan.md](context-budget-plan.md) | 上下文和 buffer 预算方案 | 处理 prompt/messages/tool schema 溢出、session 淘汰和内存预算 |
| [skill-summary-design.md](skill-summary-design.md) | Skill 渐进披露设计 | 设计 summary-only Skill、`read_skill` 工具和低 token 占用策略 |
| [cAGENT与smart_home综合技术分析.md](cAGENT与smart_home综合技术分析.md) | cAGENT 与 smart_home 综合分析 | 从真实 demo 学习 cAGENT 的完整使用方式 |
| [handwrite-blueprint.md](handwrite-blueprint.md) | 手写实现训练计划 | 想按 7 天路线复刻一个最小 cAGENT 时参考 |
| [plan.md](plan.md) | 早期实现计划 | 查看从骨架到可运行库的阶段拆解，当前状态请以源码为准 |
| [fix.md](fix.md) | 改进建议清单 | 梳理公共 API、session、配置、安全和调试能力的后续优化点 |
| [capability-gap.md](capability-gap.md) | 能力盘点与缺口分析 | 核实内核已具备/缺失的能力，任务 B（多设备）/C（MCP）开发依据 |

## 当前实现概览

cAGENT 当前已经不只是目录骨架，核心能力包括：

- `include/agent.h` 聚合公共 API。
- `agent_create` / `agent_destroy` / `agent_run` / `agent_run_simple` 同步运行入口。
- `agent_cancel`、`agent_reset`、`agent_set_limits`、`agent_get_stats` 等运行控制 API。
- 模型 provider 抽象和 OpenAI-compatible provider。
- tool registry、schema 构建、guard 和 tool result 回写。
- Skill registry、context provider 和系统上下文构建。
- session 管理、turn 淘汰和模型 messages 构建。
- runtime 抽象以及 openvela/ESP-IDF/STM32 适配占位或实现。
- Kconfig profiles、buffer/capacity 预算和 CMake/NuttX 构建入口。

## 维护原则

- 当前行为以源码和公共头文件为准。
- `architecture.md` 描述当前架构边界。
- `plan.md` 和 `handwrite-blueprint.md` 保留演进和教学价值，其中早期“骨架阶段”描述不代表当前仓库状态。
- API key、业务工具、UI 和产品策略不应进入 cAGENT core，应由上层应用注入。
