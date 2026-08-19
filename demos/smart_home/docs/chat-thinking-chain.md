# smart_home Chat 思维链显示

## 背景

`cagent_demo` 终端版通过 `fprintf(stderr, ...)` 在 event callback 中逐事件打印
推理过程，开发者能看清 ReAct 的每一轮 LLM 调用、每个工具的触发和结果、最终回复
以及耗时分布。但 `smart_home` 的 LVGL Chat 界面把这些信息浓缩为一张 trace card，
只展示工具状态列表——LLM 调用轮次、推理延迟、总耗时等信息不可见，开发者调试
多轮 ReAct 时缺少可观测性。

## 目标

把 cAGENT 的 10 个事件映射为一条按时间推进的思维链，嵌入 Chat 屏幕的消息流中。

一次 `smart_home "打开客厅灯，亮度 35%"` 的理想显示效果：

```text
┌─────────────────────────────────┐
│ 用户: 打开客厅灯，亮度 35%        │ ← user bubble
└─────────────────────────────────┘

  ⏳ Thinking...

┌─────────────────────────────────┐
│ Agent trace                     │ ← trace card (可折叠)
├─────────────────────────────────┤
│ Round 1 · LLM   ·  2.3s         │ ← round marker + 延迟
│  ✓ set_light  ·  3ms            │ ← tool entry + 延迟
│  ✓ get_home_status  ·  1ms      │
│ Round 2 · LLM   ·  1.8s         │
├─────────────────────────────────┤
│ 2 rounds · 2 tools · 4.2s       │ ← 摘要行
└─────────────────────────────────┘

┌─────────────────────────────────┐
│ 已为您打开客厅灯，亮度 35%        │ ← assistant bubble
└─────────────────────────────────┘
```

非目标：

- 不做独立的 trace 面板或屏幕分割（圆形 466px 屏空间有限）。
- 不做永久消息历史持久化——trace 条目只存活在本次请求中。
- 不显示 token 消耗（需要 cAGENT model provider 侧扩展，不在本阶段范围）。

## 当前 gap

`ui_event_async_cb` 中对 cAGENT 事件的覆盖情况：

| cAGENT 事件 | 当前处理 | 问题 |
|-------------|---------|------|
| `RUN_START` | **未处理** | 无法记录请求起始时间戳，总耗时无基准 |
| `ITERATION_START` | **未处理** | 迭代边界不可见 |
| `MODEL_REQUEST` | `show_thinking()` | 正确，但未记录时间戳 |
| `MODEL_RESPONSE` | **未处理** | 核心 gap：LLM 轮次和延迟信息完全丢失 |
| `TOOL_CALL` | `trace_tool(name, id, RUNNING)` | 正确，但未记录时间戳 |
| `TOOL_RESULT` | `trace_tool(name, id, OK/FAILED)` | 缺少工具执行延迟 |
| `RUN_DONE` | 仅处理错误情况 | 缺少整体摘要 |
| `CANCELLED` | **未处理** | 取消时无 UI 反馈 |
| `ERROR/TIMEOUT` | `finish_thinking()` + error bubble | 正确 |

**核心 gap：`AGENT_EVENT_MODEL_RESPONSE` 完全没有被处理。** cAGENT 在每次 LLM
调用返回时都发射这个事件（`agent_loop.c:339`），但 smart_home 侧丢弃了它，
导致 trace card 只能看到工具执行，看不到 LLM 调了几次、每次花了多长时间。

次要 gap：`RUN_START` 和 `CANCELLED` 未处理，导致总耗时无法计算、取消时 UI 无反馈。

## 思维链事件映射

一次 2 轮 ReAct 的完整事件序列和 UI 行为：

```
RUN_START                      → 记录 run_start_ms，重置计数器
  ITERATION_START (iter=1)
    MODEL_REQUEST              → show_thinking() + 记录 pending_event_ms
    MODEL_RESPONSE              → trace_round(1, 延迟) + trace_round_count++
      TOOL_CALL set_light      → trace_tool("set_light", RUNNING) + 记录 pending_event_ms
      TOOL_RESULT set_light    → trace_tool("set_light", OK, 延迟毫秒)
      TOOL_CALL get_status     → trace_tool("get_home_status", RUNNING)
      TOOL_RESULT get_status   → trace_tool("get_home_status", OK, 延迟毫秒)
  ITERATION_START (iter=2)
    MODEL_REQUEST              → show_thinking() + 记录 pending_event_ms
    MODEL_RESPONSE (final)     → trace_round(2, 延迟) + trace_round_count++
  RUN_DONE                     → finish_thinking()
                                  trace_summary(轮数, 工具数, 总耗时)
                                  assistant bubble (来自 agent_done_async_cb)
```

