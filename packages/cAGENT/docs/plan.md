# cAGENT Implementation Plan

> 状态说明：本文档保留为早期实现计划和阶段拆解记录。当前仓库已经超过
> “目录骨架/不可编译”阶段，实际能力请以 `include/`、`src/`、`Kconfig`、
> `CMakeLists.txt` 和 [README.md](../README.md) 为准。

## 目标

本文档把 `docs/architecture.md` 拆成可执行的开发计划。目标不是一次性实现完整
Agent 框架，而是逐步把当前骨架推进到：

```text
可 include
可编译
可链接
可运行 mock ReAct demo
可在嵌入式平台裁剪
```

优先级原则：

- 先做 public API 和可编译库。
- 先跑通 mock ReAct 闭环，再接真实 OpenAI-compatible HTTP。
- 先建立 timeout、cancel、limits、buffer 上限等硬边界，再做高级能力。
- 核心库不放业务工具、通道、UI、Voice、MQTT、MCP 产品实现。

## 当前状态

当前仓库已经具备可接入的 cAGENT core 基线：

```text
include/agent.h
include/cagent/*.h
src/core/agent_core.c
src/core/agent_loop.c
src/core/context_builder.c
src/llm/model.c
src/llm/model_mock.c
src/llm/model_openai.c
src/tools/tool_registry.c
src/tools/tool_schema.c
src/tools/tool_guard.c
src/skills/skill_registry.c
src/memory/session_mgr.c
src/runtime/runtime.c
src/runtime/runtime_openvela.c
docs/architecture.md
docs/plan.md
```

已经完成的基线能力：

- public API、错误码、limits、request/response 类型已经在 `include/` 中定义。
- `agent_create()`、`agent_destroy()`、`agent_run()`、`agent_run_simple()`、`agent_cancel()` 已实现。
- tool registry、schema 构建、guard 和本地工具执行路径已实现。
- context provider、Skill registry、session manager 和 turn eviction 已实现。
- model provider 抽象、mock model、OpenAI-compatible adapter 已实现。
- openvela runtime、Kconfig profile、CMake/NuttX 构建入口已接入。

仍建议按计划继续推进的缺口：

- host examples/tests 体系仍需补齐。
- 长期 memory/snapshot 仍是占位或规划能力。
- 异步/流式接口、更多 policy/stats/debug API 仍需设计收敛。
- ESP-IDF/STM32 runtime 需要按目标板补齐生产级 HTTP/TLS/时间/锁实现。

## 阶段划分

```text
P0: 库骨架可编译
P1: mock ReAct 闭环可运行
P2: OpenAI-compatible adapter 与嵌入式边界
P3: 平台适配、测试完善和可选能力
```

## P0: 可编译库骨架

目标：

```text
开发者可以 #include <agent.h>
项目可以生成 libcagent.a
所有 public/internal 类型有最小定义
空实现也能编译和链接
```

### P0.1 Public API

文件：

```text
include/agent.h
include/cagent/types.h
include/cagent/config.h
include/cagent/runtime.h
include/cagent/model.h
include/cagent/tools.h
include/cagent/skill.h
include/cagent/context.h
include/cagent/session.h
include/cagent/memory.h
include/cagent/event.h
include/cagent/policy.h
```

任务：

- 在 `include/agent.h` include 所有 public headers。
- 在 `types.h` 定义：
  - opaque `agent_t`
  - `agent_error_t`
  - `agent_request_t`
  - `agent_response_t`
  - `agent_limits_t`
  - `agent_stats_t`
- 在 `config.h` 定义：
  - `agent_config_t`
  - 默认配置宏/函数
  - preset 声明
- 在 `runtime.h` 定义：
  - `agent_runtime_t`
  - HTTP request/response 类型
  - optional lock/critical section callback
- 在 `model.h` 定义：
  - `agent_model_t`
  - `agent_model_ops_t`
  - `agent_model_request_t`
  - `agent_model_response_t`
  - `agent_model_tool_call_t`
