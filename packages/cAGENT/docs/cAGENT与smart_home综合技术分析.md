# cAGENT 与 smart_home 综合技术分析文档

> 版本：1.0
> 日期：2026-06-20
> 文档定位：集项目分析、C语言技术要点、嵌入式实践、面试准备于一体的综合技术文档

---

## 目录

1. [项目概述](#1-项目概述)
2. [代码逻辑深度分析](#2-代码逻辑深度分析)
3. [C 语言技术要点](#3-c-语言技术要点)
4. [嵌入式系统实现](#4-嵌入式系统实现)
5. [常见面试题整理](#5-常见面试题整理)
6. [技术难点与解决方案](#6-技术难点与解决方案)
7. [学习路径建议](#7-学习路径建议)

---

## 1. 项目概述

### 1.1 cAGENT — 嵌入式 AI Agent 推理框架

**定位**：面向 MCU/RTOS 平台的轻量级 C Agent 推理核心库。让嵌入式设备具备 LLM（大语言模型）驱动的 Reasoning + Acting（ReAct）能力。

**核心特性**：

| 特性 | 说明 |
|---|---|
| 纯 C 实现 | 无 C++ 依赖，C99 标准，适配 bare-metal / RTOS |
| 平台抽象层 | 运行时接口支持 openvela/NuttX、ESP-IDF/FreeRTOS、STM32 |
| ReAct 循环 | 完整的 推理→工具调用→结果反馈→继续推理 循环 |
| 工具系统 | 可注册工具，LLM 通过 tool_calling 协议调用 |
| Session 管理 | 有界内存的对话历史，按完整 turn 淘汰 |
| 模型抽象 | vtable 设计，支持 OpenAI 兼容 API 和 Mock 测试 |
| 事件系统 | 10 类事件，应用层可注册回调观察推理过程 |
| INT8 量化 | 上下文/工具 schema 缓存，dirty 标记减少重复计算 |

**技术栈**：

```text
应用层（smart_home）
    ↓
┌─────────────── cAGENT Public API ───────────────┐
│ agent.h / config.h / model.h / tool.h / ...     │
├─────────────────────────────────────────────────┤
│ Core:  agent_core(生命周期) + agent_loop(ReAct循环)  │
│        + context_builder(上下文组装)              │
│                                                     │
│ LLM:   model(外观) + model_openai(HTTP+TLS)         │
│        + model_mock(测试) + llm_parse(响应解析)      │
│                                                     │
│ Tools: tool_registry(注册) + tool_schema(JSON构建)   │
│        + tool_guard(策略/超时/大小守卫)             │
│                                                     │
│ Skills: skill_registry(上下文注入)                  │
│                                                     │
│ Memory: session_mgr(有界对话历史)                   │
│                                                     │
│ Runtime: 平台抽象层(POSIX/openvela/ESP-IDF/STM32)   │
└─────────────────────────────────────────────────┘
```

**规模统计**：~8500 行 C 代码，18 个源文件，12 个公共头文件，6 份设计文档。

---

### 1.2 smart_home — 基于 cAGENT 的智能家居 Demo

**定位**：展示 cAGENT 在嵌入式 RTOS 上构建自然语言交互应用的完整范例。

**核心特性**：

| 特性 | 说明 |
|---|---|
| LLM 驱动的家居控制 | 自然语言 → LLM 理解意图 → 调用工具 → 控制虚拟设备 |
| ReAct 可视化 | LVGL 界面实时显示思考链：模型请求 → 工具调用 → 结果 → 最终回复 |
| 多后端切换 | DeepSeek / Kimi / Qwen / OpenAI，运行时切换 |
| LVGL UI | 3 屏架构（面板/聊天/设置），支持触控交互 |
| 虚拟设备模拟 | Light + AC，含完整的状态管理和场景控制 |
| Skill 系统 | Markdown 策略文件注入 LLM 上下文 |
| 工具-模型双线程 | Agent 在 pthread 工作线程运行，UI 在主线程，通过 lv_async_call 通信 |
| 控制台 + LVGL 双模式 | `CONSOLE` UI 用于 NSH 命令行，`LVGL` UI 用于嵌入式触摸屏 |

**架构分层**：

```text
┌────────────────────────────────────────────────────────────┐
│  main（入口）                                               │
│    ├── console 交互式 REPL / NSH 单次命令                   │
│    └── LVGL 3 屏 GUI                                       │
├────────────────────────────────────────────────────────────┤
│  agent（cAGENT 编排层）                                     │
│    smart_home_agent.c — 组装 agent + model + tools + skills │
├────────────┬────────────┬──────────────┬───────────────────┤
│  device/   │  tools/    │  skills/     │  config/          │
│  虚拟设备   │  LLM 工具   │  策略文件     │  模型后端预设       │
│  Light+AC  │  8 个工具   │  5 个 .md     │  5 个 preset      │
├────────────┴────────────┴──────────────┴───────────────────┤
│  ui/                                                        │
│    控制台 UI: 逐事件打印 + REPL                              │
│    LVGL UI: 面板 / 聊天 / 设置                               │
└────────────────────────────────────────────────────────────┘
```

**规模统计**：~4000 行 C 代码，25 个源文件，7 份设计文档，25 个 PNG 图标。

---

### 1.3 两个项目的关系

```text
cAGENT（框架）         smart_home（应用）
─────────────         ───────────────────
agent_create()   →    smart_home_agent_app_init()
agent_run()      →    smart_home_agent_run()
register_tool()  →    smart_home_tools_register()  (8 个工具)
register_skill() →    smart_home_skills_register() (5 个策略)
set_event_cb()   →    smart_home_ui_event_cb()
runtime          →    openvela runtime (pthread + mbedTLS)
model_openai     →    DeepSeek/Kimi/Qwen/OpenAI backend
```

smart_home 是 cAGENT 的**完整使用示例**，展示了所有主要 API 的调用方式、错误处理模式、多线程集成策略。

---

## 2. 代码逻辑深度分析

### 2.1 cAGENT 核心：ReAct 推理循环

**文件**：[agent_loop.c](agent_loop.c)（457 行）

这是整个框架的**心脏**。ReAct（Reasoning + Acting）循环是 LLM Agent 的标准范式。

**算法流程**：

```
agent_run(request, response)
│
├─ 1. mutex_lock() → busy 检查（防重入）
├─ 2. 保存 limits → 允许 request 临时覆盖
├─ 3. emit RUN_START
│
└─ agent_loop_run()
   │
   ├─ arena_begin()   — 分配临时的线性内存池
   ├─ session.add_user(message)
   │
   └─ for iter = 0; iter < max_steps; iter++:
      │
      ├─ check_run_state()   — cancel/deadline 检查
      ├─ emit ITERATION_START
      │
      ├─ [budget retry loop]  — 上下文溢出时自动淘汰+重试
      │  ├─ context_build()     — 组装 system prompt + providers + skills
      │  ├─ tool_schema_build() — 构建 OpenAI tools JSON（缓存+dirty标记）
      │  ├─ session_build_messages() — 构建 messages JSON
      │  ├─ model.complete()    — HTTP POST → LLM API
      │  └─ 如果 budget 溢出 → evict_oldest_turn() → 重试
      │
      ├─ parse_response()
      │
      ├─ 如果是 final content:
      │  ├─ session.add_assistant(content)
      │  ├─ copy_final_output(response)
      │  └─ return AGENT_OK
      │
      └─ 如果是 tool_calls:
         ├─ session.add_assistant_tool_calls(...)
         └─ for each tool_call:
            ├─ emit TOOL_CALL
            ├─ tool_execute()  — 策略检查 + 超时守卫 + 执行
            ├─ emit TOOL_RESULT
            └─ session.add_tool(call_id, result)
         └─ continue  ← 下一轮迭代，LLM 会看到工具结果

   → 超过 max_steps → 返回 AGENT_ERROR_LIMIT
```

**关键设计决策**：

1. **同步模型**：`agent_run()` 是阻塞的。虽然牺牲了灵活性，但极大简化了应用层代码——没有回调地狱，没有状态机。

2. **Budget Retry Loop**（[agent_loop.c:278-353]）：当上下文组装或模型调用因溢出失败时，自动淘汰最早的完整 turn 然后重试。这保证了长时间对话的鲁棒性。

3. **Arena 分配器**（[agent_loop.c:113-177]）：每次 run 分配一次性的线性内存池。所有 run 内的临时缓冲区（context、tools_json、messages_json）从 arena 分配，run 结束时一次性释放。这消除了复杂的内存管理，避免了碎片化。

4. **协作式取消**（[agent_core.c:236-251]）：通过 `critical_section` 安全设置 `cancel_requested` 标志。取消不是立即生效的——实际检查点分布在循环的多个位置，保证不会在关键操作中断。

---

### 2.2 Session 管理器：有界对话历史与 Turn 淘汰

**文件**：[session_mgr.c](session_mgr.c)（1039 行）

这是 cAGENT 中**最复杂**的模块。对话历史必须在严格遵守 OpenAI tool_calling 消息链约束的同时，适应嵌入式设备有限的内存。

**核心数据结构**：

```c
// Session 中的一条消息
typedef struct {
    agent_message_role_t role;           // USER / ASSISTANT / TOOL
    char content[512];                   // 消息内容（内拷贝）
    uint32_t content_len;
    char tool_call_id[64];              // 仅 TOOL 角色使用
    tool_calls[CAGENT_SESSION_MAX_TOOL_CALLS]; // ASSISTANT tool_calls
    uint32_t tool_call_count;
    uint64_t timestamp_ms;
    bool turn_boundary;                 // 标记 turn 起点（user 消息）
} agent_session_entry_t;

// 一个 session（对话）
typedef struct {
    char id[48];
    agent_session_entry_t entries[CAGENT_MAX_SESSION_MESSAGES];
    uint32_t count;
    uint32_t complete_turns;
} agent_session_t;
```

**关键不变量（Invariant）**：

```text
消息链完整性约束：
  user(turn_boundary=true)
    → assistant(tool_calls=[...])    ← 可选，可以有多个
      → tool(call_id=xxx)            ← 必须一一对应
    → assistant(tool_call_count=0)   ← final 回复，标记 turn 结束
```

**Turn 淘汰算法**（[session_mgr.c:754-819]）：

```
问题：消息缓冲区满了，需要释放空间。但不能破坏消息链完整性。

方案：按完整 turn 淘汰。

算法：
  1. 从头部扫描，找到第一个 turn_boundary（user message 起点）
  2. 从该位置继续扫描，找到对应的 assistant final（tool_call_count=0）
  3. 如果找到完整 turn → memmove 删除 [turn_start, turn_end]
  4. 如果找不到完整 turn（只有 user 没有 final）→ 返回 AGENT_ERROR_LIMIT
  5. 尾部未完成的 turn 不参与淘汰
```

```c
// 核心代码片段
uint32_t remove_count = turn_end - turn_start + 1u;
uint32_t remaining = session->count - turn_start - remove_count;
if (remaining > 0u) {
    memmove(&session->entries[turn_start],
            &session->entries[turn_end + 1u],
            remaining * sizeof(agent_session_entry_t));
}
session->count -= remove_count;
```

**尾部回滚**（[session_mgr.c:828-868]）：

`agent_session_drop_unfinished_tail()` 在 run 失败时回滚未完成的 turn。例如：user 消息已追加 → tool_calls 已追加 → 工具执行失败 → 回滚整个未完成 turn。已完成的历史 turn 保留。

---

### 2.3 OpenAI 模型 Provider

**文件**：[model_openai.c](model_openai.c)（928 行，cAGENT 最大单文件）

实现了与 OpenAI 兼容 API（Chat Completions）的完整交互。

**HTTP 请求构建**：

```text
POST /v1/chat/completions HTTP/1.1\r\n
Host: api.deepseek.com\r\n
Content-Type: application/json\r\n
Authorization: Bearer sk-xxx\r\n
\r\n
{
  "model": "deepseek-v4-flash",
  "messages": [{"role":"system","content":"..."},
               {"role":"user","content":"..."}],
  "tools": [{"type":"function","function":{"name":"...","parameters":{...}}}],
  "stream": false,
  "max_tokens": 1024
}
```

**JSON 响应解析**（递归下降）：

```text
响应 JSON:
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": "..." | null,
      "tool_calls": [{
        "id": "call_xxx",
        "type": "function",
        "function": {"name": "get_weather", "arguments": "{...}"}
      }]
    }
  }]
}

解析器：
  parse_response()
    → find_key("choices") → find_key("message")
      → 如果 content 存在 → content 字符串提取（支持转义）
      → 如果 tool_calls 存在 → 逐个解析 id/name/arguments
```

**Magic Number 校验**：

每个 model_openai 实例在创建时写入 `0x4f414943u`（"OAIC"）作为 magic number，API 调用时校验——防止野指针或已释放的内存在 provider 间传递。

---

### 2.4 请求 Arena 分配器

**文件**：[agent_loop.c:113-177]

```c
// 结构
typedef struct {
    unsigned char *buffer;   // 底层内存
    size_t size;            // 总大小
    size_t used;            // 已用（分配位置）
    size_t peak;            // 峰值
} agent_request_arena_t;

// 分配算法（线性 + 对齐）
static void *arena_alloc(agent_t *agent, size_t size) {
    aligned_used = align_up_size(agent->request_arena.used);
    if (size > agent->request_arena.size - aligned_used) return NULL;
    next_used = aligned_used + size;
    ptr = buffer + aligned_used;
    agent->request_arena.used = next_used;
    if (next_used > peak) peak = next_used;
    memset(ptr, 0, size);      // 零初始化
    return ptr;
}
```

**为什么不用 malloc/free？**

1. **确定性**：线性分配 O(1)，不会触发碎片回收
2. **无泄漏风险**：arena_end 一次性释放，不依赖逐个 free
3. **峰值追踪**：peak 字段记录最大用量，方便调优
4. **符合嵌入式约束**：与外部的静态 arena 模式一致

---

### 2.5 smart_home 工具系统

**文件**：[smart_home_tools.c](smart_home_tools.c)（773 行）

smart_home 通过 8 个工具将 LLM 与虚拟家居设备连接起来。每个工具有三个要素：

1. **JSON Schema**（编译时常量字符串）：定义 LLM 可调用的参数
2. **Handler 函数**：`int handler(const agent_tool_call_t *call, agent_tool_result_t *result, void *user_data)`
3. **注册信息**：名称、描述、flags（LLM_VISIBLE、READ_ONLY）

**工具列表**：

| 工具 | 类型 | 核心逻辑 |
|---|---|---|
| `get_home_status` | 只读 | 遍历设备数组，构建完整状态 JSON |
| `get_weather` | 只读 | DJB2 哈希模拟天气（3 城市固定 + 随机生成） |
| `set_light` | 写入 | 查找设备 → clamp 参数 → 设置 on/brightness |
| `set_ac` | 写入 | 查找设备 → 可选参数默认当前值 → 设置 mode/temp/fan |
| `run_scene` | 复合 | 批量调用 set_light/set_ac 实现场景切换 |
| `set_timer` | 写入 | 查找空闲槽位 → time(NULL) 计算到期时间 |
| `list_timers` | 只读 | 遍历定时器数组，计算剩余秒数 |
| `cancel_timer` | 写入 | 按 ID 查找 → memset 清零 |

**自定义 JSON 解析**（[smart_home_tools.c:82-181]）：

smart_home **没有**使用 cJSON 等第三方库。整个工具层使用自实现的轻量级 JSON 解析函数：

```c
// 查找 JSON key 后的值起始位置
static const char *find_json_value(const char *json, const char *key) {
    p = strstr(json, key);           // 简单子串搜索
    p = strchr(p + strlen(key), ':'); // 找到冒号
    p++;                              // 跳过空格
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// 提取整数值
static int json_get_int(const char *json, const char *key, int *out) {
    const char *p = find_json_value(json, key);
    *out = (int)strtol(p, &end, 10);  // 用标准库 strtol
    return (end == p) ? ERROR : OK;
}
```

**局限性**（文档明确标注）：

- 不处理嵌套对象中的同名 key
- 不验证 JSON 整体结构
- 不处理字符串值中的引号转义

**为什么不用 cJSON？** 这些限制对工具参数解析是可接受的——LLM 生成的 JSON 是平坦的、格式良好的，不需要完整的 JSON 解析器。代价是代码体积从 ~20KB（cJSON）降到 ~100 行。

---

### 2.6 虚拟设备层

**文件**：[smart_home_device.c](smart_home_device.c)（680 行）

在无真实硬件的模拟环境中，设备层实现了完整的**虚拟智能家居状态管理**。

**状态模型**：

```c
typedef struct {
    smart_home_device_t devices[8];  // 最多 8 个设备
    int next_device_id;              // 自增 ID
    int env_temperature;             // 环境温度 0-45°C
    int env_humidity;                // 环境湿度 0-100%
    int env_light;                   // 环境光照 0-1000 lux
} smart_home_state_t;

typedef struct {
    int used;                        // 槽位是否被占用
    int id;                          // 唯一 ID
    char room[16];                   // 房间名（仅 living_room/bedroom）
    char name[32];                   // 设备名
    smart_home_device_type_t type;   // LIGHT / AC
    int on;                          // 开关状态
    int brightness;                  // 亮度 0-100（仅 Light）
    int temperature;                 // 目标温度 16-30（仅 AC）
    int ac_mode;                     // cool=0/heat=1/dry=2/fan=3/auto=4
    int ac_fan_speed;               // low=0/medium=1/high=2/auto=3
} smart_home_device_t;
```

**设计原则**：

1. **Clamp 是一切**：每个 setter 都经过范围约束——防止 LLM 返回异常值破坏状态一致性
2. **简化验证**：房间名只接受 `living_room` 和 `bedroom`，拒绝模糊输入
3. **上下文注入**：设备通过 `agent_context_provider_t`（优先级 80）将当前状态 JSON 注入每条 model 请求。LLM 不需要"猜测"家里的状态
4. **设备 CRUD 不在 LLM 工具中**：add/edit/delete 操作仅在 Panel UI 层面暴露，LLM 不能创建设备——这防止了 LLM 幻觉产生的虚假设备

---

### 2.7 LVGL UI 多线程架构

**文件**：[smart_home_lvgl_agent.c]（~250 行）

这是嵌入式 GUI 集成的核心难点——Agent 是同步阻塞的（HTTP 请求可能需要数秒），不能阻塞 LVGL 主循环。

**双线程模型**：

```text
┌─ LVGL 主线程 (timer/poll loop) ───────────┐
│  lv_timer_handler()                        │
│    → UI 事件处理（触控、刷新）              │
│    → lv_async_call() 回调（线程安全通道）   │
│      ├── ui_event_async_cb()               │
│      └── agent_done_async_cb()             │
└────────────────────────────────────────────┘
          ↕ lv_async_call (线程安全)
┌─ Agent 工作线程 ──────────────────────────┐
│  agent_worker_main()                       │
│    → smart_home_agent_run() (阻塞)         │
│    → lv_async_call(agent_done_async_cb)    │
└────────────────────────────────────────────┘
          ↕ agent event callback
┌─ Agent 内部线程 ──────────────────────────┐
│  smart_home_lvgl_event_cb()                │
│    → 收到事件 → malloc payload             │
│    → lv_async_call(ui_event_async_cb)      │
└────────────────────────────────────────────┘
```

**关键线程安全设计**：

1. **Agent 在工作线程运行**：`pthread_create(agent_worker_main)`，detached 线程
2. **事件回调跨线程转发**：Agent 事件在 Agent 线程上下文触发 → `malloc` 分配 payload → `lv_async_call()` 投递到 LVGL 线程
3. **防重入**：`request_inflight` 标志阻止用户在上一次请求完成前发送新请求
4. **lv_async_call 是唯一的跨线程通道**：LVGL 的 `lv_async_call` 将函数调用安全地排队，在主循环执行

**思考链可视化**：

```text
用户输入 "把卧室灯打开"
  → 聊天栏显示用户气泡（蓝色右对齐）
  → 显示 "Thinking..." trace 卡片
  → ITERATION_START → 更新 trace 卡片
  → MODEL_REQUEST → 更新状态
  → TOOL_CALL: set_light → trace 卡片显示工具名 + running
  → TOOL_RESULT: set_light → trace 卡片显示 ok/failed
  → 模型返回 final content → 显示助手气泡（白色左对齐）
  → 面板自动刷新设备状态
```

---

### 2.8 Skill 系统：Markdown 策略注入

**文件**：[smart_home_skill_loader.c]（~200 行）

Skills 是**不可执行的上下文资源**——它们以 Markdown 文件形式存在，运行时加载并注入 LLM 的 system prompt。

**文件格式**（YAML 前导 + Markdown 正文）：

```markdown
---
name: smart_home_safety
priority: 100
flags: enabled llm_visible
---

# 智能家居安全规则

1. 所有设备查询和状态变更必须使用工具
2. 不要在未调用工具的情况下声称设备已更改
3. 亮度范围 0-100，空调温度 16-30
...
```

**加载流程**：

```
smart_home_skills_register()
  → load_skills_from_default_paths()
    → 遍历 /data/res/skills/*.md
    → 解析 YAML 前导（name/priority/flags）
    → agent_register_skill() ← 注册为 cAGENT skill
  → agent_register_tool_simple("read_skill", ...)
    → 注册一个 LLM 可见的 read_skill 工具
```

**SUMMARY_ONLY 标志**：

```c
#define AGENT_SKILL_FLAG_SUMMARY_ONLY  0x04
```

带有此标志的 skill 的完整内容不会进入 system prompt，只有摘要（名称+描述）进入。当 LLM 需要详细信息时，调用 `read_skill` 工具按需获取。这是"渐进式信息披露"（Progressive Disclosure）在 LLM 上下文管理中的应用。

---

## 3. C 语言技术要点

### 3.1 函数指针与 vtable 模式

**出现位置**：[model.h](model.h)、[runtime.h](runtime.h)

```c
// 模型 Provider 的 vtable 设计
typedef struct agent_model_ops {
    int (*complete)(agent_model_t *model, const agent_runtime_t *runtime,
                    const agent_model_request_t *request,
                    agent_model_response_t *response);
    int (*cancel)(agent_model_t *model);
    void (*destroy)(agent_model_t *model);
} agent_model_ops_t;

// 运行时平台抽象
typedef struct {
    void *(*malloc)(size_t size, void *user_data);
    void (*free)(void *ptr, void *user_data);
    int (*mutex_create)(void *user_data);
    int (*mutex_lock)(void *mutex, uint32_t timeout_ms, void *user_data);
    int (*http_post)(...);
    // ... 共 15+ 个函数指针
} agent_runtime_t;
```

**涉及的 C 语言知识点**：

- 函数指针声明语法：`返回类型 (*名称)(参数列表)`
- 通过函数指针实现多态：调用者不知道具体实现，只通过 `ops->complete()` 调用
- 回调注册模式：`agent_set_event_callback(agent, my_callback, user_data)`
- `user_data` 透传模式：C 语言中"闭包"的标准替代方案

---

### 3.2 静态数组与编译时常量

**出现位置**：整个 cAGENT 和 smart_home

```c
// cAGENT 中的容量限制（通过宏/Kconfig 配置）
#define CAGENT_MAX_TOOLS      16
#define CAGENT_MAX_SESSIONS    4
#define CAGENT_MAX_SESSION_MESSAGES 64
#define CAGENT_REQUEST_ARENA_SIZE   16384

// 嵌入在 agent_t 内部的固定大小数组
agent_tool_t        tools[CAGENT_MAX_TOOLS];
agent_session_t     sessions[CAGENT_MAX_SESSIONS];
agent_session_entry_t entries[CAGENT_MAX_SESSION_MESSAGES];
```

**涉及的 C 语言知识点**：

- 编译时数组大小：`sizeof(array) / sizeof(array[0])`
- `memset` 初始化大型结构体
- 固定内存布局的可预测性（无 runtime 分配）
- `alignas` / 内存对齐（如 `model.cc` 中 `alignas(16)`）
- FLEXIBLE_ARRAY_MEMBER 的替代方案（用固定大小数组而非 FAM）

---

### 3.3 memmove 实现环形删除

**出现位置**：[session_mgr.c:803-815]

```c
// session entry 数组中删除一段 [turn_start, turn_end]
uint32_t remove_count = turn_end - turn_start + 1u;
uint32_t remaining = session->count - turn_start - remove_count;

if (remaining > 0u) {
    memmove(&session->entries[turn_start],          // dest
            &session->entries[turn_end + 1u],       // src
            remaining * sizeof(agent_session_entry_t)); // count
}
session->count -= remove_count;
```

**涉及的 C 语言知识点**：

- `memmove` vs `memcpy` 的区别：`memmove` 保证正确处理源和目的重叠
- 为什么这里必须用 `memmove`：删除段在数组头部，后续元素前移时源和目的重叠
- `sizeof()` 作为乘法因子确保正确计算字节数

---

### 3.4 字符串安全操作

**出现位置**：[session_mgr.c:33-67]、[smart_home_device.c]

```c
// cAGENT 中的 copy_string —— 从不静默截断
static int copy_string(char *dst, size_t dst_size,
                       const char *src, bool allow_null,
                       size_t *written) {
    len = strlen(src);
    if (len >= dst_size) {
        dst[0] = '\0';
        return AGENT_ERROR_LIMIT;  // 不够大 → 报错！
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    if (written) *written = len;
    return AGENT_OK;
}
```

```c
// smart_home 中的安全名称复制（过滤控制字符）
static void copy_json_safe_name(char *dest, size_t dest_size,
                                 const char *src, const char *fallback) {
    for (i = 0; i + 1 < dest_size && input && input[i]; i++) {
        char ch = input[i];
        if (ch == '"' || ch == '\\' || (unsigned char)ch < 0x20) {
            ch = ' ';  // 控制字符替换为空格
        }
        dest[i] = ch;
    }
    dest[i] = '\0';
}
```

**涉及的 C 语言知识点**：

- `strncpy` 的陷阱：可能不添加 null 终止符
- 缓冲区溢出防护：始终传递 `sizeof(dst)` 作为大小限制
- `snprintf` 返回值检查：`if (n < 0 || (size_t)n >= remaining)` 判断截断
- 空指针防御：所有公共 API 入口检查 `if (!pointer) return ERROR`
- `strtol` / `strtoul` 的正确用法：检查 `end == p` 确认解析成功

---

### 3.5 位运算与标志位

**出现位置**：[types.h](types.h) 和工具/skill 注册

```c
// 标志位定义
#define AGENT_TOOL_FLAG_LLM_VISIBLE  0x01u
#define AGENT_TOOL_FLAG_READ_ONLY    0x02u
#define AGENT_CONTEXT_FLAG_OPTIONAL  0x01u
#define AGENT_SKILL_FLAG_SUMMARY_ONLY 0x04u

// 位或组合
agent_register_tool_simple(..., AGENT_TOOL_FLAG_LLM_VISIBLE |
                                   AGENT_TOOL_FLAG_READ_ONLY);

// 位与检查
if (tool->flags & AGENT_TOOL_FLAG_LLM_VISIBLE) { ... }
```

**涉及的 C 语言知识点**：

- 位操作符 `|`（设置）、`&`（检查）、`~`（取反）、`^`（翻转）
- 使用 `unsigned` 类型作为标志容器（避免有符号整数的符号位陷阱）
- 每个标志占 1 位 → 32 个标志用一个 `uint32_t` 表达
- 编译时常量用 `0xu` 后缀确保无符号类型

---

### 3.6 DJB2 哈希算法

**出现位置**：[smart_home_tools.c:220-229]

```c
static unsigned int weather_hash_location(const char *location) {
    unsigned int hash = 5381u;  // 魔数种子
    while (location && *location) {
        hash = ((hash << 5) + hash) + (unsigned char)*location++;
        //   = hash * 33 + char
    }
    return hash;
}
```

**涉及的 C 语言知识点**：

- `#define` 宏 vs `static inline` 函数的选择（这里用普通函数，因为不需要宏的 token 替换能力）
- `const char *` 字符串遍历：`while (*ptr) { ... ptr++; }`
- 无符号整数溢出在 C 标准中是 well-defined（wrap around modulo 2^N）
- 位移操作替代乘法：`(hash << 5) + hash = hash * 33`

---

### 3.7 enum 与 switch-case 完整性

**出现位置**：整个项目

```c
// 枚举定义
typedef enum {
    AGENT_MESSAGE_ROLE_SYSTEM = 0,
    AGENT_MESSAGE_ROLE_USER,
    AGENT_MESSAGE_ROLE_ASSISTANT,
    AGENT_MESSAGE_ROLE_TOOL,
} agent_message_role_t;

// 模式匹配
static const char *message_role_name(agent_message_role_t role) {
    switch (role) {
    case AGENT_MESSAGE_ROLE_USER:      return "user";
    case AGENT_MESSAGE_ROLE_ASSISTANT: return "assistant";
    case AGENT_MESSAGE_ROLE_TOOL:      return "tool";
    default:                           return NULL;  // 显式处理未知值
    }
}
```

**涉及的 C 语言知识点**：

- `enum` 默认值从 0 开始递增
- `switch` 语句必须处理 `default` 分支（防御性编程）
- `typedef enum` 创建类型别名
- `enum` 在 C 中是 `int` 类型，不是强类型（与 C++ 不同）

---

### 3.8 goto cleanup 错误处理模式

**出现位置**：[agent_loop.c:201-456]、[smart_home_agent.c:47-119]

```c
int smart_home_agent_app_init(smart_home_agent_app_t *app) {
    ret = create_agent();
    if (ret != OK) { goto cleanup; }

    ret = register_context();
    if (ret != OK) {
        smart_home_agent_app_deinit(app);  // 逆序释放已初始化的资源
        return ret;
    }

    ret = register_skills();
    if (ret != OK) {
        smart_home_agent_app_deinit(app);
        return ret;
    }

    // ... 更多初始化

    return OK;
}
```

**涉及的 C 语言知识点**：

- C 语言中 `goto` 的合理使用场景：统一错误处理路径
- 资源释放顺序必须是**初始化的逆序**
- Linux 内核编码风格推荐此模式
- 比 `if-else` 嵌套层数更少，可读性更好

---

## 4. 嵌入式系统实现

### 4.1 平台抽象层（PAL）设计

**文件**：[runtime.h](runtime.h) + 4 个 runtime 实现文件

cAGENT 有 4 个平台适配：

| 平台 | 文件 | 特点 |
|---|---|---|
| POSIX fallback | `runtime.c` | 标准 `malloc/free`、`time(NULL)*1000`（秒精度）、stderr 日志 |
| openvela/NuttX | `runtime_openvela.c`（665 行） | `pthread_mutex`、`clock_gettime`、syslog、**mbedTLS HTTPS** |
| ESP-IDF/FreeRTOS | `runtime_espidf.c`（530 行） | FreeRTOS binary semaphore、`esp_timer`、ESP_LOGx、**mbedTLS + lwIP** |
| STM32 | `runtime_stm32.c`（602 行） | FreeRTOS 和 bare-metal **双配置**，CMSIS 临界区 |

**抽象接口 (vtable)**：

```c
typedef struct {
    // 内存
    void *(*malloc)(size_t size, void *user_data);
    void  (*free)(void *ptr, void *user_data);

    // 时间（必须单调递增，精度至少毫秒）
    uint64_t (*now_ms)(void *user_data);

    // 日志
    void (*log)(int level, const char *tag, const char *fmt, ...);

    // 同步原语
    void *(*mutex_create)(void *user_data);
    int   (*mutex_lock)(void *mutex, uint32_t timeout_ms, void *user_data);
    int   (*mutex_unlock)(void *mutex, void *user_data);
    void  (*mutex_destroy)(void *mutex, void *user_data);

    // 临界区（ISR 安全）
    uint32_t (*enter_critical)(void *user_data);
    void     (*exit_critical)(void *user_data, uint32_t state);

    // 网络
    int (*http_post)(...);  // HTTPS POST with TLS

    void *user_data;
} agent_runtime_t;
```

---

### 4.2 openvela/NuttX 运行时实现

**文件**：[runtime_openvela.c](runtime_openvela.c)（665 行）

```c
// 线程安全
static int openvela_mutex_lock(void *mutex, uint32_t timeout_ms, void *user_data) {
    if (timeout_ms == 0u) {
        return pthread_mutex_trylock((pthread_mutex_t *)mutex) == 0
                   ? AGENT_OK : AGENT_ERROR_BUSY;
    }
    // 带超时的锁（用条件变量模拟 pthread_mutex_timedlock）
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    return pthread_mutex_timedlock(mutex, &ts) == 0
               ? AGENT_OK : AGENT_ERROR_TIMEOUT;
}

// 高精度时间（必须单调）
static uint64_t openvela_now_ms(void *user_data) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // 单调时钟，不受系统时间调整影响
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
```

**mbedTLS HTTPS 实现**（核心难点）：

```text
openvela HTTPS POST 流程:
  1. socket() + connect() → TCP 连接到 API 服务器
  2. mbedtls_ssl_init() + mbedtls_ssl_config_defaults()
  3. mbedtls_ssl_setup() → 绑定 socket 到 TLS 上下文
  4. mbedtls_ssl_handshake() → TLS 握手（1.2 / 1.3）
  5. mbedtls_ssl_write() → 发送 HTTP 请求
  6. mbedtls_ssl_read() 循环 → 接收响应
  7. 解析分块传输编码（Transfer-Encoding: chunked）
  8. mbedtls_ssl_free() + close()
```

---

### 4.3 临界区保护（ISR 安全）

**出现位置**：[agent_core.c:236-251] 和各 runtime

```c
// 协作式取消的标志设置必须在 ISR 中安全
int agent_cancel(agent_t *agent) {
    uint32_t state;
    state = agent_runtime_enter_critical(&agent->runtime);  // 关中断 / 禁调度
    agent->cancel_requested = true;                          // 原子操作位置
    agent_runtime_exit_critical(&agent->runtime, state);     // 恢复
    agent_model_cancel(agent->model);
    return AGENT_OK;
}
```

**各平台的实现**：

```c
// STM32 bare-metal -> CMSIS 关全局中断
static uint32_t stm32_bare_enter_critical(void *user_data) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

// FreeRTOS -> 暂停任务调度
static uint32_t freertos_enter_critical(void *user_data) {
    taskENTER_CRITICAL();
    return 0;
}
```

**涉及的嵌入式知识点**：

- 临界区 vs 互斥锁：临界区关中断（ISR 和任务都安全），互斥锁睡眠（仅任务上下文）
- `cancel_requested` 是 `volatile bool` 的原因：可能被中断处理函数或另一线程修改
- 保存/恢复中断状态的必要性：不能盲目 `enable_irq()`，可能之前已经关了

---

### 4.4 网络 I/O 与 TLS

**mbedTLS 配置要点**：

```c
// 在 runtime_openvela.c / runtime_espidf.c 中
mbedtls_ssl_config config;
mbedtls_ssl_config_init(&config);
mbedtls_ssl_config_defaults(&config,
    MBEDTLS_SSL_IS_CLIENT,                    // 我们发起连接
    MBEDTLS_SSL_TRANSPORT_STREAM,             // TCP
    MBEDTLS_SSL_PRESET_DEFAULT);              // 默认密码套件

// 设置主机名（SNI）
mbedtls_ssl_set_hostname(&ssl, host);

// 验证选项（Demo 阶段不校验证书，生产必须开启）
mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_OPTIONAL);
```

**Chunked Transfer Decoding**：

```text
HTTP/1.1 200 OK
Transfer-Encoding: chunked

1a4\r\n          ← 十六进制 chunk 大小
{...json...}\r\n
0\r\n            ← 终止 chunk
\r\n
```

cAGENT 的 HTTP 响应读取器处理了 Content-Length 和 chunked 两种模式。chunked 模式需要逐块解析大小行→数据→CRLF。

---

### 4.5 内存管理策略

**三层内存策略**：

| 层级 | 分配方式 | 生命周期 | 使用场景 |
|---|---|---|---|
| 全局静态 | `static` 数组 | 整个程序 | 工具输出缓冲区、定时器数组 |
| Agent 成员 | `agent_t` 中的固定数组 | Agent 实例 | tools/skills/sessions |
| 请求 Arena | `agent_runtime_malloc` + 线性分配 | 单次 `agent_run()` | context/tools_json/messages_json |

**为什么是三层而非统一管理？**

1. **全局静态**：工具 handler 的输出缓冲区不能随 run 释放——它们可能在事件回调中还被引用
2. **Agent 成员**：tools/skills 在整个 Agent 生命周期中存在，但随 Agent 销毁而销毁
3. **请求 Arena**：每次 run 的临时数据，run 结束后完全无用——最适合线性分配+整块释放

---

### 4.6 日志与可观测性

**事件系统设计**（[event.h](event.h)）：

```c
// 10 类事件
typedef enum {
    AGENT_EVENT_RUN_START,        // 推理开始
    AGENT_EVENT_RUN_DONE,         // 推理结束
    AGENT_EVENT_ITERATION_START,  // ReAct 迭代开始
    AGENT_EVENT_MODEL_REQUEST,    // 发送 HTTP 请求
    AGENT_EVENT_MODEL_RESPONSE,   // 收到 HTTP 响应
    AGENT_EVENT_TOOL_CALL,        // 工具被调用
    AGENT_EVENT_TOOL_RESULT,      // 工具执行结果
    AGENT_EVENT_ERROR,            // 错误
    AGENT_EVENT_CANCELLED,        // 被取消
    AGENT_EVENT_TIMEOUT,          // 超时
} agent_event_type_t;
```

smart_home 控制台 UI 将这些事件转换为人类可读的时间线：

```text
[   0ms] ▶ start
[   0ms]   step 1
[  12ms]   request
[1423ms]   response ok  (HTTP round-trip)
[1423ms]   └─ tool call: set_light
[1424ms]   └─ tool result: ok (set_light complete)
[1424ms]   step 2
[1450ms]   request
[2530ms]   response ok  (LLM sees tool result)
[2530ms] ▶ done ok
```

---

## 5. 常见面试题整理

### 5.1 C 语言基础

**Q1: `static` 关键字在 C 语言中有哪些用法？分别在项目中哪里出现？**

**A**:

| 用法 | 含义 | 项目示例 |
|---|---|---|
| `static` 全局变量 | 文件作用域，外部不可见 | `static smart_home_timer_t g_timers[8]` |
| `static` 函数 | 文件作用域，外部不可见（实现细节隐藏） | `static int copy_string(...)` |
| `static` 局部变量 | 函数退出后保持值，仅初始化一次 | `static char output[1024]` 在工具 handler 中 |

`static` 的核心意义是**限制可见性**和**控制生命周期**。在嵌入式系统中，它避免了全局命名空间污染，同时保证了线程本地存储的持久性。

---

**Q2: `const` 关键字的多种用法？项目中哪里体现了 const 正确性？**

**A**:

```c
// 1. 指向常量的指针（不能通过指针修改数据）
const char *name;                      // *name 不可修改
const agent_tool_call_t *call;         // *call 不可修改

// 2. 常量指针（指针自身不可修改）
char *const buffer = fixed_addr;

// 3. 常数组（数据不可修改，通常放入 .rodata 段）
static const char g_system_prompt[] = "You are a smart home assistant...";
#define SCHEMA_GET_HOME_STATUS "...json..."  // 编译时常量字符串

// 4. const 参数（函数承诺不修改参数）
int copy_string(char *dst, size_t dst_size, const char *src, ...);
```

项目中的 `const` 正确性：任何不应修改的参数都声明为 `const`。这是 C 语言中实现"接口契约"的最佳方式——编译器会检查违规。

---

**Q3: `sizeof` 是函数还是操作符？在项目中有什么典型的嵌入式用法？**

**A**: `sizeof` 是**编译时操作符**（不是函数）。它返回对象或类型在内存中的字节数，结果在编译时就确定了（VLA 除外）。

```c
// 典型嵌入式用法
memset(&obj, 0, sizeof(obj));                     // 初始化任意结构体
strncpy(buf, src, sizeof(buf) - 1);               // 安全字符串复制
agent = agent_runtime_malloc(runtime, sizeof(*agent)); // 分配正确大小
int count = sizeof(array) / sizeof(array[0]);     // 计算数组元素个数
```

---

**Q4: `volatile` 的使用场景？项目中为什么 `cancel_requested` 声明为 `volatile`？**

**A**:

`volatile` 告诉编译器：**每次访问这个变量都要从内存读取，不要优化到寄存器**。

```c
volatile bool cancel_requested;  // 在 agent_internal.h 中
```

```c
// 在 agent_loop.c 的循环中
if (agent->cancel_requested) {  // 每次循环都从内存读取
    return AGENT_ERROR_CANCELLED;
}
```

如果不加 `volatile`：
- 编译器可能将 `cancel_requested` 优化到寄存器
- 另一个线程/ISR 设置了这个标志，但循环中的检查读到的还是寄存器中的旧值
- 取消永远不生效 → 死循环

---

### 5.2 数据结构与算法

**Q5: 线性 Arena 分配器如何工作？相比 malloc/free 有什么优势？**

**A**:

```c
// 核心思想：只记录一个 offset，分配时 offset 递增，释放时不归还
typedef struct {
    unsigned char *buffer;
    size_t size;
    size_t used;     // 当前 offset
    size_t peak;     // 历史峰值
} arena_t;

void *arena_alloc(arena_t *arena, size_t size) {
    // 对齐 + 检查越界 + offset 递增 + 清零 + 返回
    offset = align_up(arena->used, sizeof(void*));
    if (offset + size > arena->size) return NULL;
    arena->used = offset + size;
    arena->peak = max(arena->peak, arena->used);
    memset(ptr, 0, size);
    return ptr;
}
```

**优势**：
1. **O(1) 分配**：不存在空闲链表遍历
2. **无碎片化**：不归还 = 不产生碎片
3. **整块释放**：arena_end 一次性释放，不依赖逐个 free
4. **峰值追踪**：peak 字段记录历史最大用量，指导容量调优
5. **适用场景**：请求/响应式短生命周期分配

**局限**：不能单独释放某个对象。只适用于生命周期一致的对象集合。

---

**Q6: 环形缓冲区（Ring Buffer）的经典实现？项目中有类似的模式吗？**

**A**:

cAGENT 的 session entry 数组使用了**逻辑上类似环形缓冲的逐出策略**，但物理实现是 memmove：

```c
// 淘汰最旧的完整 turn（逻辑上的 FIFO）
// 物理实现：数组头部删除 + memmove
if (remaining > 0) {
    memmove(&entries[turn_start],          // dest
            &entries[turn_end + 1],        // src
            remaining * sizeof(entry));    // count
}
```

memmove 的复杂度是 O(N)，但在 MCU 的对话历史规模下（数十条消息，每条 ~600 字节）是可接受的。

真正的环形缓冲（O(1) 入队/出队）在 audio_event 项目中大量使用。

---

### 5.3 嵌入式系统

**Q7: 嵌入式 RTOS 中任务间通信有哪些方式？cAGENT 使用了哪些？**

**A**:

| 方式 | 特点 | cAGENT 使用 |
|---|---|---|
| 互斥锁 (Mutex) | 保护共享资源，有优先级继承 | `agent_run()` 的 busy 锁 |
| 信号量 (Semaphore) | 任务同步，计数型 | ESP-IDF runtime 的 binary semaphore |
| 消息队列 (MQ) | 任务间传递数据 | audio_event 中音频驱动通知 |
| 临界区 (Critical Section) | ISR 安全，关中断 | `agent_cancel()` 的标志设置 |
| 事件回调 (Callback) | 观察者模式，松耦合 | Agent 事件 → UI 的事件桥接 |
| lv_async_call | GUI 线程安全投递 | smart_home LVGL 跨线程通信 |

---

**Q8: 为什么嵌入式系统倾向于静态分配而非动态分配？**

**A**:

1. **确定性**：编译时就知道总内存使用，不会运行时 OOM
2. **无碎片化**：长期运行的设备中，malloc/free 会导致 heap 碎片化
3. **快速启动**：BSS 段在启动时清零，无需逐个构造
4. **调试友好**：linker map 文件直接显示每个 buffer 地址和大小
5. **安全性**：静态分配的缓冲区地址固定，便于 MPU 设置内存保护

cAGENT 的整个设计贯穿此原则：tools 数组、sessions 数组、session entries 全部在 `agent_t` 结构体内部静态分配。只有 request arena 在每次 run 时动态分配（因为大小取决于 Kconfig 配置）。

---

**Q9: 从 NuttX Sim 到真实 MCU 的代码移植要点？**

**A**:

1. **平台抽象层是关键**：所有平台相关代码通过 runtime vtable 隔离
2. **内存约束收紧**：Sim 下 Arena 可以设 128KB，MCU 上可能只有 32KB
3. **时间精度**：Sim 用 `clock_gettime(CLOCK_MONOTONIC)`（纳秒级），MCU 可能只有毫秒精度 HAL tick
4. **TLS 链路**：Sim 下用宿主 OpenSSL（功能完整），MCU 上用 mbedTLS（需要裁剪配置以减小 ROM）
5. **堆栈大小**：Sim 的栈无限大（Linux 分配），MCU 上需要精确评估每个任务的栈深度
6. **日志输出**：Sim 用 fprintf(stderr)，MCU 用 syslog/UART
7. **文件系统**：Sim 用 HostFS，MCU 用 LittleFS/SPI Flash

---

### 5.4 AI Agent 概念

**Q10: 什么是 ReAct 循环？cAGENT 是如何实现的？**

**A**:

ReAct = Reasoning + Acting。LLM 在每一步中既可以"思考"（生成文本），也可以"行动"（调用工具）。

```
用户: "北京今天天气怎么样"

迭代 1:
  LLM 推理: 需要调用 get_weather 工具
  LLM 行动: tool_call: get_weather(location="Beijing")
  工具返回: {"temperature": 30, "condition": "clear"}

迭代 2:
  LLM 推理: 有了天气数据，可以回答了
  LLM 输出: "北京今天晴天，气温 30°C。"
  → 标记为 final，结束循环
```

cAGENT 实现在 [agent_loop.c:258-440]：
- `max_steps` 限制迭代次数
- 每次迭代先组装上下文+工具 schema+对话历史
- LLM 返回 → 要么 final content（结束），要么 tool_calls（执行后继续）
- 工具结果以 tool role 消息写回 session，下一轮 LLM 能"看到"

---

**Q11: Tool Calling / Function Calling 协议是什么？**

**A**:

LLM 的 tool calling 是一种结构化的输出格式。LLM 不是"猜测"然后输出文本，而是输出一个标准 JSON 结构：

```json
// LLM 响应
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": null,
      "tool_calls": [{
        "id": "call_abc123",
        "type": "function",
        "function": {
          "name": "get_weather",
          "arguments": "{\"location\":\"Beijing\"}"
        }
      }]
    }
  }]
}
```

然后应用代码执行工具并返回结果：

```json
// 下一条消息
{
  "role": "tool",
  "tool_call_id": "call_abc123",
  "content": "{\"temperature\": 30, \"condition\": \"clear\"}"
}
```

LLM 收到工具结果后，再次推理并给出最终的自然语言回复。

---

## 6. 技术难点与解决方案

### 6.1 上下文窗口溢出管理

**挑战**：嵌入式设备的内存有限（对话历史只能存储几十条消息），但长对话会超过这个限制。

**方案**：三层防护（[context-budget-plan.md](context-budget-plan.md)）

```text
第 0 层（已实现）：Turn 淘汰 + 重试
  → 上下文溢出 → evict_oldest_turn() → 重新组装 → 重试
  → 优点：对应用层完全透明
  → 局限：丢失早期对话历史

第 1 层（规划中）：应用策略 API
  → 应用注册 policy 回调，选择保留哪些历史

第 2 层（规划中）：细粒度预算
  → 为 system prompt / tools / messages 分别设置预算
```

cAGENT 的 budget_retry 循环（[agent_loop.c:278-353]）优雅地解决了"一次 evict 不够，需要多次"的场景：

```c
for (budget_retry = 0;
     budget_retry <= CAGENT_MAX_SESSION_MESSAGES;
     budget_retry++) {
    ret = build_context();
    ret = build_tools_json();
    ret = build_messages_json();
    if (is_budget_error(ret)) {
        evict_oldest_turn();    // 淘汰 → 下一轮重试
        continue;
    }
    break;
}
```

---

### 6.2 并发安全性

**挑战**：Agent 在工作线程运行（HTTP 阻塞不可中断），UI 在主线程运行（LVGL 不是线程安全的），多个线程需要安全的协调。

**方案**：多级防护

```text
第 1 级：Agent busy 标志 + trylock
  → agent_run() 入口 trylock，已 run 中返回 BUSY
  → 保证同时只有一个 Agent 推理运行

第 2 级：Critical section 保护 cancel_requested
  → 关中断/禁调度 → 设置标志 → 恢复
  → 协程中断可在任意时刻安全取消

第 3 级：lv_async_call 跨线程桥接
  → malloc payload + lv_async_call(callback)
  → LVGL 主循环在安全时刻调用 callback
  → 所有 widget 操作都在正确线程

第 4 级：request_inflight 防重入
  → UI 发送请求前检查此标志
  → 防止用户双击按钮触发两次推理
```

---

### 6.3 跨平台 HTTP/TLS

**挑战**：openvela、ESP-IDF、STM32 各有不同的网络栈和 TLS 库。同时需要支持分块传输编码。

**方案**：

1. **统一内部抽象**：每个 runtime 实现相同的 `http_post` 签名
2. **mbedTLS 作为通用 TLS 库**：三个平台都用 mbedTLS，减少碎片化
3. **共享的 HTTP 解析逻辑**：`parse_chunked_body()` 在各 runtime 中逻辑相同
4. **Chunked Transfer 解码**：逐块解析 `size\r\ndata\r\n`，最后 `0\r\n\r\n` 终止

```text
通用 HTTP 响应读取器结构（所有 runtime 共用模式）：
  1. 读取响应头 → 找到 Content-Length 或 Transfer-Encoding: chunked
  2. 如果 Content-Length → 直接读取 N 字节
  3. 如果 chunked → 循环：
     a. 读取 chunk 大小（十六进制行）
     b. 如果为 0 → 读取尾部 header → 结束
     c. 读取 chunk 数据 + CRLF
  4. 检查 HTTP 状态码（200 = 成功）
```

---

### 6.4 JSON 解析的选择

**挑战**：cJSON 约 20KB ROM，对于资源受限的 MCU 是沉重负担。但 LLM API 返回值是 JSON。

**方案**：分层决策

| 场景 | 方案 | 原因 |
|---|---|---|
| cAGENT model_openai | 递归下降手写解析器 | 响应 JSON 格式固定，不需要通用解析器 |
| cAGENT tool_schema | 手写 JSON 构建器 | 输出可控，只用 `snprintf` 拼接 |
| smart_home tools | `find_json_value` + `strtol`/`strncmp` | 参数是平坦 JSON，子串搜索足够 |
| smart_home LVGL UI | 无 JSON 解析（直接用 C 结构体） | 配置和状态全在 C 层面 |

**手写解析器的取舍**：

- ✅ ROM 极小（~300 行代码 vs 20KB cJSON）
- ✅ 完全可控（无第三方依赖）
- ❌ 不处理嵌套对象中的同名 key
- ❌ 不验证 JSON 整体结构
- ❌ 不处理字符串中的转义引号

对于受控的 LLM 输入（OpenAI API 返回格式有严格规范），这些局限是可接受的。

---

### 6.5 Session 完整性保证

**挑战**：tool_calling 要求在 session 中维持 `user → assistant(tool_calls) → tool → assistant(final)` 的完整消息链。掉消息或顺序错误会破坏 LLM 的上下文理解。

**方案**：多层约束

```c
// 1. 插入前检查——防止状态机错误
session_has_unfinished_turn()      // user 之后才能加 assistant
session_has_pending_tool_calls()   // tool_calls 之后才能加 tool
session_has_pending_tool_call(id)  // 验证 tool_result 对应正确的 call_id

// 2. 淘汰时保证完整性
// 只淘汰完整 turn，不留下孤立的 assistant(tool_calls)
// 无完整 turn 时返回 AGENT_ERROR_LIMIT

// 3. 失败时回滚
// agent_session_drop_unfinished_tail()
//   删除从最后一个 user turn_boundary 到尾部的所有消息
//   已完成的历史 turn 保留
```

---

## 7. 学习路径建议

### 7.1 学习路线图

```text
第 1 周：先跑起来
  □ 构建 smart_home demo（sim:tflm 或 ESP-IDF）
  □ 通过 NSH 或串口发送自然语言命令
  □ 观察事件日志，理解 ReAct 循环的每一步
  重点文件：smart_home_main.c, smart_home_agent.c

第 2 周：理解 cAGENT 核心
  □ agent_core.c — 生命周期、防重入、事件系统
  □ agent_loop.c — ReAct 循环、Arena 分配器
  □ agent_internal.h — 内部数据结构全景
  重点文件：agent_core.c, agent_loop.c

第 3 周：工具系统与 Session 管理
  □ tool_registry.c + tool_schema.c + tool_guard.c
  □ session_mgr.c — Turn 淘汰算法
  □ smart_home_tools.c — 8 个实际工具的实现
  重点文件：session_mgr.c, smart_home_tools.c

第 4 周：平台抽象与网络
  □ runtime.h — PAL 接口设计
  □ runtime_openvela.c — 完整实现（pthread + TLS）
  □ model_openai.c — HTTP/JSON 交互
  重点文件：runtime_openvela.c, model_openai.c

第 5 周：LVGL 多线程 UI
  □ smart_home_lvgl_agent.c — Agent ↔ UI 桥接
  □ smart_home_lvgl_chat.c — 聊天思考链可视化
  □ smart_home_lvgl_panel.c — 设备面板
  重点文件：smart_home_lvgl_agent.c

第 6 周：源码中提取面试素材
  □ 整理本项目中的 C 语言考点
  □ 整理嵌入式系统设计模式
  □ 能自述 cAGENT 的 ReAct 算法
```

---

### 7.2 按角色定制的学习重点

**如果你目标是嵌入式 C 开发**：

1. 深入 runtime 层：理解 4 个 platform adapter 的差异
2. 手动实现 session_mgr 中的 memmove 淘汰逻辑
3. 为 STM32 bare-metal 写一个最小的 runtime
4. 闭卷写出 arena 分配器的完整实现

**如果你目标是 AI 应用开发**：

1. 理解 ReAct 循环的每种错误处理路径
2. 为 smart_home 添加一个新的工具（如"设置闹钟"）
3. 修改 system prompt 和 skills 观察 LLM 行为变化
4. 理解 context overflow 的三层防护策略

**如果你目标是系统架构师**：

1. 绘制 cAGENT 的模块依赖图和数据流图
2. 分析 PAL 设计的优缺点（与 POSIX/Linux 标准抽象对比）
3. 评估 session 淘汰策略在不同应用场景下的适用性
4. 设计一个"异步 ReAct"（非阻塞）的 API 方案

---

### 7.3 关键源码阅读顺序

```text
cAGENT（从外向内）：
  1. include/agent.h          — 公共 API 全貌
  2. include/cagent/types.h   — 所有数据类型
  3. src/core/agent_core.c    — 生命周期（短，但关键）
  4. src/core/agent_loop.c    — ReAct 循环（心脏）
  5. src/memory/session_mgr.c — 对话管理（最复杂）
  6. src/llm/model_openai.c   — 网络交互
  7. src/runtime/runtime_openvela.c — 平台适配

smart_home（从入口开始）：
  1. src/app/smart_home_main.c    — 入口
  2. src/agent/smart_home_agent.c — 编排层
  3. src/tools/smart_home_tools.c — 工具实现
  4. src/device/smart_home_device.c — 虚拟设备
  5. src/ui/lvgl/smart_home_lvgl_agent.c — 多线程桥接
```

---

### 7.4 动手实践项目

**初级**：修改 system prompt，改变助手语气
**中级**：添加一个新工具（如 `toggle_device`）
**高级**：为 cAGENT 添加"异步 ReAct"（非阻塞 API）
**专家**：为新的 RTOS（如 Zephyr）编写完整的 platform runtime

---

## 附录：项目文件索引

### cAGENT 核心文件

| 文件 | 行数 | 职责 |
|---|---|---|
| `src/core/agent_core.c` | 390 | 生命周期、同步推理入口、cancel/reset |
| `src/core/agent_loop.c` | 457 | **ReAct 循环执行体** |
| `src/core/context_builder.c` | ~200 | 组装 system prompt + providers + skills |
| `src/memory/session_mgr.c` | 1039 | **有界对话历史、Turn 淘汰算法** |
| `src/llm/model_openai.c` | 928 | **OpenAI 兼容 API、HTTP+TLS、JSON 解析** |
| `src/llm/llm_parse.c` | ~150 | 响应文本解析（final\| / tool\| 格式） |
| `src/tools/tool_registry.c` | ~100 | 工具注册/注销/查找 |
| `src/tools/tool_schema.c` | 527 | **OpenAI tools JSON 构建、缓存、dirty 标记** |
| `src/tools/tool_guard.c` | ~120 | 策略检查、超时检测、大小限制 |
| `src/runtime/runtime_openvela.c` | 665 | **openvela PAL: pthread + mbedTLS** |
| `src/runtime/runtime_espidf.c` | 530 | ESP-IDF PAL: FreeRTOS + mbedTLS |
| `src/runtime/runtime_stm32.c` | 602 | STM32 PAL: FreeRTOS + bare-metal 双模式 |

### smart_home 核心文件

| 文件 | 行数 | 职责 |
|---|---|---|
| `src/agent/smart_home_agent.c` | 211 | **cAGENT 编排：创建 Agent、注册所有组件** |
| `src/tools/smart_home_tools.c` | 773 | **8 个 LLM 工具、自定义 JSON 解析** |
| `src/device/smart_home_device.c` | 680 | **虚拟设备状态管理** |
| `src/skills/smart_home_skill_loader.c` | ~200 | Markdown 技能文件加载 |
| `src/config/smart_home_backends.c` | ~150 | 5 个 LLM 后端预设 |
| `src/ui/smart_home_ui.c` | ~180 | 控制台 UI（事件打印 + REPL） |
| `src/ui/lvgl/smart_home_lvgl_agent.c` | ~250 | **多线程 Agent-UI 桥接** |
| `src/ui/lvgl/smart_home_lvgl_chat.c` | ~300 | 聊天界面 + 思考链可视化 |
| `src/ui/lvgl/smart_home_lvgl_panel.c` | ~500 | 设备面板 + 控制弹窗 |

### 设计文档

| 文档 | 内容 |
|---|---|
| `docs/architecture.md` | cAGENT 完整架构设计（~2000 行） |
| `docs/api_reference.md` | 自动生成的 API 参考（~2500 行） |
| `docs/context-budget-plan.md` | 上下文溢出防护方案 |
| `docs/skill-summary-design.md` | Skill 渐进式信息披露设计 |
| `smart_home/docs/chat-thinking-chain.md` | 思考链可视化设计 |
| `smart_home/docs/environment-simulation.md` | 虚拟环境模拟方案 |
| `smart_home/docs/multi-llm-backend.md` | 多 LLM 后端切换方案 |
| `smart_home/docs/ui-design.md` | LVGL UI 设计文档 |
