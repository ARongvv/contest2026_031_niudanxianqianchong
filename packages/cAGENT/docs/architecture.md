# cAGENT Architecture

## 定位

`cAGENT` 是一个面向嵌入式和跨设备平台的通用 C Agent 核心库。目标运行环境
包括 openvela/NuttX、ESP-IDF、STM32/FreeRTOS、bare-metal 设备和 POSIX
主机测试环境。

这个项目的核心定位是 **Agent 推理内核**，不是完整应用框架。它应该提供
ReAct 推理循环、工具注册、Skill 注册、上下文构建、会话管理、模型适配和
runtime 抽象；通道接入、UI、语音、MQTT、WebSocket、MCP 服务器、设备业务
工具和产品策略应该由上层应用导入。

一次典型推理闭环如下：

```text
user input
  -> session/context
  -> model provider
  -> tool calls
  -> tool execution
  -> tool result messages
  -> final reply
```

## 当前状态

当前仓库已经从早期“架构骨架阶段”推进到可接入的核心库阶段。源码中已经包含
公共 API、同步 ReAct loop、模型 provider、工具/Skill/context/session 管理、
OpenAI-compatible provider、openvela runtime 适配、Kconfig profiles 和
CMake/NuttX 构建入口。

仍在演进中的方向包括：更完整的长期 memory/snapshot、异步/流式调用、更细粒度
policy API、更多平台 runtime 的生产级实现，以及围绕调试、统计和安全管控的
增强能力。历史计划文档中的“空文件/不可编译”描述仅反映早期阶段，当前行为应
以 `include/`、`src/`、`Kconfig` 和 `CMakeLists.txt` 为准。

当前文件结构：

```text
cAGENT/
├── include/
│   ├── agent.h
│   └── cagent/
│       ├── config.h
│       ├── context.h
│       ├── event.h
│       ├── memory.h
│       ├── model.h
│       ├── model_openai.h
│       ├── model_openai_compat.h
│       ├── policy.h
│       ├── runtime.h
│       ├── session.h
│       ├── skill.h
│       ├── tools.h
│       └── types.h
├── src/
│   ├── core/
│   │   ├── agent_internal.h
│   │   ├── agent_core.c
│   │   ├── agent_event.c
│   │   ├── agent_loop.c
│   │   ├── config_load.c
│   │   └── context_builder.c
│   ├── llm/
│   │   ├── llm_internal.h
│   │   ├── model.c
│   │   ├── model_mock.c
│   │   ├── model_openai.c
│   │   ├── llm_parse.c
│   │   └── llm_router.c
│   ├── memory/
│   │   ├── memory_internal.h
│   │   ├── memory_store.c
│   │   └── session_mgr.c
│   ├── runtime/
│   │   ├── runtime.c
│   │   ├── runtime.h
│   │   ├── runtime_espidf.c
│   │   ├── runtime_espidf.h
│   │   ├── runtime_openvela.c
│   │   ├── runtime_openvela.h
│   │   ├── runtime_stm32.c
│   │   └── runtime_stm32.h
│   ├── skills/
│   │   ├── skill_registry.c
│   │   └── skills_internal.h
│   ├── tools/
│   │   ├── tool_guard.c
│   │   ├── tool_registry.c
│   │   ├── tool_schema.c
│   │   └── tools_internal.h
│   └── types_internal.h
├── docs/
│   ├── README.md
│   ├── api_reference.md
│   ├── architecture.md
│   └── plan.md
├── CMakeLists.txt
├── Kconfig
├── README.md
└── LICENSE
```

已经落地的核心模块：

- `include/agent.h` 作为聚合入口，应用代码不需要 include `src/` 内部头文件。
- `include/cagent/*.h` 暴露 agent、model、tool、skill、context、session、runtime、event 等公共 API。
- `src/core` 实现 Agent 生命周期、同步 `agent_run()` 外壳、事件回调、配置加载和 ReAct loop。
- `src/tools` 实现工具注册表、OpenAI-compatible tools schema 构建和 tool guard。
- `src/skills` 实现 Skill 注册、排序和上下文注入。
- `src/memory` 实现 session 管理、turn 淘汰、tool_calls/tool result 消息构建和基础 memory 占位。
- `src/llm` 实现模型抽象、mock model、OpenAI-compatible adapter、响应解析和 provider router。
- `src/runtime` 实现平台抽象，当前以 openvela runtime 为主要在线路径，并保留 ESP-IDF/STM32 适配边界。
- `Kconfig` 提供 profile、capacity、buffer、runtime 和 model provider 裁剪选项。
- `CMakeLists.txt` 提供 NuttX/openvela 集成构建入口。

仍需继续完善的能力：

- 长期 memory/snapshot 的生产级存储实现。
- 更丰富的 policy/stats/debug 公共 API。
- 异步或流式 model 调用。
- ESP-IDF/STM32 runtime 的完整联网/TLS 实现。
- 更系统的 host 单元测试和协议兼容测试。

## 设计原则

- 核心是库，不是 daemon，不默认创建后台线程。
- 核心不拥有 message bus，不绑定 CLI、Voice、MQTT、WebSocket 或 UI。
- Tool 和 Skill 由上层开发者导入，核心只提供注册、调度和安全边界。
- Tool 是默认唯一可执行扩展点。
- Skill 默认是上下文资源，不直接执行代码。
- Model provider 可替换，核心不绑定某个云厂商。
- Runtime/port 是唯一平台边界，core 不直接 include openvela、ESP-IDF 或 STM32 HAL。
- 所有嵌入式资源都必须有上限，包括 tools、skills、sessions、messages、context、tool input 和 tool output。
- `agent_run()` 必须同时受 step 数、整体 timeout、单次 model timeout、单次 tool timeout 和 cancel flag 控制。
- 先实现同步闭环，再实现可接管的 step loop。
- Event、Policy、Guard 是真实设备可控性的基础能力。

## 范围边界

核心库应该包含：

- Agent 生命周期。
- 同步推理入口。
- Bounded ReAct loop。
- Tool registry。
- Tool schema builder。
- Tool guard。
- Skill registry。
- Context provider。
- Session memory。
- Model provider 抽象。
- OpenAI-compatible non-streaming adapter。
- Mock model。
- Runtime abstraction。
- Event callback。
- Policy callback。
- Timeout/cancel。
- Limits 和 stats。

核心库不应该包含：

- 飞书、微信、MQTT、WebSocket、Voice、LVGL 等接入通道。
- 具体设备业务工具，例如 `wifi_scan`、`ota_update`、`read_sensor`。
- Skill Markdown loader、文件扫描、云端同步、热更新。
- MCP client/server 的完整产品实现。
- 多设备组网协议。
- 后台 worker 服务。
- 应用级 message bus。
- LLM 配置 UI 或命令行。

这些能力可以在上层应用、`examples/`、`platforms/` 或 `integrations/` 中实现。

## 分层

```text
Application / Product / Demo
    |
    | register tools, skills, providers, policy
    v
Public API: include/agent.h, include/cagent/*.h
    |
    +-- Core
    |     - lifecycle
    |     - ReAct loop
    |     - step loop, optional
    |     - event dispatch
    |     - limits / stats
    |
    +-- LLM
    |     - model provider interface
    |     - OpenAI-compatible adapter
    |     - response/tool-call parser
    |     - optional router adapter
    |
    +-- Tools
    |     - tool registry
    |     - schema builder
    |     - guarded execution
    |
    +-- Skills
    |     - skill registry
    |     - skill context builder
    |
    +-- Memory
    |     - session history
    |     - context providers
    |     - snapshot/restore, optional
    |
    +-- Runtime / Port
          - allocator
          - time
          - sleep
          - mutex, optional
          - log
          - http, optional
```