- 在 `tools.h` 定义：
  - `agent_tool_t`
  - `agent_tool_fn`
  - `agent_tool_call_t`
  - `agent_tool_result_t`
  - tool flags
- 在 `event.h` 定义：
  - `agent_event_type_t`
  - `agent_event_t`
  - `agent_event_cb_t`
- 在 `policy.h` 定义：
  - policy action
  - policy decision
  - policy callback

完成标准：

- 所有 public headers 可被 C 和 C++ include。
- public headers 不 include `src/` 下任何头文件。
- `gcc -fsyntax-only` 可通过一个只 include `<agent.h>` 的文件。

### P0.2 Internal State

文件：

```text
src/core/agent_internal.h
src/types_internal.h
```

任务：

- 定义 `struct agent` 内部状态：
  - config
  - runtime
  - model
  - tools state
  - skills state
  - context providers state
  - sessions state
  - event callback
  - policy callback
  - busy flag
  - cancel_requested
  - run deadline
  - stats
- 声明内部函数：
  - `agent_loop_run()`
  - `agent_event_emit()`
  - `agent_context_build()`
  - `agent_tool_schema_build()`
  - `agent_tool_execute()`
  - session append helpers

完成标准：

- 所有 `.c` 文件能 include `agent_internal.h`。
- `struct agent` 不暴露给 public API。

### P0.3 Runtime Fallback

文件：

```text
src/runtime/runtime.c
src/runtime/runtime.h
```

任务：

- 实现 runtime default fill：
  - malloc/free fallback
  - now_ms fallback
  - log fallback
  - http_post fallback returns `AGENT_ERROR_NOTSUP`
- 实现可选 lock/critical wrapper：
  - callback 不存在时安全退化
  - 单线程模式不强制要求 mutex

完成标准：

- `agent_create()` 可以在没有用户 runtime 时使用 fallback。
- 未提供 HTTP 时 model adapter 能得到 `AGENT_ERROR_NOTSUP`。

### P0.4 Core Lifecycle

文件：

```text
src/core/agent_core.c
src/core/agent_event.c
```

任务：

- 实现：
  - `agent_create()`
  - `agent_destroy()`
  - `agent_set_model()`
  - `agent_set_event_callback()`
  - `agent_set_policy_callback()`
  - `agent_set_limits()`
  - `agent_get_stats()`
  - `agent_cancel()`
  - `agent_reset()`
- 实现 `agent_event_emit()`：
  - 填充 `timestamp_ms`
  - 透传 `trace_id`
  - 支持 NULL callback

完成标准：

- 可创建/销毁 agent。
- busy/cancel/stats 初始状态正确。
- event callback 可收到简单事件。

### P0.5 Build System

文件：

```text
CMakeLists.txt
Kconfig
```

任务：

- CMake 生成静态库 `cagent`。
- CMake 暴露 cache options：
  - `CAGENT_MAX_TOOLS`
  - `CAGENT_CONTEXT_MAX`
  - `CAGENT_MODEL_MOCK`
  - `CAGENT_MODEL_OPENAI`
- Kconfig 定义：
  - `CAGENT`
  - preset
  - core limits
  - timeout defaults
  - feature switches
  - model providers
  - runtime adapters
- 代码只使用 `CAGENT_*` 宏，`CONFIG_CAGENT_*` 通过桥接进入 C 宏。

完成标准：

- standalone CMake 可配置并编译。
- openvela/NuttX 可接入 Kconfig。
- 无平台 adapter 时仍能编译 core + mock。

## P1: Mock ReAct 闭环

目标：

```text
无网络、无真实 LLM 的情况下跑通一次完整 ReAct：
user -> mock model tool_call -> tool handler -> mock model final
```

### P1.1 Tool Registry

文件：

```text
src/tools/tool_registry.c
src/tools/tool_schema.c
src/tools/tool_guard.c
src/tools/tools_internal.h
include/cagent/tools.h
```

任务：

