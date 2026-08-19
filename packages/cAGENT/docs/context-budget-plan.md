# cAGENT Context Budget Plan

## 背景

cAGENT 面向嵌入式平台，所有运行时资源都必须有明确上限。当前实现已经对
context、tool schema、session message、tool input/output 和 model output
设置了固定大小限制，能避免无界内存增长。

但在多轮 ReAct 对话中，session 历史、assistant tool calls、tool result、
skills、context providers 和 tool schema 会共同进入模型请求。当序列化后的
`messages_json` 或 OpenAI-compatible HTTP request 超过
`CAGENT_CONTEXT_BUFFER_SIZE` 时，当前逻辑会直接返回
`AGENT_ERROR_CONTEXT_OVERFLOW`。如果失败发生在本轮 user message 已经写入
session 之后，还可能留下未完成 turn，导致下一轮请求继续返回
`AGENT_ERROR_INVALID`。

smart_home demo 中出现的典型现象：

```text
第 1 轮：正常
第 2 轮：正常
第 3 轮：AGENT_ERROR_CONTEXT_OVERFLOW (-8)
第 4 轮：AGENT_ERROR_INVALID (-3)
```

这说明 cAGENT 当前有边界保护，但缺少上下文预算超限后的自动恢复策略。

## 当前机制

已有保护：

- `CAGENT_CONTEXT_BUFFER_SIZE` 限制 context、tools、messages buffer。
- `CAGENT_REQUEST_ARENA_SIZE` 限制单次 run 的临时 arena，默认由
  system/context、tool schema、messages 三块 buffer 自动推导。
- `CAGENT_MAX_SESSION_MESSAGES` 限制 session message 数量。
- `CAGENT_SESSION_CONTENT_MAX_SIZE` 限制单条 session 内容。
- `CAGENT_SESSION_TOOL_CALL_MAX_SIZE` 限制 tool call arguments。
- `CAGENT_SESSION_MAX_TOOL_CALLS` 限制单条 assistant tool_calls 数量。
- `CAGENT_TOOL_ARGS_MAX_SIZE` 限制工具入参。
- `CAGENT_TOOL_OUTPUT_MAX_SIZE` 限制工具输出。
- session 存储空间不足时可淘汰最早完整 turn。

主要缺口：

- context/messages 序列化超过 buffer 时不会自动淘汰旧 turn 后重试。
- run 失败后不会回滚本次刚追加的未完成 turn。
- context、tool schema、messages、HTTP request 共用同一尺寸配置，问题定位和调优
  粒度较粗。
- stats/event 没有暴露各类 buffer 的 used/size、淘汰次数和失败原因细节。

## 目标

目标不是取消固定上限，而是在固定上限内提供可预测的恢复策略：

```text
bounded resource
  -> detect overflow
  -> evict or trim according to policy
  -> retry build
  -> rollback failed turn if unrecoverable
  -> report actionable diagnostics
```

设计原则：

- 默认行为必须适合嵌入式设备。
- 不静默截断 critical context。
- 不让失败 run 污染 session。
- 允许应用选择 fail-fast、evict、clear 或 keep-recent 策略。
- 预算管理优先发生在 cAGENT 库内部，而不是要求每个 demo 自行补救。

## 阶段一：最小自恢复

阶段一目标是解决 `-8` 后继续 `-3` 的问题，尽量少改公共 API。

### 1. build messages/context 失败时淘汰并重试

在 `agent_loop_run()` 中，以下构建步骤可能因 buffer 不足失败：

```c
agent_context_build(...)
agent_tool_schema_build(...)
agent_session_build_model_messages(...)
```

其中最常见的是 `agent_session_build_model_messages()` 超限。建议先针对
messages 构建加入重试：

```text
build messages
  -> OK: continue
  -> LIMIT/CONTEXT_OVERFLOW:
       evict oldest complete turn
       retry build messages
       repeat until success or no complete turn
```

建议限制重试次数，避免异常状态下长时间循环：