关键边界：

```text
core 不认识平台
platform 不理解 Agent 推理逻辑
integration 只连接外部依赖
application 组合真实业务能力
```

## 文件职责清单

这一节描述目标职责，不表示当前文件都已经实现。当前大部分文件仍是空文件或
占位文件。

### Public Headers

| 文件 | 职责 |
|------|------|
| `include/agent.h` | 聚合公共头文件。应用侧优先只 include 这个文件。 |
| `include/cagent/types.h` | opaque `agent_t`、错误码、基础常量、请求/响应基础类型。 |
| `include/cagent/config.h` | `agent_config_t`、默认配置、资源上限默认值、system prompt 配置、默认 timeout/preset。 |
| `include/cagent/runtime.h` | `agent_runtime_t`，定义 allocator、time、log、HTTP、可选 lock/critical section 等平台注入点。 |
| `include/cagent/model.h` | model provider 抽象，定义 `agent_model_ops_t`、model request/response、response token budget。 |
| `include/cagent/tools.h` | tool 定义、tool call/result、tool 注册/注销/启停 API、JSON result 契约、parallel-safe flag。 |
| `include/cagent/skill.h` | skill 定义、skill 注册/注销/启停 API。 |
| `include/cagent/context.h` | context provider 定义、注册/注销 API、context build API、overflow 策略。 |
| `include/cagent/session.h` | session 清理、查询、turn 淘汰语义、可选 snapshot 相关 session API。 |
| `include/cagent/memory.h` | memory backend、snapshot/restore、长期记忆扩展点。 |
| `include/cagent/event.h` | event 类型、timestamp、trace_id、event payload、event callback 注册 API。 |
| `include/cagent/policy.h` | policy callback、allow/deny/confirm 决策类型。 |

### Internal Headers

| 文件 | 职责 |
|------|------|
| `src/types_internal.h` | 内部共享常量、内部 helper 类型，避免污染 public API。 |
| `src/core/agent_internal.h` | `struct agent` 内部状态定义，包括 busy、cancel_requested、deadline、stats，以及 core/llm/tools/memory 跨模块内部函数声明。 |
| `src/llm/llm_internal.h` | LLM adapter 内部 request/response、parser 中间结构、OpenAI-compatible 映射。 |
| `src/tools/tools_internal.h` | tool registry entry、tool rate state、schema/guard 内部函数声明。 |
| `src/skills/skills_internal.h` | skill registry entry、skill context builder 内部声明。 |
| `src/memory/memory_internal.h` | session entry、memory state、session message 内部结构、turn 完整性和淘汰边界。 |
| `src/runtime/runtime.h` | runtime fallback、runtime fill defaults、内部 HTTP wrapper、lock/critical section wrapper 声明。 |
| `src/runtime/runtime_openvela.h` | openvela/NuttX runtime adapter 的内部声明。 |

### Source Files

| 文件 | 职责 |
|------|------|
| `src/core/agent_core.c` | 实现 `agent_create()`、`agent_destroy()`、`agent_reset()`、`agent_cancel()`、`agent_set_model()`、event/policy callback setter、同步入口参数校验和 busy 防重入。 |
| `src/core/agent_loop.c` | 实现 bounded ReAct loop，协调 context、schema、model、tool execution、session 回填、timeout/cancel 检查和最终响应。 |
| `src/core/agent_event.c` | 统一事件派发，封装 `agent_event_emit()`，避免各模块直接触碰 callback 细节。 |
| `src/core/context_builder.c` | 管理 context provider 注册表，按 priority 拼接 system prompt、动态上下文和 skill context，并处理 context overflow。 |
| `src/runtime/runtime.c` | 提供 libc/POSIX 风格 fallback：malloc/free、now_ms、log、默认 unsupported HTTP。 |
| `src/runtime/runtime_openvela.c` | openvela/NuttX runtime adapter，例如 syslog、clock、mbedTLS/BSD socket HTTP。 |
| `src/tools/tool_registry.c` | 管理上层注册的工具，提供查找、启停、顺序执行入口，并维护 tool result JSON 契约。 |
| `src/tools/tool_schema.c` | 把已注册且 LLM 可见的工具转换为模型需要的 tools schema。 |
| `src/tools/tool_guard.c` | 执行通用安全检查：disabled、输入大小、输出大小、副作用限流、确认需求。 |
| `src/skills/skill_registry.c` | 管理 skill 注册表，并提供默认 skill context provider 输出。 |
| `src/memory/session_mgr.c` | 管理 session history，追加 user/assistant/tool messages，并按完整 turn 淘汰。 |
| `src/memory/memory_store.c` | 放 memory backend、snapshot/restore、长期记忆扩展点；MVP 可先只做空实现。 |
| `src/llm/model_openai.c` | OpenAI-compatible non-streaming provider，构建请求、解析 final/tool_calls，并通过 runtime HTTP 发送。 |
| `src/llm/model_mock.c` | Mock model provider，用于 host demo 和 ReAct loop 测试。 |
| `src/llm/llm_parse.c` | 解析模型响应中的 final content 和 tool_calls。 |
| `src/llm/llm_router.c` | 可选 model router adapter，不应成为 core 必需依赖。 |

### Build And Docs

| 文件 | 职责 |
|------|------|
| `CMakeLists.txt` | 生成 `cagent` 静态库/对象库，声明 include path、源文件和可选模块。 |
| `Kconfig` | 嵌入式裁剪配置：最大工具数、最大 session、是否启用 OpenAI adapter/step loop/snapshot。 |
| `README.md` | 开发者快速开始：最小接入、mock demo、构建方式。 |
| `docs/architecture.md` | 架构边界、文件职责、推理流程和演进计划。 |
| `docs/plan.md` | 任务拆分和阶段计划。 |
| `examples/mock_react/` | 无网络 mock ReAct demo，验证模型工具调用闭环。 |
| `tests/` | host 单元测试和 mock ReAct 集成测试。 |

### Public API

`include/agent.h` 是聚合头文件，应用侧优先使用：

```c
#include <agent.h>
```

`include/cagent/*.h` 按模块拆分公共类型和 API：

```text
types.h      基础类型、错误码、常量
config.h     agent_config_t、默认配置
runtime.h    agent_runtime_t
model.h      model provider API
tools.h      tool 定义和注册 API
skill.h      skill 定义和注册 API
context.h    context provider API
session.h    session 管理 API
memory.h     memory/snapshot API
event.h      event callback API
policy.h     policy callback API
```

内部头文件只能被 `src/` 使用，不应暴露给应用。

### Core

`src/core` 负责 Agent 生命周期和推理循环。

MVP 需要实现：

```c
agent_t *agent_create(const agent_config_t *config);
void agent_destroy(agent_t *agent);

int agent_run(agent_t *agent,
              const agent_request_t *request,
              agent_response_t *response);
```

后续再增加：

```c
int agent_begin(agent_t *agent, const agent_request_t *request);
int agent_step(agent_t *agent, agent_step_result_t *step);
int agent_cancel(agent_t *agent);
```