异常路径：

```
RUN_START
  ITERATION_START (iter=1)
    MODEL_REQUEST              → show_thinking()
    MODEL_RESPONSE              → trace_round(1, 延迟)
      TOOL_CALL set_light      → trace_tool("set_light", RUNNING)
      TOOL_RESULT set_light    → trace_tool("set_light", FAILED, 延迟)
  CANCELLED                    → finish_thinking() + append_error_bubble("请求已取消")
  RUN_DONE                     → trace_summary (标记为 cancelled)
```

## 数据结构扩展

### 1. `smart_home_lvgl.h` — UI 状态新增字段

```c
typedef struct {
    /* ... existing fields ... */

    /* 思维链时间追踪（每次 RUN_START 时重置） */
    uint64_t run_start_ms;      /* RUN_START 时间戳，用于计算总耗时 */
    uint64_t pending_event_ms;  /* 上一个 MODEL_REQUEST 或 TOOL_CALL 的时间戳 */
    uint32_t trace_round_count; /* 当前请求的 LLM 轮数（MODEL_RESPONSE 时递增） */
} smart_home_lvgl_t;
```

`pending_event_ms` 是一个复用字段：在 `MODEL_REQUEST` 和 `TOOL_CALL` 时被更新为
当前事件时间戳，在 `MODEL_RESPONSE` 和 `TOOL_RESULT` 时计算差值得出延迟。
两个场景不会同时发生——cAGENT 的 ReAct loop 保证同一迭代内
MODEL_REQUEST → MODEL_RESPONSE → TOOL_CALL → TOOL_RESULT 严格串行，
因此 `pending_event_ms` 的写入和读取不存在竞争。

### 2. `smart_home_lvgl_agent.c` — payload 新增字段

当前 `ui_event_payload_t` 缺少 `timestamp_ms`，需要补上：

```c
typedef struct {
    smart_home_lvgl_t *ui;
    agent_event_type_t type;
    uint64_t timestamp_ms;       /* ← 新增：来自 agent_event_t.timestamp_ms */
    uint32_t iteration;
    int error_code;
    char message[128];
    char tool_name[64];
    char tool_call_id[64];
} ui_event_payload_t;
```

`timestamp_ms` 由 `smart_home_lvgl_event_cb` 从 `event->timestamp_ms` 透传，
cAGENT 保证在 `agent_event_emit` 时自动填充（见 `event.h` 注释）。

### 3. `smart_home_lvgl.h` — trace_tool_t 新增延迟字段

```c
typedef struct {
    lv_obj_t *row;
    lv_obj_t *status_label;
    lv_obj_t *delay_label;       /* ← 新增：延迟文本 "3ms" */
    char id[64];
    char name[64];
    int state;                   /* -1 running, 0 failed, 1 ok */
} smart_home_lvgl_trace_tool_t;
```

## ui_event_async_cb 扩展

```c
static void ui_event_async_cb(void *data)
{
    ui_event_payload_t *payload = (ui_event_payload_t *)data;
    smart_home_lvgl_t *ui;

    if (!payload) {
        return;
    }

    ui = payload->ui;
    if (!ui) {
        free(payload);
        return;
    }

    switch (payload->type) {
    case AGENT_EVENT_RUN_START:              /* ← 新增 */
        ui->run_start_ms = payload->timestamp_ms;
        ui->trace_round_count = 0;
        break;

    case AGENT_EVENT_MODEL_REQUEST:
        ui->pending_event_ms = payload->timestamp_ms;
        smart_home_lvgl_show_thinking(ui);
        break;

    case AGENT_EVENT_MODEL_RESPONSE:         /* ← 新增 */
        smart_home_lvgl_trace_round(ui,
            payload->iteration,
            payload->timestamp_ms - ui->pending_event_ms);
        ui->trace_round_count++;
        break;

    case AGENT_EVENT_TOOL_CALL:
        ui->pending_event_ms = payload->timestamp_ms;
        smart_home_lvgl_trace_tool(ui,
            payload->tool_name, payload->tool_call_id, -1, 0);
        break;

    case AGENT_EVENT_TOOL_RESULT: {
        uint32_t tool_ms = (uint32_t)(payload->timestamp_ms -
                                      ui->pending_event_ms);
        int ok = (payload->error_code == AGENT_OK) ? 1 : 0;
        smart_home_lvgl_trace_tool(ui, payload->tool_name,
            payload->tool_call_id, ok, tool_ms);
        if (ok && (strcmp(payload->tool_name, "set_light") == 0 ||
                   strcmp(payload->tool_name, "set_ac") == 0 ||
                   strcmp(payload->tool_name, "run_scene") == 0)) {
            smart_home_lvgl_refresh_cards(ui);
        }
        break;
    }

    case AGENT_EVENT_CANCELLED:              /* ← 新增 */
        smart_home_lvgl_finish_thinking(ui);
        smart_home_lvgl_append_error_bubble(ui, "请求已取消");
        break;

    case AGENT_EVENT_ERROR:
    case AGENT_EVENT_TIMEOUT:
        smart_home_lvgl_finish_thinking(ui);
        smart_home_lvgl_append_error_bubble(ui, payload->message);
        break;

    case AGENT_EVENT_RUN_DONE: {
        uint64_t total_ms = payload->timestamp_ms - ui->run_start_ms;
        smart_home_lvgl_trace_summary(ui,
            ui->trace_round_count,
            ui->chat_trace_tool_count,
            (uint32_t)total_ms);
        smart_home_lvgl_finish_thinking(ui);
        if (payload->error_code != AGENT_OK) {
            smart_home_lvgl_append_error_bubble(ui, payload->message);
        }
        break;
    }

    default:
        break;
    }

    free(payload);
}
```