```text
max_retry = CAGENT_MAX_SESSION_MESSAGES
```

### 2. 失败时回滚本次 unfinished turn

`agent_loop_run()` 进入后会先追加 user message。需要记录本次 run 开始前的
session 状态：

```c
uint32_t session_start_count = session->count;
uint32_t session_start_complete_turns = session->complete_turns;
```

如果 run 最终失败，并且本轮没有形成 assistant final，则回滚到起点：

```text
ret != AGENT_OK
  -> restore session count
  -> clear tail entries
  -> restore complete_turns
```

这样可以避免：

```text
user appended
context overflow
no assistant final
next run sees unfinished turn
return invalid
```

### 3. 保留现有错误语义

如果淘汰后仍失败，仍返回原始错误：

- `AGENT_ERROR_CONTEXT_OVERFLOW`
- `AGENT_ERROR_LIMIT`

但 session 应保持可继续使用。

### 4. 阶段一验收

测试场景：

- 连续 5-10 轮 smart_home tool calling，不应出现 `-8` 后紧接 `-3`。
- 人为调小 `CAGENT_CONTEXT_BUFFER_SIZE`，应触发 evict retry。
- 无完整 turn 可淘汰时，应返回 `-8/-5`，但下一轮仍可继续正常请求。
- tool_calls 已写入但 tool result/final assistant 未完成时，失败后 session 不残留
  pending tool calls。

阶段一推荐改动文件：

```text
src/core/agent_loop.c
src/memory/session_mgr.c
src/memory/memory_internal.h
include/cagent/session.h   (如需要公开 debug/trim API，可延后到阶段二)
```

## 阶段二：API 化预算策略

阶段二目标是把阶段一的固定策略扩展为可配置策略，并提供应用可调用的 session
管理能力。

### 1. 新增 context budget policy

建议新增策略枚举：

```c
typedef enum {
    AGENT_CONTEXT_BUDGET_FAIL_FAST = 0,
    AGENT_CONTEXT_BUDGET_EVICT_OLDEST,
    AGENT_CONTEXT_BUDGET_CLEAR_SESSION,
    AGENT_CONTEXT_BUDGET_KEEP_RECENT
} agent_context_budget_policy_t;
```

语义：

- `FAIL_FAST`: 保持当前风格，超限直接返回错误。
- `EVICT_OLDEST`: 淘汰最早完整 turn 后重试。
- `CLEAR_SESSION`: 超限时清空当前 session 后重试当前输入。
- `KEEP_RECENT`: 只保留最近 N 个完整 turn，再重试。

默认建议：

```text
embedded demo: EVICT_OLDEST 或 KEEP_RECENT
严格审计场景: FAIL_FAST
一次性命令场景: CLEAR_SESSION
```

### 2. 新增公共 API

建议增加：

```c
int agent_set_context_budget_policy(agent_t *agent,
                                    agent_context_budget_policy_t policy);

int agent_session_trim(agent_t *agent,
                       const char *session_id,
                       uint32_t max_complete_turns);

int agent_session_trim_to_budget(agent_t *agent,
                                 const char *session_id,
                                 size_t max_serialized_bytes);

int agent_session_clear_incomplete_turn(agent_t *agent,
                                        const char *session_id);
```

其中 `trim_to_budget` 可以先实现为估算型，不要求第一版精确 token 计算。

### 3. 扩展 stats

建议在 `agent_stats_t` 中增加：

```c
uint32_t context_overflows;
uint32_t session_evictions;
uint32_t failed_turn_rollbacks;
uint32_t last_context_used;
uint32_t last_context_size;
uint32_t last_messages_used;
uint32_t last_messages_size;
uint32_t last_tools_used;
uint32_t last_tools_size;
```

### 4. 扩展事件

当前 event 只有 `message` 和 `error_code`，不够定位预算问题。建议增加新的事件类型
或复用现有 error 事件的 message：

```text
AGENT_EVENT_CONTEXT_BUDGET
```

事件 message 可包含简短结构化文本：