同步模式也必须支持 `agent_cancel()`。它是 cooperative cancel：外部线程、按键、
MQTT 断连或 watchdog 预警可以请求取消，但正在阻塞的 model/tool 调用只有在底层
provider 支持 timeout/cancel 时才能立即退出。否则 loop 会在下一次检查点返回
`AGENT_ERROR_CANCELLED`。

Agent 还需要提供清理路径：

```c
int agent_reset(agent_t *agent);
int agent_session_clear(agent_t *agent, const char *session_id);
int agent_session_clear_all(agent_t *agent);
```

`agent_reset()` 不应默认清除已注册 tools、skills、model 或 runtime；它主要清理
cancel flag、stats、临时状态和 session。

同步入口的内部调用链：

```text
agent_run()
  -> validate request and limits
  -> append user message
  -> for each iteration:
     -> build context
     -> build tool schema
     -> model.complete()
     -> parse final reply or tool calls
     -> if final reply:
        -> append assistant message
        -> return
     -> if tool calls:
        -> append assistant tool_calls message
        -> execute tools
        -> append tool result messages
        -> continue
  -> return iteration limit error
```

## ReAct 推理循环流程

ReAct loop 的目标是让模型在“思考/调用工具/读取工具结果/最终回答”之间循环，
但在 C 库里必须把每一步变成可控、可观测、可中断、资源有上限的调用链。

### 高层流程

```text
Application
  -> agent_run()
     -> agent_loop_run()
        -> build context
        -> build tool schema
        -> model.complete()
        -> parse model response
        -> execute tool calls
        -> append tool results
        -> repeat or final
```

### 文件级调用逻辑

```text
include/agent.h
  -> exposes agent_run()

src/core/agent_core.c
  agent_run()
    -> validate agent/request/response
    -> check ctx->busy
    -> clear ctx->cancel_requested
    -> compute request deadline from limits.timeout_ms
    -> set ctx->busy = true
    -> agent_event_emit(REQUEST_START, timestamp_ms, trace_id)
    -> agent_loop_run()
    -> agent_event_emit(REQUEST_DONE)
    -> set ctx->busy = false

  agent_cancel()
    -> set ctx->cancel_requested
    -> call model.cancel if supported

src/core/agent_loop.c
  agent_loop_run()
    -> session_mgr.c: agent_session_add_user()
    -> for iter < config.max_steps:
       -> check cancel_requested
       -> check elapsed_ms <= limits.timeout_ms
       -> context_builder.c: agent_context_build()
       -> tool_schema.c: agent_tool_schema_build()
       -> agent_event.c: agent_event_emit(LLM_START)
       -> model_openai.c / model provider: agent_model_complete(timeout, max_output_tokens)
       -> agent_event.c: agent_event_emit(LLM_END)
       -> llm_parse.c: parse content/tool_calls
       -> if final content:
          -> session_mgr.c: agent_session_add_assistant()
          -> copy response
          -> agent_event.c: agent_event_emit(FINAL)
          -> return AGENT_OK
       -> if tool_calls:
          -> session_mgr.c: agent_session_add_assistant_tool_calls()
          -> for each tool call:
             -> check cancel_requested
             -> check request deadline
             -> policy callback: agent_policy_check()
             -> tool_guard.c: agent_tool_guard_check()
             -> agent_event.c: agent_event_emit(TOOL_START)
             -> tool_registry.c: agent_tool_execute(per_tool_timeout)
             -> agent_event.c: agent_event_emit(TOOL_END)
             -> session_mgr.c: agent_session_add_tool()
          -> continue next iteration
    -> return AGENT_ERROR_LIMIT
```

### 单轮详细时序

```text
1. 保存用户输入
   agent_loop.c
     -> agent_session_add_user(session_id, input)

2. 构建上下文
   context_builder.c
     -> append config.system_prompt
     -> call registered context providers by priority
     -> call skill context provider
     -> output context buffer

3. 构建工具 schema
   tool_schema.c
     -> iterate registered tools
     -> skip disabled tools
     -> skip tools not visible to LLM
     -> emit JSON schema array

4. 调模型
   model_openai.c or model provider
     -> build messages from session
     -> attach context as system message
     -> attach tools schema
     -> apply max_output_tokens
     -> apply per_model_timeout_ms
     -> call runtime.http_post or custom model ops

5. 解析模型响应
   llm_parse.c
     -> parse final assistant content
     -> parse tool_calls[].id
     -> parse tool_calls[].function.name
     -> parse tool_calls[].function.arguments

6. 如果是最终回答
   session_mgr.c
     -> add assistant message
   agent_loop.c
     -> write response->output
     -> emit FINAL

7. 如果是工具调用
   session_mgr.c
     -> add assistant message with tool_calls
   tool_registry.c
     -> find tool by name
   policy.h callback
     -> allow / deny / require confirmation
   tool_guard.c
     -> common checks
   developer tool handler
     -> execute real capability
   session_mgr.c
     -> add tool result message
   agent_loop.c
     -> next iteration
```

### 关键状态流转

```text
agent_t
  config
  runtime
  model
  tools registry
  skills registry
  context providers
  sessions
  event callback
  policy callback
  busy flag
  stats
```

每次 `agent_run()` 应更新：

```text
last_error
iterations
model_calls
tool_calls
elapsed_ms
```

### 错误处理原则

- 参数错误立即返回 `AGENT_ERROR_INVALID`。
- 同一个 `agent_t` 重入时返回 `AGENT_ERROR_BUSY`。
- 外部取消返回 `AGENT_ERROR_CANCELLED`。
- 整体请求超时、单次 model 超时或单次 tool 超时返回 `AGENT_ERROR_TIMEOUT`。
- context 拼接溢出返回 `AGENT_ERROR_CONTEXT_OVERFLOW`。
- context/schema/session buffer 不够返回 `AGENT_ERROR_LIMIT`。
- model provider 失败返回 `AGENT_ERROR_MODEL` 或 `AGENT_ERROR_NETWORK`。
- tool 不存在返回结构化 tool error，并让模型有机会继续推理。
- policy 拒绝返回结构化 tool error，不直接崩掉整个 loop。
- 达到最大迭代次数返回 `AGENT_ERROR_LIMIT`。

### 为什么 session 必须记录 tool_calls

OpenAI-compatible tool calling 需要保留这组消息顺序：

```text
user
assistant(tool_calls=[...])
tool(tool_call_id=...)
assistant(final)
```

因此 `agent_loop.c` 不能只执行工具，还必须调用 `session_mgr.c` 把
assistant tool_calls 和 tool result 都写回 session。否则下一轮模型无法知道工具
结果对应哪个 call id。

### Timeout 和 Cancel

`max_steps` 只能限制推理轮数，不能限制真实耗时。嵌入式场景必须同时支持整体
timeout、单次 model timeout、单次 tool timeout 和 cooperative cancel。

推荐 limits：

```c
typedef struct {
    uint32_t max_steps;
    uint32_t timeout_ms;
    uint32_t per_model_timeout_ms;
    uint32_t per_tool_timeout_ms;
    uint32_t max_tool_calls;
    uint32_t max_output_tokens;
} agent_limits_t;
```

检查点：

```text
agent_run start:
  start_ms = runtime.now_ms()
  deadline = start_ms + timeout_ms

each iteration start:
  check cancel_requested
  check deadline

before model call:
  check cancel_requested
  pass per_model_timeout_ms and max_output_tokens to provider

before each tool call:
  check cancel_requested
  check deadline
  pass per_tool_timeout_ms to tool budget

after model/tool return:
  update elapsed_ms and stats
```