### 关键设计决策

**为什么 `MODEL_RESPONSE` 后不调用 `finish_thinking()`？**

原文档在 `MODEL_RESPONSE` 分支中调用了 `finish_thinking()`，这是错误的。
在多轮 ReAct 中，`MODEL_RESPONSE` 之后可能还有 `TOOL_CALL` → `TOOL_RESULT` →
下一轮 `MODEL_REQUEST`。`finish_thinking()` 会将所有未完成的工具标记为 OK 并
将 trace card 状态设为 Done，导致后续事件无法正确更新 trace card。

正确的做法是：只在 `RUN_DONE` 时调用 `finish_thinking()`，表示整个请求结束。

**为什么总耗时在 `RUN_DONE` 中计算而非 `agent_done_async_cb`？**

`agent_done_async_cb` 运行在 LVGL 线程，通过 `lv_async_call` 调度，与
`ui_event_async_cb` 之间可能存在时序交错。`RUN_DONE` 事件携带
`timestamp_ms`，与 `RUN_START` 的时间戳同源（均由 cAGENT 在 agent_run
线程中填充），计算出的总耗时更准确。而 `agent_done_async_cb` 中没有时间戳信息。

## smart_home_lvgl_event_cb 扩展

在事件回调中透传 `timestamp_ms`：

```c
void smart_home_lvgl_event_cb(const agent_event_t *event, void *user_data)
{
    /* ... existing payload allocation ... */

    payload->timestamp_ms = event->timestamp_ms;   /* ← 新增 */

    /* ... existing field copies ... */
}
```

## trace card 视觉布局

每行从单一工具状态扩展为"round 标记 + 工具 + 延迟"的三元模型：

```
┌─────────────────────────────────┐
│ Agent trace          ● Running  │ ← header (可折叠, 点击切换)
├─────────────────────────────────┤
│ Round 1 · LLM   ·  2.3s         │ ← trace_round 创建
│  ✓ set_light         3ms        │ ← trace_tool 创建/更新
│  ✓ get_home_status   1ms        │
│ Round 2 · LLM   ·  1.8s         │
├─────────────────────────────────┤
│ 2 rounds · 2 tools · 4.2s       │ ← trace_summary 创建
└─────────────────────────────────┘
```

实现方式：

- `trace_round()` 在 trace body 中创建新的 label 行，内容 `"Round N · LLM · X.Xs"`
  - 延迟 < 1000ms 时显示 `"Nms"`（如 `3ms`）
  - 延迟 >= 1000ms 时显示 `"X.Xs"`（如 `2.3s`），保留一位小数
- `trace_tool()` 在已有条目中更新状态和延迟：
  - 状态为 running 时延迟显示为 `"..."`（等待 TOOL_RESULT）
  - 状态为 ok/failed 时更新为实际延迟（如 `"3ms"`）
- `trace_summary()` 在 trace body 末尾创建分隔行，内容 `"N rounds · M tools · X.Xs"`
  - 仅在 `RUN_DONE` 时调用一次

## agent_done_async_cb 调整

`agent_done_async_cb` 不再负责调用 `trace_summary`（已移至 `RUN_DONE` 事件处理），
也不再调用 `finish_thinking`（同样已移至 `RUN_DONE`）。它只负责显示最终的
assistant bubble 或 error bubble：