- 实现 `agent_register_tool()`。
- 实现 `agent_unregister_tool()`。
- 实现 `agent_tool_set_enabled()`。
- 实现 tool lookup。
- 实现 `agent_tool_execute()`。
- 实现 tool result JSON 契约：
  - tool handler 输出 UTF-8 JSON
  - 空成功输出补 `{"ok":true}`
  - 失败包装结构化 JSON
- 实现 tool flags：
  - `LLM_VISIBLE`
  - `READ_ONLY`
  - `SIDE_EFFECT`
  - `REQUIRES_CONFIRM`
  - `DISABLED`
  - `PARALLEL_SAFE` reserved
- 实现顺序执行语义。
- 实现 `timeout_ms = 0` 使用全局 `per_tool_timeout_ms`。

完成标准：

- 重名注册失败。
- registry 满返回 limit error。
- disabled tool 不进入 schema，不可执行。
- tool 不存在返回结构化错误。

### P1.2 Tool Schema

文件：

```text
src/tools/tool_schema.c
```

任务：

- 遍历 LLM visible + enabled tools。
- 构建 OpenAI-compatible tools JSON。
- 支持 `input_schema_json == NULL` 或空字符串表示无参数。
- schema JSON parse 失败返回 `AGENT_ERROR_PARSE`。
- 输出 buffer 不够返回 `AGENT_ERROR_LIMIT`。
- 预留 schema dirty/cache 字段。

完成标准：

- mock demo 能把工具 schema 传给 mock model。
- 单元测试覆盖无参数、有参数、disabled、非法 schema。

### P1.3 Context And Skills

文件：

```text
src/core/context_builder.c
src/skills/skill_registry.c
src/skills/skills_internal.h
include/cagent/context.h
include/cagent/skill.h
```

任务：

- 实现 context provider registry。
- provider 按 priority 排序。
- 实现 `agent_context_build()`。
- 实现 context overflow 检测：
  - critical context 不静默截断
  - 返回 `AGENT_ERROR_CONTEXT_OVERFLOW`
- 实现 skill registry。
- 实现默认 skill context provider。
- Skill 默认是 context，不执行代码。

完成标准：

- system_prompt + provider + skill summary 可拼接。
- provider 溢出可检测。
- skill 注册后能进入 context。

### P1.4 Session Manager

文件：

```text
src/memory/session_mgr.c
src/memory/memory_internal.h
include/cagent/session.h
```

任务：

- 实现 session lookup/create。
- 实现：
  - append user
  - append assistant final
  - append assistant tool_calls
  - append tool result
- 实现 `agent_session_clear()`。
- 实现 `agent_session_clear_all()`。
- 实现 model messages build。
- 实现完整 turn 淘汰。

Turn 定义：

```text
turn starts: user
turn ends: assistant(final)
```

完成标准：

- 不会留下孤立 `tool` message。
- 不会留下孤立 `assistant(tool_calls)`。
- session 满时只淘汰完整 turn。

### P1.5 Mock Model

文件：

```text
src/llm/model_mock.c
src/llm/llm_parse.c
include/cagent/model.h
```

任务：

- 新增 `model_mock.c`。
- mock model 支持脚本式响应：
  - 第一次 complete 返回 tool_call。
  - 第二次 complete 返回 final content。
- mock model 支持统计调用次数。
- `llm_parse.c` 先支持内部测试响应格式或 OpenAI-compatible 子集。

完成标准：

- 不依赖网络。
- 可用于单元测试和 example。

### P1.6 ReAct Loop

文件：

```text
src/core/agent_loop.c
src/core/agent_core.c
```

任务：

- 实现 `agent_run()` 调用 `agent_loop_run()`。
- loop 检查：
  - max_steps
  - timeout_ms
  - cancel_requested
- 每轮：
  - append user
  - build context
  - build tool schema
  - model complete
  - final reply 或 tool calls
  - tool calls 顺序执行
  - append tool results