取消语义：

```text
agent_cancel() requests cancellation.
It does not forcibly kill arbitrary blocking C code.
It returns at the next cooperative check unless model/tool/runtime supports
active cancellation.
```

内部状态建议：

```c
volatile bool cancel_requested;
bool busy;
uint64_t run_start_ms;
uint64_t run_deadline_ms;
```

在 RTOS/多核环境中，`cancel_requested` 需要 runtime lock、critical section 或
平台 atomic 保证可见性。

### Tool 执行并发模型

MVP 明确采用顺序执行：

```text
tool_call[0] -> result[0]
tool_call[1] -> result[1]
...
all tool results appended
next model iteration
```

这是嵌入式优先的设计决策：

- 内存峰值低。
- 外设资源冲突少。
- 日志顺序清晰。
- 取消和 timeout 更容易处理。
- 不依赖 runtime thread/task。

未来可选并行执行必须显式启用：

```c
#define AGENT_TOOL_FLAG_PARALLEL_SAFE (1u << N)
```

并行执行还需要：

```text
runtime task/thread support
mutex/critical section
tool output buffer pool
join timeout
parallel tool count limit
cancel propagation
```

默认不应启用并行工具。

### Runtime

`runtime` 是跨平台边界。core 只能通过 `agent_runtime_t` 调用平台能力。

建议公共定义：

```c
typedef struct {
    void *user_data;

    void *(*malloc)(size_t size, void *user_data);
    void (*free)(void *ptr, void *user_data);

    uint64_t (*now_ms)(void *user_data);
    void (*sleep_ms)(uint32_t ms, void *user_data);

    void (*log)(int level,
                const char *tag,
                const char *message,
                void *user_data);

    int (*http_post)(const agent_http_request_t *request,
                     agent_http_response_t *response,
                     void *user_data);

    void *(*mutex_create)(void *user_data);
    void (*mutex_destroy)(void *mutex, void *user_data);
    int (*mutex_lock)(void *mutex, uint32_t timeout_ms, void *user_data);
    void (*mutex_unlock)(void *mutex, void *user_data);

    uint32_t (*enter_critical)(void *user_data);
    void (*exit_critical)(uint32_t key, void *user_data);
} agent_runtime_t;
```

MVP 可以先只实现：

```text
malloc
free
now_ms
log
```

`http_post` 可以先作为 optional callback，由 OpenAI-compatible adapter 使用。
如果没有设置，默认返回 `AGENT_ERROR_NOTSUP`。

`mutex_*` 和 `critical section` 可以是 optional，但一旦支持 `agent_cancel()`、
多线程调用或多核平台，至少要提供其中一种同步机制。ISR 场景不应调用会阻塞的
mutex，只能使用 critical section 或平台 atomic 设置 cancel flag。

平台映射：

| 平台 | runtime 实现建议 |
|------|------------------|
| POSIX | libc malloc/free、gettimeofday、stderr log、mock/curl HTTP |
| openvela/NuttX | libc/kmm、clock_gettime、syslog、BSD socket + mbedTLS |
| ESP-IDF | heap_caps、esp_timer、ESP_LOG、esp_http_client |
| STM32/FreeRTOS | pvPortMalloc、xTaskGetTickCount、串口 log、lwIP/AT |
| bare-metal | 静态 allocator、硬件 tick、串口 log、上层 transport |

### Context Provider

Context provider 用来把动态上下文注入 LLM system message。它不关心上下文来源，
只负责把内容写入 buffer。

推荐 API：

```c
typedef int (*agent_context_fn)(char *buf,
                                size_t size,
                                void *user_data);

typedef struct {
    const char *name;
    int priority;
    agent_context_fn build;
    void *user_data;
} agent_context_provider_t;

int agent_register_context_provider(agent_t *agent,
                                    const agent_context_provider_t *provider);

int agent_unregister_context_provider(agent_t *agent,
                                      const char *name);

int agent_context_build(agent_t *agent,
                        char *buf,
                        size_t size);
```

构建顺序：

```text
config.system_prompt
  + providers ordered by priority
  + skill provider output
  -> context string
  -> model system message
```

Context overflow 不允许静默截断。推荐规则：

```text
1. system_prompt 和 critical provider 不可丢。
2. provider 按 priority 构建，低优先级先被跳过或降级。
3. provider 可根据 budget 输出摘要。
4. skill context 默认先 summary，再必要时裁剪。
5. 如果 critical context 放不下，返回 AGENT_ERROR_CONTEXT_OVERFLOW。
```

推荐 provider result：

```c
typedef struct {
    size_t written;
    bool truncated;
    bool required;
} agent_context_result_t;
```

至少在 MVP 中，`agent_context_build()` 必须检测 `snprintf`/append 溢出并返回
明确错误，不能生成被静默截断的 system message。

### Tools

Tool 是 Agent 可以执行外部动作的扩展点。

推荐公共定义：

```c
typedef int (*agent_tool_fn)(const agent_tool_call_t *call,
                             agent_tool_result_t *result,
                             void *user_data);

typedef struct {
    const char    *name;
    uint16_t       group_id;

    const char    *description;
    const char    *input_schema_json;

    agent_tool_fn  execute;
    void          *user_data;

    uint32_t       flags;
    uint32_t       timeout_ms;
} agent_tool_t;
```

工具注册：

```c
int agent_register_tool(agent_t *agent, const agent_tool_t *tool);

int agent_unregister_tool(agent_t *agent, const char *name);
int agent_tool_set_enabled(agent_t *agent, const char *name, bool enabled);
```

字段契约：

| 字段 | 说明 |
|------|------|
| `name` | LLM 调用和 registry lookup 使用的全局唯一工具名。必须非空、长度有限、字符集受限。 |
| `group_id` | 应用层定义的工具分组，`0` 表示 default。core 不解释具体业务含义。 |
| `description` | 给模型看的工具用途说明。应简短，复杂使用方式放入 Skill。 |
| `input_schema_json` | JSON object schema。`NULL` 或空字符串表示无参数工具。 |
| `execute` | 开发者提供的工具执行函数。 |
| `user_data` | 应用私有上下文，例如 driver handle、RTOS queue、设备状态。 |
| `flags` | LLM 可见性、副作用、确认需求、禁用状态、并行安全等行为标记。 |
| `timeout_ms` | 单工具 timeout 覆盖值；`0` 表示使用全局 `per_tool_timeout_ms`。 |

`group_id` 适合做：

```text
按 group 启停工具
按 active skill 暴露工具
低内存时只暴露关键 group
policy 按 group 做权限判断
MCP/remote tools 使用独立 group
```

如果需要可读 group 名，不建议把字符串塞进每个 tool；可以后续增加独立 group
metadata：

```c
int agent_register_tool_group(agent_t *agent,
                              uint16_t group_id,
                              const char *name);
```

字符串所有权必须在实现中明确。MVP 推荐两种之一：

```text
策略 A：注册时复制 name/description/schema 到内部固定 buffer。
策略 B：要求字符串生命周期长于 agent。
```

嵌入式默认更倾向策略 B 或静态工具表；如果支持热更新工具，再引入复制策略。

工具执行链：