```text
messages overflow used=4312 size=4096 evicted=1 retry=1
```

阶段二推荐改动文件：

```text
include/cagent/types.h
include/cagent/session.h
include/cagent/event.h
src/core/agent_core.c
src/core/agent_loop.c
src/memory/session_mgr.c
docs/api_reference.md
```

### 5. 阶段二验收

- 应用可选择 `FAIL_FAST` 并观察到不淘汰。
- 应用可选择 `EVICT_OLDEST` 并观察到自动恢复。
- 应用可调用 `agent_session_trim(..., 2)` 只保留最近两轮。
- stats 能显示最近一次 messages/context/tools 的 used/size。
- event callback 能看到预算淘汰日志。

## 阶段三：精细预算与预裁剪

阶段三目标是提升调优精度，并减少“构建失败后再补救”的次数。

### 1. 拆分 buffer 配置

当前所有构建 buffer 共用：

```text
CAGENT_CONTEXT_BUFFER_SIZE
```

建议拆分为：

```text
CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE
CAGENT_TOOL_SCHEMA_BUFFER_SIZE
CAGENT_MESSAGES_BUFFER_SIZE
CAGENT_HTTP_REQUEST_BUFFER_SIZE
```

默认可保持向后兼容：

```c
#ifndef CAGENT_MESSAGES_BUFFER_SIZE
#define CAGENT_MESSAGES_BUFFER_SIZE CAGENT_CONTEXT_BUFFER_SIZE
#endif
```

这样 smart_home 这类问题可以单独增大 messages/request，而不必增大 tool schema 或
context provider buffer。

### 2. 构建前预估 session serialized size

为 session entry 增加或计算序列化估算值：

```text
role overhead
content escaped length
tool_call id/name/arguments overhead
tool result overhead
JSON delimiters
```

在 build 前先根据 budget 选择可保留的完整 turns：

```text
start from newest turn
accumulate estimated size
stop before exceeding budget
build only selected window
```

这比“build 失败后 evict retry”更稳定，尤其适合低性能 MCU。

### 3. 支持 history window

新增配置：

```c
uint32_t max_history_turns;
size_t max_history_bytes;
```

语义：

- `max_history_turns = 0` 表示不按轮数限制。
- `max_history_bytes = 0` 表示不按历史字节限制。
- 两者都设置时取更严格者。

smart_home 推荐：

```text
max_history_turns = 2
max_history_bytes = 2048
```

设备状态由 context provider 提供，不依赖长历史。

### 4. 摘要扩展点

后续可以增加摘要回调，但不建议阶段三一开始就绑定模型总结：

```c
typedef int (*agent_session_summarize_cb_t)(
    agent_t *agent,
    const agent_session_entry_t *entries,
    size_t entry_count,
    char *summary,
    size_t summary_size,
    void *user_data);
```

第一版可只提供应用侧 hook。是否调用模型做摘要由应用决定，cAGENT core 不默认产生
额外模型调用。

### 5. 阶段三验收

- 可以单独配置 messages/request buffer 大小。
- 连续多轮 tool calling 时，构建前即可裁剪历史窗口。
- stats 能区分 context/tools/messages/request 的 peak。
- 在小 buffer 配置下，系统仍能按 policy 保持可用。
- 不因自动摘要引入隐藏模型调用或不可预测延迟。

## 推荐落地顺序

优先级从高到低：

```text
P0 阶段一：evict retry + failed turn rollback
P1 阶段二：policy API + trim API + stats/event
P2 阶段三：拆分 buffer + pre-trim + summary hook
```

smart_home 当前问题只需要阶段一即可明显改善。阶段二让 demo 能主动设置策略。阶段三
是把 cAGENT 做成更稳的嵌入式 Agent runtime。

## 实现状态

当前已落地阶段一的核心恢复能力：

- `agent_session_build_model_messages()` 因预算不足失败时，`agent_loop_run()` 会淘汰
  最早完整 turn 并重试。