- 事件：
  - REQUEST_START
  - LLM_START/END
  - TOOL_START/END
  - FINAL
  - ERROR
  - REQUEST_DONE

完成标准：

- mock ReAct demo 完整跑通。
- stats 正确记录 iterations/model_calls/tool_calls/elapsed。
- cancel flag 能在 iteration 边界终止。

### P1.7 Example

新增目录：

```text
examples/mock_react/
├── README.md
└── main.c
```

任务：

- 创建 agent。
- 注册 mock model。
- 注册一个 `demo_echo` tool。
- 执行 `agent_run()`。
- 输出 final reply。

完成标准：

```text
mock model call 1 -> tool_call demo_echo
tool demo_echo -> {"ok":true,"value":"demo_tool_ok"}
mock model call 2 -> final reply
program exits 0
```

## P2: OpenAI-Compatible Adapter And Embedded Boundaries

目标：

```text
支持真实 OpenAI-compatible 非流式调用，同时保证 timeout、buffer、context、
tool output 和 session 都有边界。
```

### P2.1 Rename Provider Files

任务：

- 将 `src/llm/llm_openai.c` 改名为 `src/llm/model_openai.c`。
- 新增 `src/llm/model.c` 作为 provider facade。
- 保留 `src/llm/llm_parse.c` 作为解析工具。
- 文档和 CMake 使用新文件名。

完成标准：

- provider 文件命名统一：

```text
model_openai.c
model_mock.c
model.c
llm_parse.c
llm_router.c
```

### P2.2 OpenAI-Compatible Request Builder

文件：

```text
src/llm/model_openai.c
src/llm/llm_parse.c
```

任务：

- 构建 request JSON。
- session messages 转 OpenAI messages。
- context 作为 system message。
- tools schema 作为 tools。
- 设置 `max_output_tokens`。
- 设置 provider timeout。
- 使用 `runtime.http_post()`。

完成标准：

- 能调用 OpenAI-compatible endpoint。
- HTTP 未配置时返回 `AGENT_ERROR_NOTSUP`。
- response 解析 final content 和 tool_calls。

### P2.3 Buffer And Arena

文件：

```text
src/runtime/runtime.c
src/core/agent_internal.h
src/core/agent_loop.c
```

任务：

- 实现 request arena。
- context/tools/model/tool output 使用 bounded buffer。
- stats 记录 peak usage。
- 实现 `agent_estimate_request_size()` 或内部估算。

完成标准：

- 大请求不会静默溢出。
- buffer 不够返回明确错误。
- stats 可观测。

### P2.4 Timeout And Cancel

任务：

- `agent_limits_t` 生效：
  - `timeout_ms`
  - `per_model_timeout_ms`
  - `per_tool_timeout_ms`
  - `max_output_tokens`
- `agent_cancel()` 设置 cancel flag。
- model provider 支持 cancel hook。
- runtime HTTP timeout 使用 provider timeout。

完成标准：

- 整体 timeout 返回 `AGENT_ERROR_TIMEOUT`。
- cancel 返回 `AGENT_ERROR_CANCELLED`。
- 单工具 timeout 可单独覆盖。

### P2.5 Context Degradation

任务：

- provider 支持 priority。
- provider 支持 importance 或 required 标记。
- optional provider 在 buffer 不够时跳过。
- critical provider 放不下返回 `AGENT_ERROR_CONTEXT_OVERFLOW`。

完成标准：

- context overflow 可预测。
- 不静默截断 safety/system context。

## P3: Tests, Platforms, Optional Integrations

### P3.1 Tests

目录：

```text
tests/
├── unit/
│   ├── test_tool_registry.c
│   ├── test_tool_schema.c
│   ├── test_tool_guard.c
│   ├── test_context_builder.c
│   ├── test_skill_registry.c
│   ├── test_session_mgr.c
│   └── test_llm_parse.c
├── integration/
│   └── test_react_mock.c
└── mock/
    ├── mock_model.c
    ├── mock_runtime.c
    └── mock_tool.c
```

测试框架：