```text
LLM tool_call
  -> tool registry lookup
  -> policy callback
  -> guard check
  -> developer handler
  -> normalized tool result JSON
  -> session tool message
```

核心库不内置业务工具。通用示例工具可以放在 `examples/` 或 optional
`integrations/`。

Tool result 契约：

```text
MVP: tool handler 输出必须是 UTF-8 JSON string。
核心不负责序列化任意 C struct。
核心只负责补齐空成功结果和包装错误结果。
```

推荐成功结果：

```json
{"ok":true,"data":{}}
```

推荐失败结果：

```json
{"ok":false,"code":-5,"message":"tool not found"}
```

如果工具成功但没有写输出，核心可以补齐：

```json
{"ok":true}
```

二进制数据应由工具自行转换为 base64、文件引用、资源 URI 或摘要，不应直接塞入
tool result buffer。

Tool timeout 语义：

```text
effective_tool_timeout =
  tool.timeout_ms != 0 ? tool.timeout_ms : limits.per_tool_timeout_ms

effective_deadline =
  min(now + effective_tool_timeout, request_deadline)
```

timeout 是 cooperative timeout。若工具阻塞在不可中断驱动调用中，core 不能强制
杀掉该函数；工具应尽量使用可超时的驱动 API 或周期性检查 call/deadline。

Tool flags 建议：

```c
#define AGENT_TOOL_FLAG_LLM_VISIBLE      (1u << 0)
#define AGENT_TOOL_FLAG_READ_ONLY        (1u << 1)
#define AGENT_TOOL_FLAG_SIDE_EFFECT      (1u << 2)
#define AGENT_TOOL_FLAG_REQUIRES_CONFIRM (1u << 3)
#define AGENT_TOOL_FLAG_DISABLED         (1u << 4)
#define AGENT_TOOL_FLAG_PARALLEL_SAFE    (1u << 5)
```

MVP 即使定义了 `PARALLEL_SAFE`，也仍然默认顺序执行。并行执行必须作为后续可选
能力，并依赖 runtime task/mutex/buffer pool 支持。

### Skills

Skill 是上下文资源，不是默认可执行代码。它描述 Agent 在某类任务中应该如何
使用工具。

推荐公共定义：

```c
typedef struct {
    const char *name;
    const char *description;
    const char *content;
    uint32_t flags;
} agent_skill_t;
```

Skill 注册：

```c
int agent_register_skill(agent_t *agent, const agent_skill_t *skill);
int agent_unregister_skill(agent_t *agent, const char *name);
int agent_skill_set_enabled(agent_t *agent, const char *name, bool enabled);
```

Skill 文件加载、Markdown 解析、热更新、云端同步由上层 loader 实现。核心只接收
已经解析好的 `agent_skill_t`。

### Memory / Session

MVP 先实现 bounded in-memory session。它必须能表达 tool calling 所需的消息
顺序：

```text
user(content)
assistant(content=null, tool_calls=[...])
tool(tool_call_id, content)
assistant(final content)
```

推荐策略：

- 固定 session 数上限。
- 固定每个 session 的消息数上限。
- 消息满时按 turn 淘汰。
- 避免留下孤立 tool result 或孤立 assistant tool_calls。
- snapshot/restore 后续作为可选能力。

Turn 边界定义：

```text
turn starts:
  user message

turn ends:
  assistant final message
```

完整 turn 示例：

```text
turn 1:
  user
  assistant(tool_calls=[...])
  tool
  tool
  assistant(final)

turn 2:
  user
  assistant(final)
```

淘汰策略：

```text
只淘汰完整 turn。
不能只删除 tool message。
不能留下孤立 assistant(tool_calls)。
尾部未完成 turn 不参与淘汰。
如果找不到完整 turn，返回 AGENT_ERROR_LIMIT 或清空 session，具体策略由配置决定。
```

### LLM / Model Provider

核心不应该直接绑定某个云服务。推荐模型抽象：

```c
typedef struct {
    int (*complete)(void *ctx,
                    const agent_model_request_t *request,
                    agent_model_response_t *response);

    int (*cancel)(void *ctx);
} agent_model_ops_t;
```

Model request 应包含输出 token 预算：

```c
typedef struct {
    const char *context;
    const char *tools_json;
    const char *session_id;
    uint32_t timeout_ms;
    uint32_t max_output_tokens;
} agent_model_request_t;
```

`max_output_tokens` 用于限制模型回复长度，OpenAI-compatible provider 可映射到
对应的 response token limit。命名优先使用 `max_output_tokens`，避免不同 provider
对 `max_tokens` 的语义差异。

第一阶段建议实现：

- `model_mock.c`：用于 host demo 和单元测试。
- `model_openai.c`：非流式 Chat Completions provider。
- `llm_parse.c`：解析 final content 和 tool_calls。

`llm_router.c` 可以保留为 optional adapter，但不应成为 core 必需依赖。

命名约定：

```text
model_*.c    provider 实现，例如 model_openai.c、model_mock.c
llm_*.c      LLM 子系统内部工具，例如 llm_parse.c、llm_router.c
```

### Event / Policy / Guard

Event 用于调试、UI、trace、审计和 watchdog。

推荐事件：

```text
REQUEST_START
LLM_START
LLM_END
TOOL_START
TOOL_END
ERROR
FINAL
REQUEST_DONE
```

推荐 event payload：

```c
typedef struct {
    agent_event_type_t type;
    uint64_t timestamp_ms;
    const char *trace_id;
    const char *session_id;
    uint32_t iteration;
    int error_code;
    union {
        agent_llm_event_t llm;
        agent_tool_event_t tool;
        agent_error_event_t error;
    } data;
} agent_event_t;
```

`timestamp_ms` 由 `runtime.now_ms()` 在 `agent_event_emit()` 中填充。`trace_id`
来自 request；若上层未提供，core 可以使用递增 request id 或留空。这样上层日志、
UI、审计系统不需要自己关联事件时间线。

Policy 用于由上层产品决定工具调用是否允许：

```text
ALLOW
DENY
REQUIRE_CONFIRMATION
```

Guard 负责核心通用检查：

- 工具是否注册。
- 工具是否禁用。
- 输入大小是否超限。
- 输出大小是否超限。
- tool call 次数是否超限。
- 有副作用工具是否限流。

## 嵌入式资源优化策略

嵌入式场景下，Agent 的主要风险不是单个模块复杂，而是 context、tools schema、
session history、model request/response 和 tool output 同时增长。`cAGENT` 的资源
策略必须围绕 **可预测、可裁剪、可降级** 设计。

### Runtime 自适应内存

核心代码不应到处直接 `malloc()` 大 buffer。所有内存申请都应经过 runtime 或
agent-local allocator。

推荐内存模式：

```c
typedef enum {
    AGENT_ALLOC_STATIC_ONLY,
    AGENT_ALLOC_ARENA,
    AGENT_ALLOC_HEAP_WITH_LIMIT,
    AGENT_ALLOC_EXTERNAL
} agent_alloc_mode_t;
```

推荐配置：

```c
typedef struct {
    agent_alloc_mode_t mode;
    void *arena;
    size_t arena_size;
    size_t max_heap_bytes;
    size_t reserve_bytes;
} agent_memory_config_t;
```

平台建议：

| 平台等级 | 推荐策略 |
|----------|----------|
| 小 MCU / bare-metal | `STATIC_ONLY` 或固定 arena |
| RTOS 中等 RAM | request arena + 少量 heap fallback |
| openvela / ESP-IDF | heap limit + runtime heap pressure 检测 |
| POSIX/host test | libc malloc/free + sanitizer/test instrumentation |