- OpenAI-compatible provider 在 request 组包阶段返回预算错误时，`agent_loop_run()`
  也会淘汰最早完整 turn 并重试当前 iteration。
- `agent_run()` 失败返回前会删除尾部未完成 turn，避免本次失败污染后续请求。
- 如果进入新 run 时发现旧版本遗留的 unfinished turn，会先清理尾部未完成 turn，再
  重试追加当前 user message。

阶段二仍是后续规划，尚未引入新的公共 policy API 或 stats/event 扩展。阶段三的
拆分 buffer 配置已先落地第一步，预裁剪和 summary hook 仍未实现。

阶段三第一步已落地拆分 buffer 编译配置，并保持 `CAGENT_CONTEXT_BUFFER_SIZE`
向后兼容。当前可分别配置：

```text
CONFIG_CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE
CONFIG_CAGENT_TOOL_SCHEMA_BUFFER_SIZE
CONFIG_CAGENT_MESSAGES_BUFFER_SIZE
CONFIG_CAGENT_HTTP_REQUEST_BUFFER_SIZE
CONFIG_CAGENT_HTTP_RESPONSE_BUFFER_SIZE
```

未显式配置时，这些值默认继承 `CONFIG_CAGENT_CONTEXT_BUFFER_SIZE`。
`CAGENT_REQUEST_ARENA_SIZE` 由前三者之和自动计算：

```text
SYSTEM_CONTEXT + TOOL_SCHEMA + MESSAGES
```

如果平台需要额外预留 arena 空间，可配置
`CONFIG_CAGENT_REQUEST_ARENA_EXTRA_SIZE`，它会追加到自动计算结果上。

OpenAI-compatible provider 的 `request_buffer_size` / `response_buffer_size`
默认分别使用 `CAGENT_HTTP_REQUEST_BUFFER_SIZE` 和
`CAGENT_HTTP_RESPONSE_BUFFER_SIZE`，上层 demo 仍可在创建 model 时进一步覆盖。

## 风险与注意事项

- 自动淘汰会丢失对话历史，必须通过 event/stats 可观测。
- 不应淘汰未完成 turn，除非是在失败回滚本次 run。
- critical context 仍然不能静默丢弃。
- tool schema 超限不能靠淘汰历史解决，需要减少工具数量、压缩 schema 或增大
  `CAGENT_TOOL_SCHEMA_BUFFER_SIZE`。
- HTTP request buffer 超限可能发生在 context/messages/tools 都分别构建成功之后，
  阶段三需要单独统计和处理。
- 对安全敏感设备，默认策略可能需要 `FAIL_FAST`，由产品层决定是否启用自动淘汰。

## smart_home 建议配置

短期建议：

```text
CONFIG_CAGENT_CONTEXT_BUFFER_SIZE=8192
```

拆分 buffer 后，更推荐针对 smart_home 使用：

```text
CONFIG_CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE=4096
CONFIG_CAGENT_TOOL_SCHEMA_BUFFER_SIZE=8192
CONFIG_CAGENT_MESSAGES_BUFFER_SIZE=4096
CONFIG_CAGENT_HTTP_REQUEST_BUFFER_SIZE=12288
CONFIG_CAGENT_HTTP_RESPONSE_BUFFER_SIZE=8192
CONFIG_CAGENT_REQUEST_ARENA_EXTRA_SIZE=0
```

这比把所有 buffer 都调成 8192 更节省内存，也更符合 smart_home 的实际压力：
工具 schema 和 HTTP request 更大，system context/messages 暂时不需要同步放大。

如果继续保留较多工具调用历史：

```text
CONFIG_CAGENT_CONTEXT_BUFFER_SIZE=12288
CONFIG_CAGENT_REQUEST_ARENA_EXTRA_SIZE=0
```

库能力补齐后，smart_home 更推荐：

```text
policy = AGENT_CONTEXT_BUDGET_KEEP_RECENT
max_history_turns = 2
```

因为智能家居的真实状态应来自 device context provider，而不是依赖长对话历史。