```text
首选 Unity
可选 cmocka
```

完成标准：

- host CI 可运行单元测试。
- mock ReAct 集成测试可运行。
- buffer overflow、context overflow、session eviction、cancel、timeout 都有测试。

### P3.2 Platform Adapters

目录：

```text
platforms/openvela/
platforms/espidf/
platforms/stm32/
platforms/posix/
```

任务：

- openvela:
  - syslog
  - clock
  - BSD socket + mbedTLS HTTP
  - heap/largest-block 检测
- ESP-IDF:
  - heap_caps
  - esp_timer
  - esp_http_client
- STM32:
  - static arena
  - FreeRTOS tick
  - UART log
  - lwIP/AT transport

完成标准：

- core 不直接 include 平台头。
- 平台只实现 runtime/port glue。

### P3.3 Optional Integrations

目录：

```text
integrations/http/
integrations/storage/
integrations/trace/
integrations/mcp/
```

任务：

- HTTP backend 可替换。
- storage backend 可替换。
- trace backend 输出事件。
- MCP bridge 通过 `agent_register_tool()` 注入外部工具。

边界：

```text
MCP 不进入 core 默认路径。
业务工具不进入 core。
通道不进入 core。
```

## API 完成清单

P0/P1 完成后，至少应具备：

```c
agent_t *agent_create(const agent_config_t *config);
void agent_destroy(agent_t *agent);

int agent_run(agent_t *agent,
              const agent_request_t *request,
              agent_response_t *response);

int agent_cancel(agent_t *agent);
int agent_reset(agent_t *agent);

int agent_set_model(agent_t *agent, agent_model_t *model);
int agent_set_event_callback(agent_t *agent,
                             agent_event_cb_t cb,
                             void *user_data);
int agent_set_policy_callback(agent_t *agent,
                              agent_policy_cb_t cb,
                              void *user_data);
int agent_set_limits(agent_t *agent, const agent_limits_t *limits);
int agent_get_stats(agent_t *agent, agent_stats_t *stats);

int agent_register_tool(agent_t *agent, const agent_tool_t *tool);
int agent_unregister_tool(agent_t *agent, const char *name);
int agent_tool_set_enabled(agent_t *agent, const char *name, bool enabled);

int agent_register_skill(agent_t *agent, const agent_skill_def_t *skill);
int agent_unregister_skill(agent_t *agent, const char *name);
int agent_skill_set_enabled(agent_t *agent, const char *name, bool enabled);

int agent_register_context_provider(agent_t *agent,
                                    const agent_context_provider_t *provider);
int agent_unregister_context_provider(agent_t *agent, const char *name);

int agent_session_clear(agent_t *agent, const char *session_id);
int agent_session_clear_all(agent_t *agent);
```

## Definition Of Done

### P0 Done

- `#include <agent.h>` works.
- `cagent` static library builds.
- `agent_create()` / `agent_destroy()` link.
- CMake and Kconfig exist.

### P1 Done

- `examples/mock_react` runs without network.
- ReAct loop performs one tool call and returns final reply.
- Tool registry/schema/guard works.
- Context provider and skill context work.
- Session stores user/assistant/tool messages in correct order.
- Timeout/cancel paths are at least checked at loop boundaries.

### P2 Done

- OpenAI-compatible non-streaming adapter works through runtime HTTP.
- Model response parser handles final content and tool_calls.
- Context overflow, tool output limit, request size limit, and session eviction
  behave predictably.

### P3 Done

- Unit tests and mock integration tests run on host.
- openvela runtime adapter builds behind Kconfig.
- Optional integrations remain outside core.

## Development Rules

- Do not add business tools to `src/tools`.
- Do not add channels, UI, voice, MQTT, or WebSocket to core.
- Do not make core depend on openvela, ESP-IDF, STM32, or POSIX headers.
- Do not silently truncate context, tool output, or model response.
- Prefer structured JSON errors over hard loop failure for tool-level errors.
- Keep public API stable and internal structs private.