内存对象按生命周期分层：

```text
agent lifetime:
  agent context
  registries
  model provider handle
  session storage

single request lifetime:
  context buffer
  tools schema buffer
  model request buffer
  model response buffer
  parse temp buffer
  tool output buffer
```

一次 `agent_run()` 结束后，请求级临时内存应统一释放或 arena reset，避免碎片。

### Request Arena

建议每次请求拥有一个临时 arena：

```c
typedef struct {
    uint8_t *base;
    size_t size;
    size_t used;
    size_t peak;
} agent_arena_t;
```

典型用途：

```text
context buffer
tools schema buffer
model request buffer
model response buffer
tool output buffer
parser scratch
```

优势：

- 申请路径简单。
- 无长期碎片。
- 可以统计单次请求 peak usage。
- OOM 时可明确降级或失败。

### Runtime 内存压力感知

runtime 可选提供内存状态回调：

```c
size_t (*heap_free)(void *user_data);
size_t (*heap_largest_free_block)(void *user_data);
```

核心根据内存压力选择运行模式：

```c
typedef enum {
    AGENT_MEMORY_PRESSURE_NORMAL,
    AGENT_MEMORY_PRESSURE_LOW,
    AGENT_MEMORY_PRESSURE_CRITICAL
} agent_memory_pressure_t;
```

建议策略：

| 压力等级 | 行为 |
|----------|------|
| NORMAL | 使用 preferred context、完整 tools schema、正常 session history |
| LOW | 使用 min context、只启用关键 provider、限制 tools 数、截断 tool output |
| CRITICAL | 拒绝非关键请求，或禁用 tool call，仅保留 current turn |

### Context Buffer 预算

context buffer 不应该只有一个固定最大值。推荐三段式预算：

```c
typedef struct {
    size_t min_context_bytes;
    size_t preferred_context_bytes;
    size_t max_context_bytes;
} agent_context_limits_t;
```

构建策略：

```text
1. 尝试 preferred_context_bytes。
2. 内存紧张时降到 min_context_bytes。
3. provider 输出过大时按 priority 和 importance 裁剪。
4. critical context 不可丢。
5. 仍然不够时返回 AGENT_ERROR_LIMIT。
```

context provider 建议带预算输入：

```c
typedef struct {
    size_t budget_bytes;
    uint32_t flags;
} agent_context_request_t;
```

provider 可以根据预算输出完整版或摘要版。

### Context Provider 降级

provider 需要区分重要性：

```c
typedef enum {
    AGENT_CONTEXT_CRITICAL,
    AGENT_CONTEXT_NORMAL,
    AGENT_CONTEXT_OPTIONAL
} agent_context_importance_t;
```

推荐保留顺序：

```text
system prompt             critical
safety policy             critical
current device state      critical
active skill summary      normal
long-term memory summary  normal
verbose debug state       optional
```

空间不足时：

```text
保留 critical
裁剪 normal
丢弃 optional
normal 输出 summary
summary 再裁剪
最后失败
```

这样可以避免 memory summary 或 debug context 挤掉安全约束。

### Tools Schema Cache

tools schema 可能非常大，不应每轮无条件重建。

推荐在 tool registry 中维护 schema cache：

```c
typedef struct {
    char *json;
    size_t size;
    bool dirty;
    uint32_t version;
} agent_tool_schema_cache_t;
```

触发 dirty：

```text
register tool
unregister tool
enable/disable tool
tool visibility changed
MCP/remote tool list changed
```

低内存策略：

```text
只暴露当前 active skill 相关工具
只暴露 top-N 工具
隐藏 optional 工具
禁用大 schema 工具
```

核心原则：tool registry 管理工具，schema builder 描述工具，ReAct loop 只消费
schema，不关心 schema 缓存细节。

### Tool Output Budget

工具输出必须有预算，否则 `read_log`、`fetch_url`、`shell` 等工具很容易返回过大
内容。

推荐工具预算：

```c
typedef struct {
    size_t max_output_bytes;
    uint32_t timeout_ms;
} agent_tool_budget_t;
```

工具结果应能表达截断：

```c
typedef struct {
    int status;
    const char *content_json;
    size_t content_size;
    bool truncated;
} agent_tool_result_t;
```

截断结果建议统一为结构化 JSON：

```json
{
  "ok": true,
  "truncated": true,
  "content": "...",
  "note": "output truncated"
}
```

这样模型可以继续推理，而不是因为 buffer 不够直接失败。

### Model Request / Response Buffer

OpenAI-compatible 请求体由多部分叠加：

```text
system context
session history
tools schema
user input
assistant tool_calls
tool results
```

构造请求前应先估算大小：

```c
size_t agent_estimate_request_size(agent_t *agent,
                                   const char *session_id,
                                   size_t context_size,
                                   size_t tools_schema_size);
```

估算超过限制时按顺序降级：

```text
压缩/淘汰 session
裁剪 optional context provider
减少 tools schema
截断 tool result
降低 response buffer
最后返回 AGENT_ERROR_LIMIT
```

response buffer 也必须有硬上限。后续支持 streaming 后，也应该限制累计输出大小。

### Session History 策略

session 不能无限增长。推荐支持三种策略：

```c
typedef enum {
    AGENT_SESSION_CURRENT_ONLY,
    AGENT_SESSION_EVICT_TURN,
    AGENT_SESSION_SUMMARIZE
} agent_session_policy_t;
```

建议默认：

| 内存等级 | Session 策略 |
|----------|--------------|
| tiny | `CURRENT_ONLY` |
| RTOS | `EVICT_TURN` |
| openvela/POSIX | `EVICT_TURN`，可选 `SUMMARIZE` |

无论哪种策略，都必须保持 tool calling 消息组完整：

```text
user
assistant(tool_calls)
tool(...)
assistant(final)
```

### 资源 Preset

建议提供几个预设配置，降低开发者接入成本：

```c
agent_config_t config = AGENT_CONFIG_PRESET_TINY;
agent_config_t config = AGENT_CONFIG_PRESET_RTOS;
agent_config_t config = AGENT_CONFIG_PRESET_LINUX;
```

建议含义：

| Preset | 适用场景 | 行为 |
|--------|----------|------|
| TINY | 小 MCU / bare-metal | 静态 buffer、少工具、current-only session、无默认 HTTP |
| RTOS | ESP-IDF / FreeRTOS | arena、有限 session、有限 tools、短 tool output |
| LINUX | openvela / POSIX | heap limit、较大 context、更多 tools、可启用 HTTP adapter |

### Stats 与可观测性

开发者需要知道 Agent 到底用了多少资源。建议 stats 至少包含：

```c
typedef struct {
    uint32_t iterations;
    uint32_t model_calls;
    uint32_t tool_calls;
    uint32_t elapsed_ms;
    size_t arena_peak_bytes;
    size_t context_bytes;
    size_t tools_schema_bytes;
    size_t model_request_bytes;
    size_t model_response_bytes;
    size_t max_tool_output_bytes;
    int last_error;
} agent_stats_t;
```

这些数据可通过：

```c
int agent_get_stats(agent_t *agent, agent_stats_t *stats);
```

提供给日志、UI、测试和资源调优。

### 优化落地顺序

第一阶段先建立硬边界：