```c
static void agent_done_async_cb(void *data)
{
    agent_done_t *done = (agent_done_t *)data;
    smart_home_lvgl_t *ui = done ? done->ui : NULL;

    if (!done) {
        return;
    }

    if (ui) {
        ui->request_inflight = 0;

        if (done->ret == AGENT_OK) {
            smart_home_lvgl_append_msg_bubble(ui, done->output, 0);
            smart_home_lvgl_refresh_cards(ui);
        } else {
            char message[160];
            snprintf(message,
                     sizeof(message),
                     "Agent call failed (%d): %.96s",
                     done->ret,
                     done->output[0] ? done->output : "no detail");
            smart_home_lvgl_append_error_bubble(ui, message);
        }

        if (ui->chat_status) {
            lv_label_set_text(ui->chat_status,
                              "Done.  (NSH: smart_home \"...\")");
        }
    }

    free(done);
}
```

## 函数签名变更

| 函数 | 原签名 | 新签名 | 说明 |
|------|--------|--------|------|
| `smart_home_lvgl_trace_tool` | `(ui, name, call_id, ok)` | `(ui, name, call_id, ok, delay_ms)` | 新增 `delay_ms` 参数；running 时传 0 |
| `smart_home_lvgl_trace_round` | 不存在 | `(ui, iteration, delay_ms)` | 新增 |
| `smart_home_lvgl_trace_summary` | 不存在 | `(ui, rounds, tools, total_ms)` | 新增 |

`trace_tool` 签名变更影响范围：
- `smart_home_lvgl_agent.c` — `ui_event_async_cb` 中的 2 处调用
- `smart_home_lvgl_chat.c` — 函数定义
- `smart_home_lvgl_internal.h` — 函数声明

## 策略边界

- trace card 只展示当前请求。`ensure_trace_card` 调用时（新请求开始时在
  `show_thinking` 中触发）重置 `chat_trace_tools[]` 和 `trace_round_count`。
- 延迟语义：
  - LLM 延迟 = `MODEL_RESPONSE.timestamp_ms - MODEL_REQUEST.timestamp_ms`
    （网络往返 + 推理时间）
  - 工具延迟 = `TOOL_RESULT.timestamp_ms - TOOL_CALL.timestamp_ms`
    （本地执行时间）
  - 总耗时 = `RUN_DONE.timestamp_ms - RUN_START.timestamp_ms`
    （从请求开始到结束的端到端时间）
- trace card 折叠：点击 header 行切换 body 显示/隐藏。当前已有基础实现
  （`trace_header_event_cb`）。
- `ITERATION_START` 事件暂不在 UI 中单独展示——迭代编号已通过 `trace_round`
  的 `iteration` 参数体现。如果未来需要展示迭代间的非 LLM 步骤，可扩展。

## 代码改动范围

| 文件 | 改动 | 行数 |
|------|------|------|
| `smart_home_lvgl.h` | 新增 `run_start_ms` + `pending_event_ms` + `trace_round_count` 字段；`trace_tool_t` 新增 `delay_label` | +4 |
| `smart_home_lvgl_agent.c` | `ui_event_payload_t` 新增 `timestamp_ms`；`smart_home_lvgl_event_cb` 透传 `timestamp_ms`；`ui_event_async_cb` 加 `RUN_START` / `MODEL_RESPONSE` / `CANCELLED` 分支 + 工具延迟 + 摘要；`agent_done_async_cb` 移除 `finish_thinking` 调用 | +30 |
| `smart_home_lvgl_chat.c` | `trace_round()` + `trace_summary()` 新函数；`trace_tool()` 签名变更 + 延迟显示 | +35 |
| `smart_home_lvgl_internal.h` | `trace_tool` 签名变更声明；`trace_round` + `trace_summary` 声明 | +3 |

总计 ~72 行。修改 1 个现有函数签名（`trace_tool`），新增 2 个函数。不依赖 cAGENT 框架层改动。

## 验收标准

- 用户输入后 Chat 页出现 `Thinking...` 状态指示。
- 每个 LLM round 在 trace card 中有独立的 marker 行，包含 round 编号和延迟。
- 每个工具在 trace card 中有独立的行，包含名称、状态符号和延迟。
- trace card 底部显示本轮摘要：round 数、工具调用数、总耗时。
- 下一轮请求开始时 trace card 自动清空上一轮的条目。
- 请求被取消时显示 "请求已取消" 错误气泡。
- LLM 延迟与 NSH event trace 中观察到的耗时一致（通过对比验证）。
- 多轮 ReAct（2+ 轮）场景下 trace card 正确显示所有 round 和 tool 行，不会提前结束。