- runtime allocator。
- request arena。
- context/tool/model/session buffer 上限。
- tool output budget。
- session turn eviction。
- stats peak memory。

第二阶段做自适应：

- memory pressure detection。
- context provider 降级。
- tools schema cache。
- active skill 相关工具暴露。
- low-memory mode。

第三阶段做平台优化：

- openvela heap/largest-block 检测。
- ESP-IDF heap_caps 检测。
- STM32 static arena。
- flash-backed snapshot。
- streaming response 累计上限。

## 配置与裁剪

嵌入式构建必须允许裁剪。`cAGENT` 的配置应该分成两层：

```text
Kconfig / CMake compile-time config:
  编译哪些模块
  默认资源预算
  默认 timeout
  默认 feature 开关
  平台 runtime adapter

runtime config:
  API key
  model name
  当前启用的 tool
  当前注册的 skill
  MCP server 地址
  session id
  用户/设备业务配置
```

原则：

```text
Kconfig 管编译形态和默认预算。
运行期 API 管具体行为和业务状态。
不要把 API key、具体模型、业务工具配置写进 Kconfig。
```

### Kconfig 菜单结构

建议顶层结构：

```kconfig
menuconfig CAGENT
    bool "cAGENT core library"
    default n
    help
      Enable the generic C Agent core library.

if CAGENT

menu "Resource preset"
menu "Core limits"
menu "Timeouts"
menu "Features"
menu "Model providers"
menu "Runtime adapters"
menu "Examples and tests"

endif
```

### Resource Preset

使用 preset 给不同设备等级提供默认值：

```kconfig
choice CAGENT_PRESET
    prompt "Resource preset"
    default CAGENT_PRESET_RTOS

config CAGENT_PRESET_TINY
    bool "Tiny MCU / bare-metal"

config CAGENT_PRESET_RTOS
    bool "RTOS device"

config CAGENT_PRESET_LINUX
    bool "Linux/openvela/POSIX"

endchoice
```

Preset 只提供默认资源预算。运行时仍可通过 `agent_config_t` 或 `agent_limits_t`
覆盖默认值。

### Core Limits

建议 Kconfig/CMake 映射这些资源上限：

```text
CONFIG_CAGENT_MAX_TOOLS
CONFIG_CAGENT_MAX_SKILLS
CONFIG_CAGENT_MAX_CONTEXT_PROVIDERS
CONFIG_CAGENT_MAX_SESSIONS
CONFIG_CAGENT_SESSION_MAX_MSGS
CONFIG_CAGENT_CONTEXT_MAX
CONFIG_CAGENT_TOOL_ARGS_MAX
CONFIG_CAGENT_TOOL_OUTPUT_MAX
```

示例：

```kconfig
menu "Core limits"

config CAGENT_MAX_TOOLS
    int "Maximum registered tools"
    default 4 if CAGENT_PRESET_TINY
    default 12 if CAGENT_PRESET_RTOS
    default 32 if CAGENT_PRESET_LINUX

config CAGENT_MAX_SKILLS
    int "Maximum registered skills"
    default 2 if CAGENT_PRESET_TINY
    default 8 if CAGENT_PRESET_RTOS
    default 32 if CAGENT_PRESET_LINUX

config CAGENT_MAX_CONTEXT_PROVIDERS
    int "Maximum context providers"
    default 2 if CAGENT_PRESET_TINY
    default 6 if CAGENT_PRESET_RTOS
    default 12 if CAGENT_PRESET_LINUX

config CAGENT_MAX_SESSIONS
    int "Maximum sessions"
    default 1 if CAGENT_PRESET_TINY
    default 4 if CAGENT_PRESET_RTOS
    default 16 if CAGENT_PRESET_LINUX

config CAGENT_SESSION_MAX_MSGS
    int "Maximum messages per session"
    default 2 if CAGENT_PRESET_TINY
    default 8 if CAGENT_PRESET_RTOS
    default 32 if CAGENT_PRESET_LINUX

config CAGENT_CONTEXT_MAX
    int "Context buffer max bytes"
    default 1024 if CAGENT_PRESET_TINY
    default 4096 if CAGENT_PRESET_RTOS
    default 8192 if CAGENT_PRESET_LINUX

config CAGENT_TOOL_ARGS_MAX
    int "Tool arguments max bytes"
    default 512 if CAGENT_PRESET_TINY
    default 2048 if CAGENT_PRESET_RTOS
    default 4096 if CAGENT_PRESET_LINUX

config CAGENT_TOOL_OUTPUT_MAX
    int "Tool output max bytes"
    default 512 if CAGENT_PRESET_TINY
    default 2048 if CAGENT_PRESET_RTOS
    default 8192 if CAGENT_PRESET_LINUX

endmenu
```

### Timeouts

Timeout 默认值适合编译期配置，但每次请求仍应允许运行期覆盖。

```kconfig
menu "Timeouts"

config CAGENT_DEFAULT_TIMEOUT_MS
    int "Default agent_run timeout in ms"
    default 10000 if CAGENT_PRESET_TINY
    default 30000 if CAGENT_PRESET_RTOS
    default 60000 if CAGENT_PRESET_LINUX

config CAGENT_DEFAULT_MODEL_TIMEOUT_MS
    int "Default model call timeout in ms"
    default 8000 if CAGENT_PRESET_TINY
    default 20000 if CAGENT_PRESET_RTOS
    default 60000 if CAGENT_PRESET_LINUX

config CAGENT_DEFAULT_TOOL_TIMEOUT_MS
    int "Default tool timeout in ms"
    default 1000 if CAGENT_PRESET_TINY
    default 5000 if CAGENT_PRESET_RTOS
    default 10000 if CAGENT_PRESET_LINUX

config CAGENT_DEFAULT_MAX_OUTPUT_TOKENS
    int "Default model output token budget"
    default 256 if CAGENT_PRESET_TINY
    default 1024 if CAGENT_PRESET_RTOS
    default 4096 if CAGENT_PRESET_LINUX

endmenu
```

### Feature Switches

功能开关应按能力组划分，不要细到每个 `.c` 文件。

```kconfig
menu "Features"

config CAGENT_ENABLE_CANCEL
    bool "Enable agent_cancel"
    default y

config CAGENT_ENABLE_EVENTS
    bool "Enable event callback"
    default y

config CAGENT_ENABLE_POLICY
    bool "Enable policy callback"
    default y

config CAGENT_ENABLE_MUTEX
    bool "Enable runtime mutex/critical-section hooks"
    default y if !CAGENT_PRESET_TINY
    default n if CAGENT_PRESET_TINY

config CAGENT_ENABLE_CONTEXT_DEGRADE
    bool "Enable context degradation"
    default y

config CAGENT_ENABLE_TOOL_SCHEMA_CACHE
    bool "Enable tool schema cache"
    default y

config CAGENT_ENABLE_STEP_LOOP
    bool "Enable manual step loop"
    default n

config CAGENT_ENABLE_SNAPSHOT
    bool "Enable memory snapshot/restore"
    default n

config CAGENT_ENABLE_STREAMING
    bool "Enable streaming model response"
    default n

endmenu
```

不建议给这些核心模块设置关闭开关：

```text
tool registry
tool guard
session manager
context builder
runtime abstraction
```

这些是 core 的基本组成，关闭它们会让 API 语义破裂。

### Model Providers

Model provider 是可选模块：

```kconfig
menu "Model providers"

config CAGENT_MODEL_MOCK
    bool "Enable mock model provider"
    default y

config CAGENT_MODEL_OPENAI
    bool "Enable OpenAI-compatible model provider"
    default y if !CAGENT_PRESET_TINY
    default n if CAGENT_PRESET_TINY

config CAGENT_MODEL_ROUTER
    bool "Enable model router"
    default n
    depends on CAGENT_MODEL_OPENAI

endmenu
```

`CAGENT_MODEL_MOCK` 建议默认开启，因为它是 host demo、单元测试和 ReAct loop
验证的基础。

### Runtime Adapters

Runtime adapter 选择平台 glue。通用 core 不应强依赖具体平台头文件。

```kconfig
menu "Runtime adapters"

config CAGENT_RUNTIME_POSIX
    bool "Enable POSIX runtime fallback"
    default y if CAGENT_PRESET_LINUX

config CAGENT_RUNTIME_OPENVELA
    bool "Enable openvela/NuttX runtime adapter"
    default n

config CAGENT_RUNTIME_ESPIDF
    bool "Enable ESP-IDF runtime adapter"
    default n

config CAGENT_RUNTIME_STM32
    bool "Enable STM32/FreeRTOS runtime adapter"
    default n

endmenu
```

平台专属配置可以后续拆到：

```text
platforms/openvela/Kconfig
platforms/espidf/Kconfig
platforms/stm32/Kconfig
```

### Examples And Tests

示例和测试也用 Kconfig/CMake 控制：

```kconfig
menu "Examples and tests"

config CAGENT_EXAMPLE_MOCK_REACT
    bool "Build mock ReAct example"
    default y
    depends on CAGENT_MODEL_MOCK

config CAGENT_TESTS
    bool "Build host/unit tests"
    default n

endmenu
```

### Kconfig 到 C 宏映射

注意：配置值必须进入 C 编译宏，避免 Kconfig 和代码常量脱节。建议规则：

```text
CONFIG_CAGENT_* 是构建系统输入。
CAGENT_* 是 C 代码实际使用的宏。
```

公共/内部头文件里不要到处直接使用 `CONFIG_*`，而是统一桥接：

```c
#ifndef CAGENT_MAX_TOOLS
#ifdef CONFIG_CAGENT_MAX_TOOLS
#define CAGENT_MAX_TOOLS CONFIG_CAGENT_MAX_TOOLS
#else
#define CAGENT_MAX_TOOLS 12
#endif
#endif
```

这样非 Kconfig 构建也可以使用：

```sh
cc -DCAGENT_MAX_TOOLS=8
```

### Standalone CMake 映射

独立 CMake 项目不一定有 Kconfig/autoconf。建议 CMake 也暴露同名 cache 变量：

```cmake
option(CAGENT_MODEL_OPENAI "Enable OpenAI-compatible provider" ON)
set(CAGENT_MAX_TOOLS 12 CACHE STRING "Maximum registered tools")
set(CAGENT_CONTEXT_MAX 4096 CACHE STRING "Context buffer max bytes")

target_compile_definitions(cagent PUBLIC
    CAGENT_MAX_TOOLS=${CAGENT_MAX_TOOLS}
    CAGENT_CONTEXT_MAX=${CAGENT_CONTEXT_MAX}
    CAGENT_MODEL_OPENAI=$<BOOL:${CAGENT_MODEL_OPENAI}>
)
```

openvela/NuttX 构建可以通过 generated config header 提供 `CONFIG_CAGENT_*`，
standalone CMake 则直接提供 `CAGENT_*`。

### Error Codes

```text
AGENT_ERROR_TIMEOUT
AGENT_ERROR_CANCELLED
AGENT_ERROR_CONTEXT_OVERFLOW
AGENT_ERROR_MODEL
AGENT_ERROR_NETWORK
AGENT_ERROR_TOOL
AGENT_ERROR_POLICY_DENIED
```

## MVP 落地顺序

第一步：让项目成为可编译库。

- 填充 `include/cagent/types.h`。
- 填充 `include/cagent/config.h`。
- 填充 `include/cagent/runtime.h`。
- 实现 `src/runtime/runtime.c` 默认 fallback。
- 填充 `include/agent.h` 聚合头。
- 填充 `CMakeLists.txt`。

第二步：实现最小 Agent 上下文。

- `agent_t` opaque type。
- `agent_create()`。
- `agent_destroy()`。
- `agent_cancel()`。
- `agent_reset()`。
- 内部状态结构 `agent_internal.h`。
- limits 默认值。
- timeout/cancel 状态字段。

第三步：实现工具闭环。

- `agent_register_tool()`。
- `agent_unregister_tool()`。
- `agent_tool_set_enabled()`。
- `agent_schema_build()`。
- `agent_tool_execute()`。
- tool guard。

第四步：实现 context 和 skill。

- context provider registry。
- `agent_context_build()`。
- skill registry。
- 默认 skill context provider。

第五步：实现 session。

- append user。
- append assistant。
- append assistant tool_calls。
- append tool result。
- build model messages。
- session 满时按 turn 淘汰。
- 定义完整 turn 边界。
- 实现 `agent_session_clear()` 和 `agent_session_clear_all()`。

第六步：实现 mock ReAct demo。

- mock model 第一次返回 tool_call。
- tool handler 返回 JSON。
- mock model 第二次返回 final reply。
- host example 跑通 `agent_run()`。

第七步：接入 OpenAI-compatible adapter。

- request builder。
- runtime `http_post`。
- response parser。
- 错误归一化。

第八步：建立测试目录。

- `tests/unit/test_tool_registry.c`。
- `tests/unit/test_tool_schema.c`。
- `tests/unit/test_tool_guard.c`。
- `tests/unit/test_context_builder.c`。
- `tests/unit/test_skill_registry.c`。
- `tests/unit/test_session_mgr.c`。
- `tests/unit/test_llm_parse.c`。
- `tests/integration/test_react_mock.c`。

测试框架建议优先考虑 Unity；它轻量、嵌入式友好，也适合 host CI。

## 推荐开发者接入体验

最小接入：

```c
agent_t *agent = agent_create(&config);
agent_set_model(agent, model);
agent_run(agent, &request, &response);
agent_destroy(agent);
```

注册工具：

```c
agent_register_tool(agent, &battery_tool, battery_handler, app);
agent_register_tool(agent, &network_tool, network_handler, app);
```

注册 Skill：

```c
agent_register_skill(agent, &diagnostics_skill);
```

注册动态上下文：

```c
agent_register_context_provider(agent, &device_state_provider);
```

产品接入：

```c
agent_set_limits(agent, &limits);
agent_set_event_callback(agent, on_event, app);
agent_set_policy_callback(agent, on_policy, app);
```

取消同步运行：

```c
/* Called from another task, UI event, disconnect handler, or watchdog path. */
agent_cancel(agent);
```

清理会话：

```c
agent_session_clear(agent, "default");
agent_session_clear_all(agent);
agent_reset(agent);
```

## 与上层应用的关系

推荐拆成两层：

```text
cAGENT
  通用 C Agent core library

agent_service / product app
  运行在 openvela、ESP-IDF、STM32 等平台上的应用层
  负责 channel、worker、tools、skill loader、MCP、UI、voice、业务策略
```

这样 `cAGENT` 保持稳定、可测试、可移植；上层应用仍然可以快速组合复杂能力。
