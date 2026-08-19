# cAGENT 手写实现 7 天实战计划

> 本文不是"成品文档"，而是**手写训练用的 7 天实战计划**。它基于
> `packages/cAGENT` 真实骨架（`docs/architecture.md`、`docs/plan.md`、
> `include/cagent/*.h`、`src/core/*.c`）提炼而来。
>
> **核心理念**：每天的开发流程是「写代码 → 学 C 语言技巧 → 练设计模式 →
> 读真实源码 → 对照反思」，四者交织在一起，而不是分开学习。文档驱动会写出
> "看起来很对"的代码；只有边写边读真实实现、边反思才能训练架构能力。
>
> **本次重排的核心思路**：从"底层向上堆叠"改为"中心向外扩展"——先在
> Day 2 用 4 个 stub 让 agent_loop 跑通最小闭环，再逐日把 stub 换成真实实现。
> 这样第一天天黑前就能看到 Agent 在动，反馈循环最短。
>
> **使用方式**：严格按 Day1→Day7 顺序，每天完成验收标准后才进入下一天。
> 每天结束写一条反思日志到 `docs/my-reflection.md`。

***

## 0. 总览

### 0.1 训练目标

通过 7 天手写一个简化版 cAGENT 库，训练：

- C 语言工程能力：opaque 类型、ops vtable、arena、固定数组注册表、cooperative cancel
- 嵌入式架构能力：编译期上限、跨平台 runtime 抽象、不静默截断契约
- Agent 内核能力：Bounded ReAct loop、5 道检查点、工具级错误处理、budget_retry
- 工程习惯：增量交付、Walking Skeleton、对照反思、mock 测试

### 0.2 7 天路线图（重排版）

| Day | 主题 | 产出 | C 语言技巧 | 设计模式 |
|-----|------|------|------------|----------|
| 1 | 公共类型与生命周期（预热） | 可编译 `libcagent.a` | 三级宏映射、opaque handle、函数指针 | Opaque Handle |
| 2 | agent\_loop 骨架 + 4 stub 跑通 | Walking Skeleton demo 输出"hello" | stub 替换法、检查点雏形 | Walking Skeleton + Bounded Loop |
| 3 | session + arena + context\_builder | session/arena/context 真实化 | arena allocator、对齐、turn 淘汰 | Arena Allocator |
| 4 | tool/skill registry + mock model 升级 | mock ReAct demo 跑通 tool\_call | 固定数组、浅拷贝、dirty 缓存 | Registry Pattern |
| 5 | OpenAI adapter + ops vtable | 真实 API 调用成功 | vtable 多态、JSON 契约、HTTP 超时 | Adapter + Ops Vtable |
| 6 | 边界加固：cancel/timeout/三级错误 | cancel/timeout/context overflow | 不静默截断、三级错误分层 | Cooperative Cancel + Bounded Loop 完整版 |
| 7 | 测试 + runtime 抽象 + 反思 | 单元测试 + posix runtime + 反思日志 | 跨平台抽象、fallback 链、mock 测试法 | Runtime Abstraction |

### 0.3 排序逻辑（为什么这么排）

| 维度 | 原顺序（底层→上层） | 新顺序（中心→外围） |
|------|---------------------|---------------------|
| 第一天看到的东西 | 一堆 `#define` 和 `typedef` | Agent 跑起来打印 "hello" |
| 反馈循环长度 | Day 4 才看到 loop 串起来 | Day 2 就有可运行物 |
| stub 替换节奏 | 无 | 每天"用一个真实实现替换一个 stub"，结构演化清晰 |
| loop 改动频率 | 一次写完 | 分 3 天增量改（Day 2 骨架 / Day 3 session / Day 4 工具分支） |

**为什么 Day 3 在 Day 4 之前**：session+arena 真实化后 loop 立刻能跑"带真实历史和真实内存管理的无工具对话"；tool_registry 真实化后需要 mock model 配合吐 tool\_calls，依赖更重。先做依赖轻的。

### 0.4 验收里程碑

```text
Day1 结束: gcc -fsyntax-only 通过，agent_create(NULL) 不崩溃
Day2 结束: Walking Skeleton demo 跑通：输入"hi" → 打印"hello"
Day3 结束: session 能追加/淘汰消息，arena 三次分配不泄漏
Day4 结束: 无网络 mock ReAct demo 完整跑通 tool_call → final
Day5 结束: 调用真实 OpenAI-compatible API 完成一次 tool call
Day6 结束: cancel/timeout/context overflow 三类边界用例通过
Day7 结束: 5 个单元测试 + posix runtime adapter + 完整反思日志
```

### 0.5 不做的事

- 不写完整应用框架（不做 CLI、Voice、MQTT、WebSocket、UI）
- 不写业务工具（不做 `wifi_scan`、`read_sensor`、`ota_update`）
- 不绑定特定云厂商
- 不追求一次写对——cAGENT 真实仓库本身是"骨架→演进"的过程

### 0.6 关键边界（贴在显示器上）

```text
core 不认识平台
platform 不理解 Agent 推理逻辑
integration 只连接外部依赖
application 组合真实业务能力
```

> 反例：如果你的 `agent_loop.c` 里出现了 `#include <nuttx/...>` 或
> `#include <esp_log.h>`，架构就破了。

***

## Day 1: 公共类型与生命周期（预热）

### 1.1 今日目标

搭建可编译的库骨架，实现 `agent_create/destroy`。**今天不写任何业务逻辑**，
只把"封装边界"立起来，同时熟悉函数指针、opaque handle、`typedef struct` 三件套。

### 1.2 要写的代码

```
include/
├── agent.h                         ← 聚合头文件
└── cagent/
    ├── types.h                     ← 错误码、limits、stats、request/response
    ├── config.h                    ← agent_config_t
    ├── runtime.h                   ← agent_runtime_t（回调集）
    ├── model.h                     ← opaque agent_model_t + ops
    ├── tools.h                     ← agent_tool_t
    ├── skill.h                     ← agent_skill_def_t
    ├── context.h                   ← agent_context_provider_t
    ├── session.h                   ← session API
    ├── memory.h                    ← memory API（占位）
    ├── event.h                     ← agent_event_t
    └── policy.h                    ← policy callback
src/
├── core/
│   ├── agent_internal.h            ← struct agent 内部定义
│   └── agent_core.c                ← create/destroy/set_*
├── runtime/
│   └── runtime.c                   ← fallback 实现
└── types_internal.h
CMakeLists.txt
```

### 1.3 C 语言教程：三级宏映射

cAGENT 的编译期上限用"直接定义 > Kconfig > 默认值"三级映射，这是嵌入式
C 库的标志。**今天必须掌握**。

```c
/* include/cagent/types.h */
#ifndef CAGENT_MAX_TOOLS
#ifdef CONFIG_CAGENT_MAX_TOOLS
#define CAGENT_MAX_TOOLS CONFIG_CAGENT_MAX_TOOLS
#else
#define CAGENT_MAX_TOOLS 12u
#endif
#endif
```

含义：

- 上层 CMake 直接 `-DCAGENT_MAX_TOOLS=8`：最高优先级
- openvela/NuttX 通过 Kconfig 生成 `CONFIG_CAGENT_MAX_TOOLS=8`：次优先级
- 都没定义：用默认值 `12u`

**为什么这么设计**：嵌入式项目要允许不同板级配置不同上限，而 Kconfig 是
NuttX/openvela 的标准配置机制。直接 `#define MAX_TOOLS 12` 会丢失可配置性。

**练习**：今天至少定义这些宏（参考 `packages/cAGENT/include/cagent/types.h`）：
`CAGENT_MAX_TOOLS`、`CAGENT_MAX_SKILLS`、`CAGENT_MAX_SESSIONS`、
`CAGENT_MAX_SESSION_MESSAGES`、`CAGENT_CONTEXT_BUFFER_SIZE`、
`CAGENT_TOOL_OUTPUT_MAX_SIZE`、`CAGENT_DEFAULT_MAX_STEPS`、
`CAGENT_DEFAULT_TIMEOUT_MS`。

### 1.4 C 语言教程：Opaque Handle

```c
/* include/cagent/types.h —— 公共头只声明不定义 */
typedef struct agent agent_t;

/* src/core/agent_internal.h —— 内部头才定义结构体 */
struct agent {
    agent_config_t config;
    agent_runtime_t runtime;
    /* ... */
};
```

应用代码无法直接访问 `agent->config`，必须通过 `agent_get_stats()` 等 API。
这是 C 语言封装的标准做法。

**为什么不用 `void *`**：`typedef struct agent agent_t` 让编译器做类型检查，
`void *` 会丢失类型安全。

### 1.5 C 语言教程：函数指针与 typedef struct 三件套

今天必须熟悉的三个写法，是后续所有 vtable / registry / callback 的基础：

```c
/* ① 函数指针 typedef（不是函数声明）*/
typedef int (*agent_tool_fn)(const agent_tool_call_t *call,
                             agent_tool_result_t *result,
                             void *user_data);

/* ② struct 的 typedef 别名 */
typedef struct agent_config {
    const char *name;
    const char *system_prompt;
    agent_limits_t limits;
    agent_runtime_t runtime;
} agent_config_t;

/* ③ 结构体内嵌函数指针表（ops vtable 雏形）*/
typedef struct {
    int  (*complete)(void *provider, /* ... */);
    int  (*cancel)(void *provider);
    void (*destroy)(void *provider);
} agent_model_ops_t;
```

**易错点**：`typedef int (*fn)(...)` 的括号不能省，写成 `typedef int *fn(...)`
会变成"返回 `int *` 的函数"，含义完全不同。今天必须把这个括号写对至少 3 次。

### 1.6 设计模式：Opaque Handle

**应用场景**：隐藏 `struct agent` 内部字段，应用代码只持有不透明指针。

**完整代码示例**：

```c
/* src/core/agent_internal.h */
struct agent {
    agent_config_t config;
    agent_runtime_t runtime;
    agent_model_t *model;
    bool busy;
    volatile bool cancel_requested;
    uint64_t run_deadline_ms;
    agent_limits_t limits;
    agent_stats_t stats;
    agent_arena_t request_arena;   /* Day 3 填 */
    void *mutex;
    /* 注册表（Day 4 填） */
};

/* src/core/agent_core.c */
agent_t *agent_create(const agent_config_t *config)
{
    agent_t *agent = (agent_t *)malloc(sizeof(*agent));
    if (!agent) return NULL;
    memset(agent, 0, sizeof(*agent));
    /* 填充 config/runtime/limits */
    return agent;
}

void agent_destroy(agent_t *agent)
{
    if (!agent) return;
    /* 释放 mutex、model（如 owned） */
    free(agent);
}
```

**为什么不用单例模式**：cAGENT 允许创建多个 agent 实例（不同配置、不同模型），
单例是反模式。手写时不要练单例。

### 1.7 真实源码解析：agent_create 的 runtime 双层填充

读 [src/core/agent_core.c](../src/core/agent_core.c) 的 `agent_create`，注意它的 runtime 处理顺序，这是 cAGENT 跨平台的关键：

```c
agent_t *agent_create(const agent_config_t *config)
{
    agent_config_t local_config;
    agent_runtime_t runtime;
    agent_t *agent;

    if (config) {
        local_config = *config;
        agent_runtime_fill_platform(&local_config.runtime);  /* ① 平台层 */
        agent_runtime_fill_defaults(&local_config.runtime);   /* ② libc 兜底 */
    } else {
        local_config = agent_config_default();  /* default 已含两层填充 */
    }
    /* ... */
}
```

**关键设计点**：

1. **拷贝 config 到栈变量**：不修改调用者的 config，保证调用者可复用
2. **双层填充顺序**：`fill_platform` 先填平台特定回调（如 openvela 的 syslog），`fill_defaults` 再用 libc 兜底剩余 NULL 字段
3. **应用层注入优先级最高**：如果 `config->runtime.log` 已被应用设置，两层 fill 都不会覆盖（实现里是 `if (!rt->log) rt->log = ...`）
4. **mutex 按需创建**：`if (agent->runtime.mutex_create)` 才创建，单线程平台可全 NULL

**今天对照检查**：你的 `agent_create` 有没有做到"不改调用者 config"+"应用层优先"+"NULL 安全"三件事？

### 1.7.1 真实源码解析：agent_config_tiny 与 model 所有权

读 [src/core/agent_core.c](../src/core/agent_core.c#L47) 的 `agent_config_tiny`，cAGENT 用预设配置照顾不同硬件档位：

```c
agent_config_t agent_config_tiny(void)
{
    agent_config_t config = agent_config_default();
    config.limits.max_steps = 4u;              /* 默认 12 → 4 */
    config.limits.timeout_ms = 10000u;         /* 30s → 10s */
    config.limits.per_model_timeout_ms = 5000u;
    config.limits.per_tool_timeout_ms = 1000u;
    config.limits.max_tool_calls = 2u;         /* 限制工具调用数 */
    config.limits.max_output_tokens = 128u;    /* 小 MCU 缩减输出 */
    return config;
}
```

**为什么提供两个预设**：`default` 适合有网络/算力充裕的设备（树莓派级），`tiny` 适合小 MCU（Cortex-M4 级）。手写时**不要让用户必须填满 config**，提供合理默认是 API 友好性的关键。

读 [src/core/agent_core.c](../src/core/agent_core.c#L252) 的 `agent_set_model` 和 `agent_set_model_owned`，理解 cAGENT 的"model 所有权"双 API 设计：

```c
int agent_set_model(agent_t *agent, agent_model_t *model)
{
    agent->model = model;
    agent->model_owned = false;   /* 调用者管理生命周期 */
    return AGENT_OK;
}

int agent_set_model_owned(agent_t *agent, agent_model_t *model)
{
    agent->model = model;
    agent->model_owned = true;    /* agent_destroy 时自动 destroy */
    return AGENT_OK;
}
```

```c
void agent_destroy(agent_t *agent)
{
    /* ... */
    if (agent->model_owned && agent->model) {
        agent_model_destroy(agent->model);   /* 只有 owned 时才 destroy */
    }
    /* ... */
}
```

**为什么需要两个 API**：

- `set_model`：model 是单例/全局/被多个 agent 共享 → 调用者管理生命周期
- `set_model_owned`：model 是 agent 私有 → agent_destroy 自动清理，避免泄漏

**今天对照检查**：你的 `agent_destroy` 有没有 `model_owned` 判断？没有的话用户用 `set_model_owned` 注入的 model 会泄漏。

### 1.8 验收标准

- [ ] `gcc -fsyntax-only -Iinclude test.c` 通过，`test.c` 只 `#include <agent.h>`
- [ ] `agent_create(NULL)` 返回非 NULL 指针
- [ ] `agent_destroy(agent)` 不崩溃、不泄漏
- [ ] `agent_set_event_callback(agent, cb, NULL)` 返回 `AGENT_OK`
- [ ] 所有头文件用 `#pragma once` + `extern "C"` 保护
- [ ] 函数指针 typedef 的括号都写对（自查 3 处）

### 1.9 对照反思（做完后回答）

读 [src/core/agent_internal.h](../src/core/agent_internal.h) 和 [src/core/agent_core.c](../src/core/agent_core.c)：

1. 我的 `struct agent` 字段和真实版差哪些？为什么差？
2. 我的三级宏映射写对了吗？有没有漏掉哪个上限？
3. 我的 `agent_create` 有没有处理 `config == NULL`？真实版怎么处理？
4. 我有没有在 `agent_create` 里就 `mutex_create`？真实版怎么做的？
5. 真实版有 `agent_config_tiny()` 这种预设配置，我想到了吗？

***

## Day 2: agent_loop 骨架 + Walking Skeleton

### 2.1 今日目标

实现**最小可运行**的 agent_loop：输入"hi" → mock model 返回"hello" → 打印。
今天用 4 个 stub 把 loop 骨架立起来，**不写任何真实业务**。这是 7 天里反馈
最快的一天——天黑前必须看到 Agent 在动。

### 2.2 要写的代码

```
src/
├── core/
│   ├── agent_loop.c           ← 最小骨架（单轮、无工具、无历史）
│   └── agent_event.c          ← 事件派发占位
├── llm/
│   ├── model.c                ← agent_model facade
│   └── model_mock.c           ← stub：固定返回 "hello"
├── memory/
│   └── session_mgr.c          ← stub：get_history 返回空，append 空实现
├── tools/
│   └── tool_registry.c        ← stub：find 返回 NULL，execute 返回 NOTSUP
└── core/
    └── context_builder.c      ← stub：直接拷贝 system_prompt
examples/
└── walking_skeleton/
    └── main.c                 ← demo：agent_run("hi") → "hello"
```

### 2.3 C 语言教程：Stub 替换法（Walking Skeleton）

Walking Skeleton 是"用最小代码跑通端到端，再逐日替换 stub 为真实实现"的工程
手法。它的核心价值是**让架构骨架在第一天就被验证**，避免"底层全写完才发现
接口设计错"。

**今天 4 个 stub 的契约**（必须先定对，否则 Day 3-5 反复推翻）：

```c
/* stub 1: mock model —— 固定返回 final content */
static int stub_complete(void *provider, agent_runtime_t *rt,
                         const agent_model_request_t *req,
                         agent_model_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->content = "hello";      /* 固定回复 */
    resp->status = AGENT_OK;
    return AGENT_OK;
}

/* stub 2: session —— 空历史，append 直接返回 OK */
int agent_session_build_model_messages(agent_t *agent, agent_session_t *s,
                                       char *buf, size_t size, size_t *written)
{
    /* 只写一个 user message */
    snprintf(buf, size, "[{\"role\":\"user\",\"content\":\"%s\"}]",
             /* current input */ "");
    if (written) *written = strlen(buf);
    return AGENT_OK;
}

/* stub 3: tool registry —— find 返回 NULL */
agent_tool_entry_t *agent_tool_registry_find(agent_t *agent, const char *name)
{
    (void)agent; (void)name;
    return NULL;   /* Day 4 替换 */
}

/* stub 4: context builder —— 直接拷贝 system_prompt */
int agent_context_build(agent_t *agent, const agent_request_t *req,
                        char *buf, size_t size, size_t *written)
{
    size_t len = strlen(agent->config.system_prompt);
    if (len >= size) return AGENT_ERROR_LIMIT;
    memcpy(buf, agent->config.system_prompt, len);
    buf[len] = '\0';
    if (written) *written = len;
    return AGENT_OK;
}
```

**关键原则**：stub 的**签名必须和真实实现一致**，今天照抄 cAGENT 真实头文件的
函数原型，不要自己发明接口。

### 2.4 设计模式：Walking Skeleton + Bounded Loop（雏形）

**应用场景**：用最小代码验证架构骨架，后续每天替换一个 stub。

**Bounded Loop 雏形**（今天只实现 1 道检查点，Day 6 补全 5 道）：

```c
int agent_loop_run(agent_t *agent, const agent_request_t *request,
                   agent_response_t *response)
{
    int ret;
    uint32_t iter;

    if (!agent || !request || !response || !agent->model) {
        return AGENT_ERROR_INVALID;
    }

    for (iter = 0u; iter < agent->limits.max_steps; iter++) {
        agent_model_request_t model_request;
        agent_model_response_t model_response;

        /* 检查点 1（雏形）：cancel_requested —— Day 6 补全 */
        if (agent->cancel_requested) {
            return AGENT_ERROR_CANCELLED;
        }

        /* build context / messages（stub） */
        char context[4096], messages[4096], tools[64] = "[]";
        ret = agent_context_build(agent, request, context, sizeof(context), NULL);
        if (ret != AGENT_OK) break;

        ret = agent_session_build_model_messages(agent, NULL,
                                                 messages, sizeof(messages), NULL);
        if (ret != AGENT_OK) break;

        /* model.complete —— stub 返回 "hello" */
        memset(&model_request, 0, sizeof(model_request));
        model_request.context = context;
        model_request.tools_json = tools;
        model_request.messages_json = messages;
        model_request.input = request->input;

        memset(&model_response, 0, sizeof(model_response));
        ret = agent_model_complete(agent->model, &agent->runtime,
                                   &model_request, &model_response);
        if (ret != AGENT_OK) break;

        /* final content → 直接返回（无 tool_calls 分支，Day 4 补） */
        if (model_response.tool_call_count == 0u) {
            size_t len = strlen(model_response.content ? : "");
            if (len >= response->output_size) { ret = AGENT_ERROR_LIMIT; break; }
            memcpy(response->output, model_response.content, len);
            response->output[len] = '\0';
            return AGENT_OK;
        }
        /* tool_call 分支留到 Day 4 */
        break;
    }

    return ret ? ret : AGENT_ERROR_LIMIT;
}
```

### 2.5 真实源码解析：agent_loop_run 的骨架结构

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L191) 的 `agent_loop_run` 主体。今天只关注**骨架结构**，不看
budget_retry 和 tool_call 分支（那是 Day 3/4 的事）。

**真实版的骨架顺序**（你今天要照抄这个顺序）：

```c
int agent_loop_run(agent_t *agent, const agent_request_t *request,
                   agent_response_t *response)
{
    /* ① 参数校验 + model 校验 */
    if (!agent || !request || !response) return AGENT_ERROR_INVALID;
    if (!agent->model) return AGENT_ERROR_INVALID;

    /* ② check_run_state（cancel + deadline）*/
    ret = check_run_state(agent);
    if (ret != AGENT_OK) return ret;

    /* ③ arena_begin（Day 3 真实化，今天 stub 成空函数）*/
    ret = arena_begin(agent, estimate_request_arena_size());
    if (ret != AGENT_OK) return ret;

    /* ④ allocate_loop_buffers（从 arena 分配 context/tools/messages）*/
    ret = allocate_loop_buffers(agent, &context, &tools_json, &messages_json);
    if (ret != AGENT_OK) { arena_end(agent); return ret; }

    /* ⑤ session find_or_create + append user message */
    session = agent_session_find_or_create(agent, request->session_id);
    /* ... append user ... */

    /* ⑥ 主循环 */
    for (iter = 0u; iter < agent->limits.max_steps; iter++) {
        /* check_run_state → build context/tools/messages → model.complete
         * → parse → tool_call 分支 or final 分支 */
    }

    /* ⑦ 收尾：arena_end + drop_unfinished_tail（错误路径）*/
    arena_end(agent);
    return ret;
}
```

**今天要吸收的 3 个设计点**：

1. **arena_begin/end 包裹整个 loop**：即使今天是 stub，函数调用结构也要保留，Day 3 才能平滑替换
2. **错误路径也要 arena_end**：真实版每个 `return ret` 前都有 `arena_end(agent)`，今天就要养成这个习惯
3. **check_run_state 在循环头部**：不是循环外检查一次，是每轮都检查，这是 cooperative cancel 生效的前提

### 2.5.1 真实源码解析：agent_run 是 agent_loop_run 的"外壳"

读 [src/core/agent_core.c](../src/core/agent_core.c#L123) 的 `agent_run`，理解 cAGENT 的"shell / core 分层"。今天只看外壳，不看 loop 内部：

```c
int agent_run(agent_t *agent, const agent_request_t *request,
              agent_response_t *response)
{
    /* ① 参数校验 */
    if (!agent || !request || !request->input || !response) {
        return AGENT_ERROR_INVALID;
    }

    /* ② mutex lock + busy 防重入 */
    if (agent_runtime_mutex_lock(&agent->runtime, agent->mutex, 0u) != AGENT_OK) {
        return AGENT_ERROR_BUSY;
    }
    if (agent->busy) {
        agent_runtime_mutex_unlock(&agent->runtime, agent->mutex);
        return AGENT_ERROR_BUSY;
    }
    agent->busy = true;

    /* ③ 保存 limits，允许 request 临时覆盖 */
    saved_limits = agent->limits;
    if (request->limits) {
        agent->limits = *request->limits;
    }

    /* ④ 重置 cancel + 计算 deadline */
    agent->cancel_requested = false;
    start_ms = agent_runtime_now_ms(&agent->runtime);
    agent->run_deadline_ms = agent->limits.timeout_ms
                              ? start_ms + agent->limits.timeout_ms : 0u;
    agent->stats.runs++;

    /* ⑤ emit RUN_START，调 loop，emit RUN_DONE */
    agent_event_emit(agent, AGENT_EVENT_RUN_START, ...);
    ret = agent_loop_run(agent, request, response);

    /* ⑥ 按 ret 更新 stats（completed/failed/cancelled/timeout）*/
    /* ⑦ 恢复 limits，清 busy，mutex unlock */
    agent->limits = saved_limits;
    agent->busy = false;
    agent_runtime_mutex_unlock(&agent->runtime, agent->mutex);
    return ret;
}
```

**关键设计点**（今天就要照抄）：

1. **busy 防重入**：同一 agent 不能并发 `agent_run`，否则 session/arena 状态错乱
2. **`saved_limits` 模式**：`request->limits` 只在本次 run 生效，run 结束恢复
3. **`run_deadline_ms = start_ms + timeout_ms`**：deadline 在外壳计算一次，loop 里只比较
4. **stats.runs 在外壳递增**：loop 不负责统计 runs，只负责 iterations/model_calls

**今天对照检查**：你的 `agent_run` 有没有 `busy` 防重入？有没有 `saved_limits` 恢复？deadline 在哪计算的？

### 2.6 Walking Skeleton Demo

```c
/* examples/walking_skeleton/main.c */
#include <stdio.h>
#include "agent.h"

int main(void)
{
    agent_config_t cfg = agent_config_default();
    cfg.system_prompt = "You are a demo agent.";
    agent_t *agent = agent_create(&cfg);

    /* 注入 stub mock model */
    agent_model_t *model = agent_model_mock_create(NULL, NULL);
    agent_set_model(agent, model);

    /* 跑一轮 */
    agent_request_t req = {0};
    req.input = "hi";
    char output[1024];
    agent_response_t resp = {0};
    resp.output = output;
    resp.output_size = sizeof(output);

    int ret = agent_run(agent, &req, &resp);
    printf("ret=%d output=%s\n", ret, resp.output);   /* 期望: ret=0 output=hello */

    agent_destroy(agent);
    return ret == AGENT_OK ? 0 : 1;
}
```

### 2.7 验收标准

- [ ] `walking_skeleton` demo 编译运行输出 `ret=0 output=hello`
- [ ] 4 个 stub 的函数签名和真实头文件一致（照抄，不发明）
- [ ] `agent_loop_run` 的骨架顺序和真实版一致（参数校验 → check → arena → session → loop → arena_end）
- [ ] 错误路径（model 返回错误）也正确走 `arena_end` 收尾
- [ ] `cancel_requested` 检查在循环头部（雏形）

### 2.8 对照反思

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L191) 的 `agent_loop_run`：

1. 我的 4 个 stub 签名和真实实现一致吗？哪个写错了？
2. 我的 loop 骨架顺序和真实版差几步？arena_begin 放对位置了吗？
3. 真实版 `check_run_state` 检查了 cancel 和 deadline 两个，我只检查了 cancel，deadline 留到哪天？
4. 我的 demo 跑通用了几次 model.complete 调用？真实版一次 loop 至少几次？

***

## Day 3: session + arena + context_builder 真实化

### 3.1 今日目标

把 Day 2 的 session/arena/context 三个 stub 换成真实实现。今天完成后 loop
能跑"带真实历史拼接和真实内存管理的无工具对话"。**今天 C 技巧最密集**，
是对齐、bump pointer、turn 边界判断。

### 3.2 要写的代码

```
src/
├── memory/
│   ├── memory_internal.h      ← agent_session_t / agent_message_t 定义
│   └── session_mgr.c          ← 真实实现：append / find / build / evict
├── core/
│   ├── agent_loop.c           ← 补 arena_begin/end/alloc + 真实 session 调用
│   └── context_builder.c      ← 真实实现：system_prompt + providers（占位）
```

### 3.3 C 语言教程：Arena Allocator

cAGENT 的内存策略分三层：

| 层级          | 用途                            | 分配方式                                 |
|---------------|---------------------------------|------------------------------------------|
| 静态          | `struct agent` 内的固定数组      | 编译期                                   |
| Agent 生命周期 | `struct agent` 本身、mutex       | `agent_create` malloc，`destroy` free    |
| Request 作用域 | context/tools/messages buffer   | **Arena**：一次 `agent_run` 一个 arena   |

**Arena 是核心**，参考真实实现 [src/core/agent_loop.c](../src/core/agent_loop.c#L106)：

```c
typedef struct {
    unsigned char *buffer;
    size_t size;
    size_t used;
    size_t peak;       /* 用于观测，写入 stats */
} agent_arena_t;

/* 对齐：sizeof(void*) 通常是 4 或 8 */
static size_t align_up_size(size_t value)
{
    const size_t align = sizeof(void *);
    return (value + align - 1u) & ~(align - 1u);
}

static int arena_begin(agent_t *agent, size_t required_size)
{
    size_t arena_size = (size_t)CAGENT_REQUEST_ARENA_SIZE;
    if (arena_size < required_size) return AGENT_ERROR_LIMIT;
    agent->request_arena.buffer =
        (unsigned char *)agent_runtime_malloc(&agent->runtime, arena_size);
    if (!agent->request_arena.buffer) return AGENT_ERROR_NOMEM;
    agent->request_arena.size = arena_size;
    return AGENT_OK;
}

static void *arena_alloc(agent_t *agent, size_t size)
{
    size_t aligned_used = align_up_size(agent->request_arena.used);
    size_t next_used;
    void *ptr;

    if (size > agent->request_arena.size - aligned_used) {
        return NULL;   /* 不够返回 NULL，调用者返回 AGENT_ERROR_LIMIT */
    }
    next_used = aligned_used + size;
    ptr = agent->request_arena.buffer + aligned_used;
    agent->request_arena.used = next_used;
    if (next_used > agent->request_arena.peak) {
        agent->request_arena.peak = next_used;   /* 更新峰值 */
    }
    memset(ptr, 0, size);
    return ptr;
}
```

**Arena 生命周期严格绑定 `agent_run()`**：

```text
agent_run() 开始
  └── arena_begin(): malloc 一大块 buffer
      └── 每轮迭代从 arena 分配 context/tools/messages
      └── 不够时返回 AGENT_ERROR_LIMIT，不静默截断
  └── arena_end(): free 整块 + 把 peak 写入 stats
agent_run() 结束
```

**为什么不用每轮 malloc/free**：

- 嵌入式 malloc 碎片风险
- 多次 free 容易漏（错误路径多）
- 无法观测峰值

### 3.4 C 语言教程：对齐（align_up_size）原理

```c
const size_t align = sizeof(void *);          /* 4 或 8 */
return (value + align - 1u) & ~(align - 1u);
```

这是 C 里最常见的"向上对齐到 2 的幂"技巧。以 `align=8` 为例：

- `value=9` → `(9+7)&~7` = `16 & 0xFFFFFFF8` = `16` ✓
- `value=16` → `(16+7)&~7` = `23 & ~7` = `16` ✓（已对齐不变）
- `value=0` → `(0+7)&~7` = `0` ✓

**为什么要对齐**：某些架构（ARM Cortex-M0）访问未对齐的 `void**` 会触发
fault；即使 x86 容忍未对齐，性能也会下降。Arena 的 bump pointer 必须对齐，
否则后续分配的指针可能未对齐。

### 3.5 设计模式：Arena Allocator

**应用场景**：一次 `agent_run` 需要分配 context/tools/messages 多个临时 buffer，
结束后统一释放，且需要观测峰值。

**完整代码示例** + 错误路径管理：

```c
int agent_loop_run(agent_t *agent, const agent_request_t *request,
                   agent_response_t *response)
{
    int ret = arena_begin(agent, estimate_request_arena_size());
    if (ret != AGENT_OK) return ret;

    /* 任何错误路径都必须走 arena_end */
    for (iter = 0u; iter < max_steps; iter++) {
        /* ... */
        if (ret != AGENT_OK) break;
    }

    if (ret == AGENT_OK) {
        arena_end(agent);
        return AGENT_OK;
    }

    /* 错误路径也要释放 */
    arena_end(agent);
    return ret;
}
```

**为什么不用 malloc/free 配对**：见 3.3 末尾。

### 3.6 真实源码解析：arena + loop_buffers 协作

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L160) 的 `allocate_loop_buffers`，看 arena 怎么和 loop 协作：

```c
static int allocate_loop_buffers(agent_t *agent,
                                 char **context,
                                 char **tools,
                                 char **messages)
{
    *context  = (char *)arena_alloc(agent, CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE);
    *tools    = (char *)arena_alloc(agent, CAGENT_TOOL_SCHEMA_BUFFER_SIZE);
    *messages = (char *)arena_alloc(agent, CAGENT_MESSAGES_BUFFER_SIZE);
    if (!*context || !*tools || !*messages) {
        *context = NULL; *tools = NULL; *messages = NULL;
        return AGENT_ERROR_LIMIT;
    }
    return AGENT_OK;
}
```

**关键设计点**：

1. **三个 buffer 一次性从 arena 分配**：不是每轮 malloc，是 loop 开始时分一次，循环里复用
2. **失败时全部置 NULL**：避免 caller 误用半分配状态
3. **arena_alloc 返回 NULL 时调用者返回 LIMIT**：不静默截断，不崩

**今天对照检查**：你的 loop 是"每轮 malloc"还是"loop 开头一次 arena_alloc"？如果是前者，今天就改过来。

### 3.7 真实源码解析：session turn 边界判断

读 [src/memory/session_mgr.c](../src/memory/session_mgr.c#L70) 的 `entry_is_assistant_final` 和
`session_has_unfinished_turn`，这是 turn 淘汰的灵魂：

```c
/* assistant final = role 为 ASSISTANT 且无 tool_calls */
static bool entry_is_assistant_final(const agent_session_entry_t *entry)
{
    return entry && entry->role == AGENT_MESSAGE_ROLE_ASSISTANT
           && entry->tool_call_count == 0u;
}
```

**Turn 定义**：

```text
turn starts: user message
turn ends:   assistant(final) message   ← role=ASSISTANT 且 tool_call_count=0
```

中间可能有：`assistant(tool_calls) → tool → tool → ... → assistant(final)`

**淘汰策略**：

- session 满时只淘汰**完整 turn**（从最老的 user 到对应的 assistant final）
- 不留孤立 `tool` message（缺对应 assistant tool_calls）
- 不留孤立 `assistant(tool_calls)`（缺对应 tool result）
- 尾部未完成 turn 不参与淘汰

**今天必须实现**：`agent_session_evict_oldest_turn`，找到第二个 user message
位置，删掉它之前的所有消息（含对应的 assistant final）。

### 3.8 真实源码解析：budget_retry 循环（提前剧透）

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L296) 的 budget_retry 循环，这是 cAGENT 区别于玩具 Agent
的关键设计：

```c
for (budget_retry = 0u; budget_retry <= CAGENT_MAX_SESSION_MESSAGES; budget_retry++) {
    ret = agent_context_build(agent, request, context, ...);
    if (ret != AGENT_OK) break;

    ret = agent_tool_schema_build(agent, tools_json, ...);
    if (ret != AGENT_OK) break;

    ret = agent_session_build_model_messages(agent, session, messages_json, ...);
    if (is_budget_error(ret)) {
        /* context/messages 太长，淘汰最老 turn 后重试 */
        int evict_ret = agent_session_evict_oldest_turn(agent, session);
        if (evict_ret == AGENT_OK) continue;   /* 重试 */
    }
    if (ret != AGENT_OK) break;

    ret = agent_model_complete(agent->model, &agent->runtime, ...);
    if (is_budget_error(ret)) {
        /* model 也返回 LIMIT（如 token 超限），淘汰重试 */
        int evict_ret = agent_session_evict_oldest_turn(agent, session);
        if (evict_ret == AGENT_OK) continue;
    }
    break;
}
```

**关键设计点**：

1. `is_budget_error` 只匹配 `LIMIT` 和 `CONTEXT_OVERFLOW`，不是所有错误都重试
2. 重试次数有上限（`CAGENT_MAX_SESSION_MESSAGES`），防止无限循环
3. 淘汰失败（没有完整 turn 可淘汰）就 break，不强行

**今天可以先留 TODO**，Day 6 完整实现。但 loop 结构里要给 budget_retry 留位置。

### 3.8.1 真实源码解析：session 消息的 JSON 序列化

读 [src/memory/session_mgr.c](../src/memory/session_mgr.c#L290) 的 `append_json_string`，这是 cAGENT 不依赖 cJSON / jansson 的关键——手写极简 JSON 转义：

```c
static int append_json_string(char *buffer, size_t buffer_size,
                              size_t *used, const char *text)
{
    int err;
    err = append_char(buffer, buffer_size, used, '"');
    if (err != AGENT_OK) return err;

    if (text) {
        while (*text) {
            unsigned char c = (unsigned char)*text++;
            const char *replacement = NULL;

            switch (c) {
            case '\\': replacement = "\\\\"; break;
            case '"':  replacement = "\\\""; break;
            case '\n': replacement = "\\n";  break;
            case '\r': replacement = "\\r";  break;
            case '\t': replacement = "\\t";  break;
            /* \b \f 同理 */
            default:
                if (c < 0x20u) {
                    /* 控制字符用 \uXXXX */
                    snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    replacement = escaped;
                }
                break;
            }
            /* ...写入 buffer... */
        }
    }
    return append_char(buffer, buffer_size, used, '"');
}
```

**为什么手写而不引第三方库**：

1. 嵌入式工具链对 cJSON / jansson 支持参差不齐
2. session 消息只用到 JSON 字符串转义一个特性，引整个库是 over-engineering
3. 手写版可控制在 100 行内，零依赖，编译期确定行为

读 [src/memory/session_mgr.c](../src/memory/session_mgr.c#L370) 的 `append_assistant_tool_calls_message`，看 cAGENT 如何把 entry 数组序列化成 OpenAI 格式：

```c
err = append_raw(buffer, buffer_size, used,
                 "{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[");
for (i = 0u; i < entry->tool_call_count; i++) {
    const agent_session_tool_call_t *call = &entry->tool_calls[i];
    /* {"id":"call_1","type":"function","function":{"name":"echo","arguments":"{}"}} */
}
return append_raw(buffer, buffer_size, used, "]}");
```

注意 `"content":null`——OpenAI 协议要求 assistant tool_calls 消息**必须** content 字段为 null，不能省略，否则部分厂商 API 会报 400。

**今天对照检查**：你的 session 序列化有没有处理这 6 个转义字符？`"content":null` 写对了吗？

### 3.9 验收标准

- [ ] `arena_begin` + `arena_alloc` 三次 + `arena_end` 不泄漏
- [ ] `arena_alloc` 超出 size 返回 NULL
- [ ] `arena_end` 把 peak 写入 `stats.last_run_arena_peak_bytes`
- [ ] `align_up_size` 在 `align=8` 下对 9/16/0 都正确
- [ ] session 能追加 user → assistant(tool_calls) → tool → assistant(final)
- [ ] session 满时 `evict_oldest_turn` 只删完整 turn
- [ ] 不会留下孤立 `tool` message
- [ ] walking_skeleton demo 仍跑通（用真实 session/arena 后行为不变）

### 3.10 对照反思

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L106) 的 arena 部分和 [src/memory/session_mgr.c](../src/memory/session_mgr.c)：

1. 我的 arena 对齐处理对了吗？真实版用 `align_up_size` 做了什么？
2. 我的 turn 淘汰逻辑和真实版一致吗？真实版怎么找"完整 turn"边界？
3. session message 我存的是字符串指针还是拷贝？真实版怎么做的？（看 `copy_string`）
4. `agent_session_drop_unfinished_tail` 这个函数我想到了吗？它解决什么问题？
5. budget_retry 循环我今天留 TODO 了吗？Day 6 补的时候位置对吗？

***

## Day 4: tool/skill registry + mock model 升级

### 4.1 今日目标

把 Day 2 的 tool_registry stub 换成真实实现，并升级 mock model 让它能吐
`tool_calls`，跑通完整的 mock ReAct demo（无网络）。**今天是 Day 2 之后
第二次"看到 Agent 在动"**，且这次有工具调用闭环。

### 4.2 要写的代码

```
src/
├── tools/
│   ├── tools_internal.h       ← agent_tool_entry_t 定义
│   ├── tool_registry.c        ← 真实实现：register/find/unregister/execute
│   └── tool_schema.c          ← 生成 tools JSON schema
├── skills/
│   ├── skills_internal.h
│   └── skill_registry.c       ← skill 注册表
├── core/
│   ├── context_builder.c      ← 补 skills 摘要拼接
│   └── agent_loop.c           ← 补 tool_call 分支
├── llm/
│   └── model_mock.c           ← 升级：支持 step 脚本吐 tool_calls
examples/
└── mock_react/
    └── main.c                 ← mock ReAct demo
```

### 4.3 C 语言教程：固定数组 vs 链表/哈希表

这是 cAGENT 与"通用 C 框架"最大的差异点。**今天必须用固定数组 + 线性查找**，
并在反思日志里回答"为什么不用链表/哈希表"。

**权衡分析**：

| 维度    | 固定数组       | 链表           | 哈希表        |
|---------|----------------|----------------|--------------|
| 内存确定性 | ✅ 编译期确定    | ❌ 运行期碎片      | ❌ 桶+链混合    |
| 嵌入式友好 | ✅ 无 malloc | ❌ 每节点 malloc | ❌ 桶 malloc |
| 查找复杂度 | O(n)，n≤12  | O(n)         | O(1) 平均    |
| 缓存局部性 | ✅ 连续       | ❌ 节点分散       | ❌ 桶分散      |
| 实现复杂度 | ✅ 极低       | 中            | 高          |

cAGENT 选固定数组的理由：**tools/skills/sessions 数量天然有限**（12/8/4），
O(n) 线性查找完全够用，换来零动态分配和编译期可预测。

### 4.4 设计模式：Registry Pattern

**应用场景**：tool/skill/context_provider 都需要"注册→查找→启停"语义，
且数量有上限。

**cAGENT 真实位置**：[src/tools/tool_registry.c](../src/tools/tool_registry.c)

**完整代码示例**（tool registry，参考真实实现）：

```c
/* src/tools/tools_internal.h */
typedef struct {
    agent_tool_t def;       /* 浅拷贝调用者的 tool 定义 */
} agent_tool_entry_t;

/* src/core/agent_internal.h（Day 4 新增字段） */
struct agent {
    /* Day 1-3 字段 ... */
    agent_tool_entry_t tools[CAGENT_MAX_TOOLS];  /* 固定数组 */
    uint32_t tool_count;
    bool tool_schema_dirty;                       /* schema 缓存失效标记 */
    uint32_t tool_schema_version;
    char tool_schema_cache[CAGENT_TOOL_SCHEMA_BUFFER_SIZE];
    size_t tool_schema_cache_len;
};

/* src/tools/tool_registry.c */
int agent_register_tool(agent_t *agent, const agent_tool_t *tool)
{
    agent_tool_entry_t *entry;

    if (!agent || !tool || !tool->name || !tool->execute) {
        return AGENT_ERROR_INVALID;
    }
    if (agent_tool_registry_find(agent, tool->name)) {
        return AGENT_ERROR_INVALID;   /* 重名注册失败 */
    }
    if (agent->tool_count >= CAGENT_MAX_TOOLS) {
        return AGENT_ERROR_LIMIT;     /* 注册表满 */
    }
    entry = &agent->tools[agent->tool_count++];
    memset(entry, 0, sizeof(*entry));
    entry->def = *tool;               /* 浅拷贝 */
    agent_tool_schema_mark_dirty(agent);
    return AGENT_OK;
}

agent_tool_entry_t *agent_tool_registry_find(agent_t *agent, const char *name)
{
    uint32_t i;
    if (!agent || !name) return NULL;
    for (i = 0u; i < agent->tool_count; i++) {
        if (agent->tools[i].def.name &&
            strcmp(agent->tools[i].def.name, name) == 0) {
            return &agent->tools[i];
        }
    }
    return NULL;
}

int agent_unregister_tool(agent_t *agent, const char *name)
{
    uint32_t i;
    for (i = 0u; i < agent->tool_count; i++) {
        if (agent->tools[i].def.name &&
            strcmp(agent->tools[i].def.name, name) == 0) {
            /* memmove 保持数组连续 */
            uint32_t tail = agent->tool_count - i - 1u;
            if (tail > 0u) {
                memmove(&agent->tools[i], &agent->tools[i + 1u],
                        tail * sizeof(agent->tools[0]));
            }
            agent->tool_count--;
            agent_tool_schema_mark_dirty(agent);
            return AGENT_OK;
        }
    }
    return AGENT_ERROR_NOTFOUND;
}
```

**关键设计点**：

- 浅拷贝 `agent_tool_t`，字符串指针由调用者持有（嵌入式不滥用 malloc）
- 用 `tool_schema_dirty` 标记缓存失效，避免每轮重新生成 schema
- `unregister` 用 `memmove` 移动数组，保持连续
- `memset(entry, 0, ...)` 后再赋值，保证 padding 干净

**为什么不用哈希表**：tools 数量 ≤12，线性查找完全够用，且零动态分配。

### 4.5 真实源码解析：dirty 标记 + schema 缓存

读 [src/tools/tool_registry.c](../src/tools/tool_registry.c#L70) 的 `agent_tool_schema_mark_dirty` 和
`agent_tool_schema_build`，看缓存怎么工作：

```c
/* 注册/注销时调 dirty */
void agent_tool_schema_mark_dirty(agent_t *agent)
{
    agent->tool_schema_dirty = true;
    agent->tool_schema_version++;   /* 版本号递增，供外部观察 */
}

/* loop 调用时：dirty 才重建，否则直接用 cache */
int agent_tool_schema_build(agent_t *agent, char *buf, size_t size, size_t *written)
{
    if (agent->tool_schema_dirty) {
        /* 重新遍历 registry 生成 JSON */
        /* ... */
        agent->tool_schema_dirty = false;
    }
    /* 从 cache 拷贝到 buf */
}
```

**为什么这么设计**：ReAct loop 每轮都要把 tools JSON 传给 model，如果每轮
都重新生成（遍历 + JSON 拼接），CPU 浪费。注册表不变化时直接用 cache。

**今天对照检查**：你的 `tool_schema_build` 是"每轮重新生成"还是"dirty 才重建"？
如果是前者，今天就改。

### 4.6 Mock Model 升级：吐 tool_calls

读 [src/llm/model_mock.c](../src/llm/model_mock.c#L44) 的 `select_step` 和 `mock_complete`，看 mock 怎么按
脚本吐 tool_calls：

```c
typedef struct {
    agent_model_mock_step_t *steps;
    size_t step_count;
    size_t cursor;          /* 顺序消费游标 */
    uint32_t call_count;
    int repeat_last;
    int cancel_requested;
    agent_tool_call_t current_call;   /* 复用单条 call，避免 malloc */
} agent_model_mock_t;

static const agent_model_mock_step_t *select_step(agent_model_mock_t *mock)
{
    if (mock->cursor < mock->step_count) {
        return &mock->steps[mock->cursor++];   /* 顺序消费 */
    }
    if (mock->repeat_last) {
        return &mock->steps[mock->step_count - 1u];   /* 重复最后一步 */
    }
    return NULL;   /* 脚本耗尽 */
}

static int mock_complete(void *provider, /* ... */, agent_model_response_t *response)
{
    const agent_model_mock_step_t *step = select_step(mock);
    if (step->type == AGENT_MODEL_MOCK_TOOL_CALL) {
        /* 填充 current_call（复用结构体，不 malloc）*/
        mock->current_call.id = step->tool_call_id;
        mock->current_call.name = step->tool_name;
        mock->current_call.arguments_json = step->arguments_json;
        response->tool_calls = &mock->current_call;
        response->tool_call_count = 1u;
    } else {
        response->content = step->content;
    }
    return AGENT_OK;
}
```

**关键设计点**：

1. **顺序消费游标**：mock 按 step 数组顺序返回，简单可预测
2. **复用 `current_call` 结构体**：不每次 malloc，嵌入式友好
3. **`repeat_last` 模式**：可模拟"模型一直要调工具"触发 max_steps
4. **`cancel_requested`**：mock 自己也支持取消，方便测试 cancel 路径

**今天升级你的 mock**：从 Day 2 的"固定返回 hello"升级到"按 step 脚本返回"。

### 4.7 真实源码解析：loop 的 tool_call 分支

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L377) 的 tool_call 分支，今天必须照抄这个结构：

```c
if (model_response.tool_call_count > 0u) {
    size_t i;

    /* ① 先把 assistant(tool_calls) 写进 session */
    ret = agent_session_add_assistant_tool_calls(agent, session,
                                                 model_response.tool_calls,
                                                 model_response.tool_call_count);
    if (ret != AGENT_OK) break;

    /* ② 逐个执行工具 */
    for (i = 0u; i < model_response.tool_call_count; i++) {
        agent_tool_result_t tool_result;
        const agent_tool_call_t *call = &model_response.tool_calls[i];
        int tool_status;

        /* 检查点：每个工具前都 check_run_state */
        ret = check_run_state(agent);
        if (ret != AGENT_OK) break;

        /* 执行 */
        memset(&tool_result, 0, sizeof(tool_result));
        tool_status = agent_tool_execute(agent, call, &tool_result);

        /* 工具失败也写 JSON 进 session，不崩 loop */
        /* 检查点：工具后也 check */
        ret = check_run_state(agent);
        if (ret != AGENT_OK) break;

        ret = agent_session_add_tool(agent, session, call->id,
                                     tool_result.content_json);
        if (ret != AGENT_OK) break;
    }

    if (ret != AGENT_OK) break;
    continue;   /* 工具调用完，继续下一轮让 model 看结果 */
}

/* final 分支 */
ret = agent_session_add_assistant(agent, session, model_response.content);
if (ret != AGENT_OK) break;
copy_final_output(response, model_response.content);
break;
```

**关键设计点**：

1. **先写 assistant(tool_calls) 再执行工具**：保证 session 历史完整，model 下一轮能看到自己上轮的 tool_calls
2. **每个工具前后都 check_run_state**：工具慢时能被 cancel
3. **工具失败也写 session**：tool_result.content_json 里是 `{"error":...}`，让 model 自己决策
4. **工具调用完 `continue`**：不是 break，是让 model 看到工具结果后继续推理

### 4.8 Tool Execute 执行链

```c
int agent_tool_execute(agent_t *agent, const agent_tool_call_t *call,
                       agent_tool_result_t *result)
{
    agent_tool_entry_t *entry = agent_tool_registry_find(agent, call->name);
    if (!entry) {
        /* 工具级错误：写 JSON 让模型继续，不崩 loop */
        snprintf(result->content_json, result->content_json_size,
                 "{\"error\":\"tool_not_found\",\"name\":\"%s\"}", call->name);
        return AGENT_ERROR_NOTFOUND;
    }
    if (!agent_tool_entry_is_enabled(entry)) {
        snprintf(result->content_json, result->content_json_size,
                 "{\"error\":\"tool_disabled\"}");
        return AGENT_ERROR_INVALID;
    }
    /* policy check（Day 6 完善）*/
    if (agent->policy_cb) {
        int decision = agent->policy_cb(call, agent->policy_user_data);
        if (decision == AGENT_POLICY_DENY) {
            snprintf(result->content_json, result->content_json_size,
                     "{\"error\":\"policy_denied\"}");
            return AGENT_ERROR_POLICY_DENIED;
        }
    }
    /* 执行 handler */
    return entry->def.execute(call, result, entry->def.user_data);
}
```

### 4.8.1 真实源码解析：tool_schema 的 dirty cache 模式

读 [src/tools/tool_schema.c](../src/tools/tool_schema.c#L434) 的 `agent_tool_schema_build`，这是 Day 4 提到 `tool_schema_dirty` 字段的完整实现：

```c
int agent_tool_schema_build(agent_t *agent, char *buffer,
                            size_t buffer_size, size_t *written)
{
    int ret;
    size_t cache_len = 0u;

    if (!agent || !buffer || buffer_size == 0u) {
        return AGENT_ERROR_INVALID;
    }

    /* ① 缓存有效：直接拷贝，不重新生成 */
    if (!agent->tool_schema_dirty && agent->tool_schema_cache_len > 0u) {
        return copy_schema_cache(agent, buffer, buffer_size, written);
    }

    /* ② 缓存失效：重新生成 schema 写入 cache */
    ret = build_schema_uncached(agent,
                                agent->tool_schema_cache,
                                sizeof(agent->tool_schema_cache),
                                &cache_len);
    if (ret != AGENT_OK) {
        buffer[0] = '\0';
        return ret;
    }

    /* ③ 更新 cache 元数据，清除 dirty */
    agent->tool_schema_cache_len = cache_len;
    agent->tool_schema_dirty = false;

    return copy_schema_cache(agent, buffer, buffer_size, written);
}
```

```c
/* 注册表变更时调用 */
void agent_tool_schema_mark_dirty(agent_t *agent)
{
    if (!agent) return;
    agent->tool_schema_dirty = true;
    agent->tool_schema_version++;   /* 版本号，便于上层检测变化 */
}
```

**关键设计点**：

1. **dirty 标记触发重生成**：`register_tool` / `unregister_tool` / `tool_set_enabled` 都调 `mark_dirty`
2. **cache 存在 `agent` 内部**：`tool_schema_cache[CAGENT_TOOL_SCHEMA_BUFFER_SIZE]`，避免每次 loop 调用都重新拼 JSON
3. **`tool_schema_version` 单调递增**：上层可用版本号判断"自上次 run 后 tool 集合是否变化"
4. **缓存命中只 memcpy**：热路径（每轮 loop 都 build schema）从 O(n × 工具数) 降到 O(cache_len)

**今天对照检查**：你的 `agent_tool_schema_build` 有没有 dirty cache？没有的话每轮 loop 都重新生成 JSON，性能差 10 倍以上。

### 4.9 Mock ReAct Demo

```c
/* examples/mock_react/main.c */
int main(void)
{
    agent_model_mock_step_t steps[] = {
        { .type = AGENT_MODEL_MOCK_TOOL_CALL,
          .tool_call_id = "call_1",
          .tool_name = "demo_echo",
          .arguments_json = "{\"text\":\"hi\"}" },
        { .type = AGENT_MODEL_MOCK_FINAL,
          .content = "echo done" }
    };
    agent_model_mock_config_t cfg = {
        .steps = steps, .step_count = 2, .repeat_last = 0
    };

    agent_t *agent = agent_create(NULL);
    agent_model_t *model = agent_model_mock_create(&cfg, NULL);
    agent_set_model(agent, model);

    agent_register_tool_simple(agent, "demo_echo", "echo input",
                                NULL, demo_echo_handler, NULL,
                                AGENT_TOOL_FLAG_LLM_VISIBLE);

    char output[4096];
    int ret = agent_run_simple(agent, "echo hi", output, sizeof(output));
    printf("ret=%d output=%s\n", ret, output);   /* 期望: ret=0 output=echo done */

    agent_destroy(agent);
    return ret == AGENT_OK ? 0 : 1;
}
```

### 4.10 验收标准

- [ ] 注册 3 个工具后 `agent_tool_registry_find` 能找到
- [ ] 重名注册返回 `AGENT_ERROR_INVALID`
- [ ] 注册第 13 个工具返回 `AGENT_ERROR_LIMIT`
- [ ] `unregister` 后 `tool_count` 正确递减
- [ ] `tool_schema_dirty` 标记工作：注册后 dirty，build 后清除
- [ ] mock ReAct demo 输出 `call_1 → tool → final`，退出码 0
- [ ] tool not found 时不崩 loop，写 JSON 让模型继续
- [ ] skill 和 context_provider 注册表同样可用

### 4.11 对照反思

读 [src/tools/tool_registry.c](../src/tools/tool_registry.c) 和 [src/llm/model_mock.c](../src/llm/model_mock.c)：

1. 我有没有手贱去写链表？为什么 cAGENT 不用？
2. 我的 `unregister` 用了 `memmove` 吗？还是用了"标记删除"？真实版怎么做的？
3. `tool_schema_dirty` 这个字段我想到了吗？没有的话每次 loop 都会重新生成 schema
4. 我的 mock 是"固定返回"还是"step 脚本"？真实版支持 `repeat_last`，我用了吗？
5. loop 的 tool_call 分支：我先写 assistant(tool_calls) 再执行工具了吗？

***

## Day 5: OpenAI Adapter 与 ops vtable

### 5.1 今日目标

把 Day 2 的 mock model 升级路径走通——实现真实 OpenAI-compatible HTTP adapter，
掌握 ops vtable 多态和三级错误分层。

### 5.2 要写的代码

```
src/
├── llm/
│   ├── model_openai.c       ← OpenAI-compatible adapter
│   └── llm_parse.c          ← 完整 JSON 解析（补 Day 2 的简化版）
```

### 5.3 C 语言教程：Ops Vtable（C 语言多态）

cAGENT 的 model provider 需要支持多个厂商（OpenAI、Anthropic、本地模型），
但 core 不能绑定任何一个。**今天必须掌握 ops vtable 写法**。

```c
/* include/cagent/model.h */
typedef struct agent_model agent_model_t;  /* opaque */

typedef struct {
    int  (*complete)(void *provider,
                     agent_runtime_t *runtime,
                     const agent_model_request_t *request,
                     agent_model_response_t *response);
    int  (*cancel)(void *provider);
    void (*destroy)(void *provider);
} agent_model_ops_t;

agent_model_t *agent_model_create(const agent_model_ops_t *ops, void *provider);
int agent_model_complete(agent_model_t *model, ...);
```

```c
/* src/core/agent_internal.h */
struct agent_model {
    agent_model_ops_t ops;    /* vtable */
    void *provider;           /* 具体实现的状态，core 不理解 */
};
```

```c
/* src/llm/model.c —— facade */
int agent_model_complete(agent_model_t *model,
                         agent_runtime_t *runtime,
                         const agent_model_request_t *request,
                         agent_model_response_t *response)
{
    if (!model || !model->ops.complete) return AGENT_ERROR_NOTSUP;
    return model->ops.complete(model->provider, runtime, request, response);
}
```

**为什么不用 C++ 虚函数 / switch-case**：

- C++ 虚函数在嵌入式工具链支持有限
- switch-case 每加一个 provider 要改 core，违背开闭原则
- ops vtable 是 C 语言多态的标准写法，零运行时开销，可静态初始化

### 5.4 C 语言教程：三级错误分层

cAGENT 的错误处理不是"全部用错误码"，而是分三级。**今天必须区分清楚**。

**第一级：致命错误（用 `agent_error_t` 错误码）**

```c
typedef enum {
    AGENT_OK = 0,
    AGENT_ERROR_NOMEM = -2,
    AGENT_ERROR_BUSY = -4,           /* 同一 agent 重入 */
    AGENT_ERROR_LIMIT = -5,          /* 迭代/buffer 上限 */
    AGENT_ERROR_TIMEOUT = -6,
    AGENT_ERROR_CANCELLED = -7,
    AGENT_ERROR_CONTEXT_OVERFLOW = -8, /* 不可静默截断 */
    AGENT_ERROR_MODEL = -9,
    AGENT_ERROR_NETWORK = -10,
    /* ... */
} agent_error_t;
```

**第二级：工具级错误（写进 tool result JSON，让模型继续推理）**

```text
tool not found → {"error":"tool_not_found","name":"..."}
policy denied  → {"error":"policy_denied"}
tool handler 失败 → {"error":"handler_failed","detail":"..."}
```

关键：**不要因为单个工具失败就崩掉整个 loop**。模型看到 tool error 后可以
选择换一个工具、道歉、或直接回答。这是 ReAct 的核心鲁棒性来源。

**第三级：context overflow（不可静默截断）**

```c
/* 错误做法：截断 system prompt */
if (len > buffer_size) {
    memcpy(buffer, prompt, buffer_size - 1);
    return AGENT_OK;  /* ← 错！丢失安全指令 */
}

/* cAGENT 做法：返回错误，让上层决策 */
if (len > buffer_size) {
    return AGENT_ERROR_CONTEXT_OVERFLOW;
}
```

**为什么不静默截断**：system prompt 可能包含安全约束
（"不要执行危险操作"），截断后模型可能失控。

### 5.5 设计模式：Adapter Pattern

**应用场景**：把 OpenAI 协议适配到 cAGENT 的 `agent_model_ops_t` 接口。

**cAGENT 真实位置**：[src/llm/model_openai.c](../src/llm/model_openai.c)

**完整代码框架**：

```c
/* src/llm/model_openai.c */
typedef struct {
    char host[64];
    char api_key[128];
    char model[64];
} openai_provider_t;

static int openai_complete(void *provider, agent_runtime_t *runtime,
                           const agent_model_request_t *request,
                           agent_model_response_t *response)
{
    openai_provider_t *p = (openai_provider_t *)provider;
    char request_body[CAGENT_HTTP_REQUEST_BUFFER_SIZE];
    char response_body[CAGENT_HTTP_RESPONSE_BUFFER_SIZE];

    /* 1. 构建 OpenAI request JSON */
    build_openai_request(request_body, sizeof(request_body),
                         request, p->model);

    /* 2. 调 runtime.http_post（注意传 timeout_ms = 检查点 3）*/
    agent_http_request_t http_req = {
        .method = "POST",
        .url = "https://api.openai.com/v1/chat/completions",
        .host = p->host,
        .path = "/v1/chat/completions",
        .headers = build_auth_header(p->api_key),
        .body = request_body,
        .body_size = strlen(request_body),
        .timeout_ms = request->timeout_ms,   /* 检查点 3 */
    };
    agent_http_response_t http_resp = {
        .body = response_body,
        .body_size = sizeof(response_body),
    };
    int ret = runtime->http_post(&http_req, &http_resp, runtime->user_data);
    if (ret != AGENT_OK) {
        response->status = AGENT_ERROR_NETWORK;
        return AGENT_ERROR_NETWORK;
    }

    /* 3. 解析响应 */
    return llm_parse_openai_response(http_resp.body, response);
}

static const agent_model_ops_t OPENAI_OPS = {
    .complete = openai_complete,
    .cancel   = NULL,    /* HTTP 不支持取消 */
    .destroy  = openai_destroy,
};

agent_model_t *agent_model_openai_create(const char *host,
                                          const char *api_key,
                                          const char *model)
{
    openai_provider_t *p = malloc(sizeof(*p));
    /* ... 填充 host/api_key/model ... */
    return agent_model_create(&OPENAI_OPS, p);
}
```

### 5.6 真实源码解析：mock 与 openai 共享同一 ops 接口

读 [src/llm/model_mock.c](../src/llm/model_mock.c#L120) 末尾的 `MOCK_OPS` 和 openai 的 `OPENAI_OPS`，
对比两者如何挂在同一个 vtable 上：

```c
/* mock */
static const agent_model_ops_t MOCK_OPS = {
    .complete = mock_complete,
    .cancel   = mock_cancel,      /* mock 支持取消 */
    .destroy  = mock_destroy,
};

/* openai */
static const agent_model_ops_t OPENAI_OPS = {
    .complete = openai_complete,
    .cancel   = NULL,             /* HTTP 不支持取消 */
    .destroy  = openai_destroy,
};
```

**关键设计点**：

1. **两个 provider 共享 `agent_model_complete` facade**：core 只调 facade，不知道是 mock 还是 openai
2. **`cancel` 可以为 NULL**：facade 里 `if (!model->ops.cancel) return AGENT_ERROR_NOTSUP`，单线程 HTTP 不支持取消是合理的
3. **静态初始化 vtable**：`static const`，零运行时开销，编译期确定

**今天对照检查**：你的 `agent_model_complete` facade 有没有 NULL 安全检查？
你的 openai_ops 和 mock_ops 能不能无感切换？

### 5.6.1 真实源码解析：build_request_json 的四分支组合

读 [src/llm/model_openai.c](../src/llm/model_openai.c#L182) 的 `build_request_json`，cAGENT 根据"有没有 messages_json"和"有没有 tools_json"组合出 4 种 JSON 模板：

```c
if (tools_json && tools_json[0] != '\0' && strcmp(tools_json, "[]") != 0) {
    if (has_messages_json) {
        /* ① 有 tools + 有历史：messages 用 [system, *历史] */
        snprintf(buffer, buffer_size,
                 "{\"model\":\"%s\",\"messages\":["
                 "{\"role\":\"system\",\"content\":\"%s\"},%.*s],"
                 "\"tools\":%s,\"tool_choice\":\"auto\","
                 "\"max_tokens\":%u}",
                 provider->model, context,
                 (int)messages_body_len, messages_body,   /* 去掉外层 [] */
                 tools_json, max_tokens);
    } else {
        /* ② 有 tools + 无历史：messages 用 [system, user] */
        snprintf(buffer, buffer_size,
                 "{\"model\":\"%s\",\"messages\":["
                 "{\"role\":\"system\",\"content\":\"%s\"},"
                 "{\"role\":\"user\",\"content\":\"%s\"}],"
                 "\"tools\":%s,\"tool_choice\":\"auto\","
                 "\"max_tokens\":%u}",
                 provider->model, context, input, tools_json, max_tokens);
    }
} else {
    /* ③ ④ 无 tools：省略 tools/tool_choice 字段 */
}
```

**关键设计点**：

1. **`messages_body = messages_json + 1` 去掉外层 `[]`**：session 给的是 `[{...},{...}]`，OpenAI 协议要的是 `[system, {...},{...}]`，所以剥掉外层括号用 `%.*s` 嵌入
2. **空 tools 判断 `strcmp(tools_json, "[]") != 0`**：避免发送 `"tools":[]` 让部分 API 报错
3. **`max_tokens` 用 `request->max_output_tokens ? : CAGENT_DEFAULT_MAX_OUTPUT_TOKENS`**：caller 不传走默认
4. **`tool_choice: "auto"`**：让模型自己决定调不调工具，是 OpenAI 协议的推荐值

**今天对照检查**：你的 `build_request_json` 有没有处理这 4 个分支？`messages_body` 去掉外层 `[]` 的技巧你写对了吗？

### 5.6.2 真实源码解析：extract_tool_calls 的就地解析

读 [src/llm/model_openai.c](../src/llm/model_openai.c#L359) 的 `extract_tool_calls`，cAGENT 不拷贝 JSON 子串，而是**就地修改 response buffer**插入 `\0` 分隔：

```c
static int extract_tool_calls(openai_compat_provider_t *provider,
                              char *json, size_t *count)
{
    char *array = find_key(json, "\"tool_calls\"");
    char *p = skip_ws(array + 1);

    while (p && *p && *p != ']') {
        char *item_start = p;
        char *item_end = find_matching_json_end(item_start);   /* 找匹配的 } */
        char saved = item_end[1];
        item_end[1] = '\0';                  /* 临时切断，让 parse_tool_call_item 处理 */
        ret = parse_tool_call_item(provider, item_start, parsed);
        item_end[1] = saved;                 /* 恢复 */
        parsed++;
        p = skip_ws(item_end + 1);
        if (*p == ',') p = skip_ws(p + 1);
    }
}
```

```c
static int parse_tool_call_item(openai_compat_provider_t *provider,
                                char *item, size_t index)
{
    char *id   = find_key(item, "\"id\"");
    char *name = find_key(item, "\"name\"");
    char *args = find_key(item, "\"arguments\"");

    /* copy_json_value 把值拷到 provider->tool_call_ids[index] 等固定数组 */
    ret = copy_json_value(provider->tool_call_ids[index],
                          sizeof(provider->tool_call_ids[index]), id);
    /* ... name / args 同理 ... */

    /* 指针指向 provider 内部固定数组，避免返回悬挂指针 */
    provider->tool_calls[index].id = provider->tool_call_ids[index];
    provider->tool_calls[index].name = provider->tool_call_names[index];
    provider->tool_calls[index].arguments_json = provider->tool_call_args[index];
}
```

**关键设计点**：

1. **`item_end[1] = '\0'` 就地切断**：不 malloc 子串，零拷贝解析，适合嵌入式
2. **解析后 `item_end[1] = saved` 恢复**：保持原 buffer 完整，后续还能解析其他字段
3. **`tool_calls` 数组存在 provider 内部**：`response->tool_calls = provider->tool_calls`，避免返回栈指针
4. **固定上限 `CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS`**：超限返回 `LIMIT`，不动态扩容

**今天对照检查**：你的 `extract_tool_calls` 是"malloc 子串"还是"就地切断"？返回的 `tool_calls` 数组存在哪？provider destroy 时会不会泄漏？

### 5.7 JSON 契约设计

**OpenAI request 格式**：

```json
{
  "model": "deepseek-chat",
  "messages": [
    {"role": "system", "content": "<context>"},
    {"role": "user", "content": "..."},
    {"role": "assistant", "tool_calls": [{"id":"call_1","type":"function","function":{"name":"echo","arguments":"{}"}}]},
    {"role": "tool", "tool_call_id": "call_1", "content": "{\"ok\":true}"}
  ],
  "tools": [{"type":"function","function":{"name":"echo","description":"...","parameters":{...}}}],
  "max_tokens": 512
}
```

**tool result JSON 契约**（cAGENT 约定）：

```text
成功且无数据：{"ok":true}
成功且有数据：{"ok":true,"value":"..."}
工具未找到：  {"error":"tool_not_found","name":"..."}
策略拒绝：    {"error":"policy_denied"}
handler 失败：{"error":"handler_failed","detail":"..."}
```

### 5.8 验收标准

- [ ] 调用 deepseek/OpenAI 真实 API 成功返回 final content
- [ ] 模型返回 tool_call 时能正确执行本地工具并回填
- [ ] HTTP 失败返回 `AGENT_ERROR_NETWORK` 不崩
- [ ] response 解析失败返回 `AGENT_ERROR_PARSE`
- [ ] tool not found 写 JSON 让模型继续，不崩 loop
- [ ] context overflow 返回 `AGENT_ERROR_CONTEXT_OVERFLOW` 而非截断
- [ ] `timeout_ms` 正确传给 `runtime->http_post`（检查点 3）

### 5.9 对照反思

读 [src/llm/model_openai.c](../src/llm/model_openai.c) 和 [src/llm/llm_parse.c](../src/llm/llm_parse.c)：

1. 我的 OpenAI request JSON 构建和真实版差异？
2. 我的 tool result JSON 契约和真实版一致吗？
3. 真实版怎么处理 HTTP 超时？我传 `timeout_ms` 给 runtime 了吗？
4. 真实版 `llm_parse` 支持哪些响应格式？我只支持了 final + tool_calls 吗？
5. 我的 openai_ops 和 mock_ops 能不能在同一个 agent 上无感切换？

***

## Day 6: 边界加固：cancel / timeout / 三级错误

### 6.1 今日目标

补全所有边界检查：5 道检查点完整版、cooperative cancel、context overflow、
budget_retry 循环。今天完成后 cAGENT 的"灵魂"才真正落地。

### 6.2 要写的代码

```
src/
├── tools/
│   └── tool_guard.c          ← 输入/输出大小检查、副作用限流
├── core/
│   ├── agent_loop.c          ← 补 5 道检查点 + budget_retry 完整版
│   └── context_builder.c     ← 补 overflow 检测 + provider priority
```

### 6.3 设计模式：Cooperative Cancel（完整版）

**应用场景**：外部线程需要中断正在执行的 `agent_run`，但 C 语言没有原生抢占。

**完整代码示例**：

```c
/* src/core/agent_core.c */
int agent_cancel(agent_t *agent)
{
    uint32_t state;
    if (!agent) return AGENT_ERROR_INVALID;

    /* 临界区保护，ISR 安全 */
    state = agent_runtime_enter_critical(&agent->runtime);
    agent->cancel_requested = true;
    agent_runtime_exit_critical(&agent->runtime, state);

    /* 如果 model 支持 cancel，通知 provider */
    agent_model_cancel(agent->model);
    return AGENT_OK;
}
```

```c
/* src/core/agent_loop.c —— 在每个检查点调用 */
static int check_run_state(agent_t *agent)
{
    uint64_t now;

    if (!agent) return AGENT_ERROR_INVALID;

    if (agent->cancel_requested) {
        return AGENT_ERROR_CANCELLED;
    }

    now = agent_runtime_now_ms(&agent->runtime);
    if (agent->run_deadline_ms != 0u && now > agent->run_deadline_ms) {
        return AGENT_ERROR_TIMEOUT;
    }

    return AGENT_OK;
}
```

**为什么不用 preemptive cancel（pthread_kill / TaskAbort）**：

- 正在阻塞的 HTTP 调用被强杀会留下半开连接、泄漏 buffer
- 嵌入式 RTOS 的 task abort 不保证资源清理
- cooperative cancel 在检查点退出，状态一致

**局限**：如果 model provider 的 HTTP 调用不支持 timeout，cancel 只能等
HTTP 返回后在下一个检查点生效。这是 cAGENT 的明确取舍。

### 6.4 Bounded Loop：5 道检查点（cAGENT 灵魂）

**今天必须严格实现这 5 道检查点**：

| # | 检查点                    | 触发位置                                       | 失败返回                    |
|---|---------------------------|------------------------------------------------|-----------------------------|
| 1 | `max_steps`               | 每轮迭代开始                                   | `AGENT_ERROR_LIMIT`         |
| 2 | `timeout_ms`（整体）      | `check_run_state()` 用 `run_deadline_ms` 比较  | `AGENT_ERROR_TIMEOUT`       |
| 3 | `per_model_timeout_ms`    | 传给 `agent_model_complete()`                  | `AGENT_ERROR_TIMEOUT`       |
| 4 | `per_tool_timeout_ms`     | 传给 `agent_tool_execute()`                    | `AGENT_ERROR_TIMEOUT`       |
| 5 | `cancel_requested`        | `check_run_state()` + 工具循环                 | `AGENT_ERROR_CANCELLED`     |

### 6.5 真实源码解析：5 道检查点的真实位置

读 [src/core/agent_loop.c](../src/core/agent_loop.c)，对照检查点的真实位置：

```c
/* 检查点 1：max_steps —— for 循环条件 */
for (iter = 0u; iter < agent->limits.max_steps; iter++) {

    /* 检查点 2 + 5：check_run_state（timeout + cancel）*/
    ret = check_run_state(agent);
    if (ret != AGENT_OK) break;

    /* budget_retry 循环内部 */
    for (budget_retry = 0u; budget_retry <= CAGENT_MAX_SESSION_MESSAGES; budget_retry++) {
        ret = agent_context_build(agent, request, context, ...);
        if (ret != AGENT_OK) break;

        ret = agent_tool_schema_build(agent, tools_json, ...);
        if (ret != AGENT_OK) break;

        ret = agent_session_build_model_messages(agent, session, messages_json, ...);
        if (is_budget_error(ret)) {
            /* 检查点 4 的兄弟：context overflow 触发 turn 淘汰重试 */
            int evict_ret = agent_session_evict_oldest_turn(agent, session);
            if (evict_ret == AGENT_OK) continue;   /* 重试 */
        }
        if (ret != AGENT_OK) break;

        /* 检查点 3：per_model_timeout_ms 通过 model_request.timeout_ms 传下去 */
        memset(&model_request, 0, sizeof(model_request));
        model_request.timeout_ms = agent->limits.per_model_timeout_ms;
        ret = agent_model_complete(agent->model, &agent->runtime, ...);
        if (is_budget_error(ret)) {
            int evict_ret = agent_session_evict_oldest_turn(agent, session);
            if (evict_ret == AGENT_OK) continue;
        }
        break;
    }

    /* tool_call 分支：每个工具前后都调用 check_run_state（检查点 5） */
    for (i = 0u; i < model_response.tool_call_count; i++) {
        ret = check_run_state(agent);          /* 工具前 */
        if (ret != AGENT_OK) break;
        /* 检查点 4：per_tool_timeout_ms 通过 tool_execute 入参传下去 */
        tool_status = agent_tool_execute(agent, call, &tool_result);
        ret = check_run_state(agent);          /* 工具后 */
        if (ret != AGENT_OK) break;
    }
}
```

**今天必须逐条对照实现的 5 道检查点**：

| # | 检查点                 | 真实位置（行号近似）                              | 我的实现 |
|---|------------------------|---------------------------------------------------|----------|
| 1 | `max_steps`            | `for (iter = 0u; iter < agent->limits.max_steps; iter++)` | ☐ |
| 2 | `timeout_ms`（整体）   | `check_run_state` 里 `now > run_deadline_ms`      | ☐ |
| 3 | `per_model_timeout_ms` | `model_request.timeout_ms = agent->limits.per_model_timeout_ms` | ☐ |
| 4 | `per_tool_timeout_ms`  | `agent_tool_execute` 内部传给 tool handler        | ☐ |
| 5 | `cancel_requested`     | `check_run_state` + tool 循环前后                 | ☐ |

### 6.6 真实源码解析：budget_retry 完整循环

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L241) 的 budget_retry 完整实现。这是 Day 3 留的 TODO，今天必须填上：

```c
for (budget_retry = 0u;
     budget_retry <= CAGENT_MAX_SESSION_MESSAGES;
     budget_retry++) {
    ret = agent_context_build(agent, request,
                              context, CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE, NULL);
    if (ret != AGENT_OK) {
        break;   /* 非预算错误，直接退出 budget_retry */
    }

    ret = agent_tool_schema_build(agent,
                                  tools_json, CAGENT_TOOL_SCHEMA_BUFFER_SIZE, NULL);
    if (ret != AGENT_OK) {
        break;
    }

    ret = agent_session_build_model_messages(agent, session,
                                             messages_json, CAGENT_MESSAGES_BUFFER_SIZE, NULL);
    if (is_budget_error(ret)) {
        int evict_ret = agent_session_evict_oldest_turn(agent, session);
        if (evict_ret == AGENT_OK) {
            continue;   /* 淘汰成功，重试 build */
        }
    }
    if (ret != AGENT_OK) {
        break;
    }

    /* model.complete */
    model_request.timeout_ms = agent->limits.per_model_timeout_ms;
    ret = agent_model_complete(agent->model, &agent->runtime,
                               &model_request, &model_response);
    if (is_budget_error(ret)) {
        int evict_ret = agent_session_evict_oldest_turn(agent, session);
        if (evict_ret == AGENT_OK) {
            continue;   /* 模型说上下文太长，淘汰重试 */
        }
    }
    break;   /* 非 budget 错误或成功，退出 budget_retry */
}
```

**关键设计点**：

1. **`is_budget_error` 只匹配 `LIMIT` 和 `CONTEXT_OVERFLOW`**：网络错误、parse 错误不重试，直接退出
2. **重试上限是 `CAGENT_MAX_SESSION_MESSAGES`**：每个 session 消息最多淘汰一次，防止无限循环
3. **淘汰失败立即 `break`**：没有完整 turn 可淘汰时，`evict_oldest_turn` 返回 `LIMIT`，不再重试
4. **`break` 在末尾而非 `if`**：成功路径也要 break，避免无谓的二次循环

**今天对照检查**：你的 budget_retry 是"只在 model 返回 LIMIT 时淘汰"还是"context_build / session_build / model_complete 三处都淘汰"？真实版是三处都淘汰。

### 6.6.1 真实源码解析：检查点 3/4 的 timeout_ms 传递路径

很多读者会问："检查点 3 写了 `model_request.timeout_ms = per_model_timeout_ms`，但 model 真的会用吗？" 答案在两处实现里。

读 [src/llm/model_openai.c](../src/llm/model_openai.c#L420) 的 `openai_complete`，看 timeout 如何穿透到 HTTP 层：

```c
static int openai_complete(void *provider, agent_runtime_t *rt,
                           const agent_model_request_t *req,
                           agent_model_response_t *resp)
{
    /* ... build_request_json ... */

    /* 把 timeout_ms 透传给 HTTP runtime */
    ret = rt->http_post(rt,
                        provider->endpoint,
                        provider->headers,
                        request_json,
                        req->timeout_ms,           /* ← 检查点 3 在这里生效 */
                        response_buf,
                        response_buf_size,
                        &written);

    if (ret == AGENT_ERROR_TIMEOUT) {
        /* HTTP 超时返回 TIMEOUT，loop 在 check_run_state 也能立刻退出 */
        return AGENT_ERROR_TIMEOUT;
    }
    /* ... parse response ... */
}
```

注意：**HTTP 超时是 cooperative cancel 的关键配合**。如果 model provider 的 HTTP 不支持 timeout（比如用了不支持 select 的 RTOS socket），cancel 只能等 HTTP 自然返回，loop 里的 deadline 检查就形同虚设。所以 cAGENT 在 `agent_runtime_openvela_fill` 里会强制让 `http_post` 用 mbedTLS 的 `mbedtls_ssl_read` 带 timeout。

读 [src/tools/tool_guard.c](../src/tools/tool_guard.c#L182) 的 `agent_tool_execute`，看检查点 4 的真实实现——**post-execution 检查**，不是 preemptive：

```c
int agent_tool_execute(agent_t *agent, const agent_tool_call_t *call,
                       agent_tool_result_t *result)
{
    /* ① 优先用 tool 自带 timeout，否则用 limits.per_tool_timeout_ms */
    effective_timeout_ms = entry->def.timeout_ms ? entry->def.timeout_ms
                                                 : agent->limits.per_tool_timeout_ms;
    start_ms = agent_runtime_now_ms(&agent->runtime);

    /* ② 同步调用 handler，C 语言无法抢占 */
    ret = entry->def.execute(call, result, entry->def.user_data);

    end_ms = agent_runtime_now_ms(&agent->runtime);

    /* ③ 事后检查：超时则改写 result 为 error，loop 继续 */
    if (effective_timeout_ms > 0u &&
        end_ms >= start_ms &&
        end_ms - start_ms > effective_timeout_ms) {
        return set_tool_error(agent, result, AGENT_ERROR_TIMEOUT, "tool timeout");
    }
    /* ... */
}
```

**关键设计点**：

1. **`effective_timeout_ms` 优先级**：`tool->def.timeout_ms > agent->limits.per_tool_timeout_ms`，让单个工具能 override
2. **post-execution 检查而非 preemptive**：C 语言没有原生抢占，只能在 handler 返回后判断"实际用了多久"
3. **超时改写 `result->content_json` 为 `{"error":"tool timeout"}`**：loop 不崩，model 下一轮能看到工具超时，自己决策
4. **`set_tool_error` 三件事**：写 `result->error_message` + 写 `result->content_json`（给 model 看）+ 返回错误码（给 loop 看）

**今天对照检查**：你的 `agent_tool_execute` 是"事后检查超时"还是"事前 setjmp/longjmp"？如果是后者，嵌入式 RTOS 上很可能不可移植，cAGENT 的做法是事后检查 + 写 JSON 让 model 继续。

### 6.7 真实源码解析：context overflow 的三道防线

cAGENT 防止"静默截断 system prompt"有三道防线，今天必须全部就位：

**第一道：context_builder 的 `append_bytes` 返回 `CONTEXT_OVERFLOW`**

读 [src/core/context_builder.c](../src/core/context_builder.c#L20)：

```c
static int append_bytes(char *buffer, size_t buffer_size, size_t *used,
                        const char *text, size_t len)
{
    if (*used + len >= buffer_size) {
        if (buffer_size > 0u) {
            buffer[*used < buffer_size ? *used : buffer_size - 1u] = '\0';
        }
        return AGENT_ERROR_CONTEXT_OVERFLOW;   /* 不截断 */
    }
    memcpy(buffer + *used, text, len);
    *used += len;
    buffer[*used] = '\0';
    return AGENT_OK;
}
```

注意：返回 `CONTEXT_OVERFLOW` 前**仍然把 `*used` 位置的字符置 `\0`**，保证 buffer 始终是合法 C 字符串，调用者不会读到未终止的内存。

**第二道：context_builder 对 critical provider 传播错误，对非 critical provider 静默跳过**

读 [src/core/context_builder.c](../src/core/context_builder.c#L171) 的 `append_one_provider`：

```c
ret = provider->build(buffer + *used, buffer_size - *used, &written, ...);
if (ret != AGENT_OK) {
    *used = before;            /* 回滚到 provider 之前的状态 */
    buffer[before] = '\0';
    return provider_is_critical(provider) ? ret : AGENT_OK;
}
```

含义：critical provider（如安全约束）失败必须传播，非 critical（如天气信息）失败就跳过继续。这是 cAGENT 的"安全优先"原则。

**第三道：agent_loop 的 budget_retry 触发 turn 淘汰**

见 6.6 节，当 `agent_context_build` 返回 `CONTEXT_OVERFLOW` 时，`is_budget_error` 返回 true，触发淘汰最老 turn 重试。三道防线协同：**第一道检测 → 第二道区分严重性 → 第三道自救**。

### 6.8 真实源码解析：tool_call 分支的 cancel 检查点

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L316) 的 tool_call 执行循环，这是检查点 5 最密集的位置：

```c
for (i = 0u; i < model_response.tool_call_count; i++) {
    const agent_tool_call_t *call = &model_response.tool_calls[i];
    int tool_status;

    /* 检查点 5a：工具执行前 */
    ret = check_run_state(agent);
    if (ret != AGENT_OK) {
        break;
    }

    agent_event_emit(agent, AGENT_EVENT_TOOL_CALL, request, iter + 1u,
                     AGENT_OK, "call", call->name, call->id);

    memset(&tool_result, 0, sizeof(tool_result));
    /* 检查点 4：per_tool_timeout_ms 在 tool_execute 内部生效 */
    tool_status = agent_tool_execute(agent, call, &tool_result);

    agent_event_emit(agent, AGENT_EVENT_TOOL_RESULT, request, iter + 1u,
                     tool_status, tool_result.error_message,
                     call->name, call->id);

    /* 检查点 5b：工具执行后 */
    ret = check_run_state(agent);
    if (ret != AGENT_OK) {
        break;
    }

    ret = agent_session_add_tool(agent, session, call->id,
                                 tool_result.content_json);
    if (ret != AGENT_OK) {
        break;
    }
}
```

**关键设计点**：

1. **工具前后各检查一次 cancel**：长时间工具（如 HTTP 调用）结束后能立即响应取消
2. **`tool_status` 和 `ret` 分开**：工具自身失败（`tool_status`）写进 tool_result JSON 让模型继续；loop 控制（`ret`）失败才 break loop
3. **`agent_session_add_tool` 失败也要 break**：session 写不进去就别继续，否则下一轮 model 看到的历史不完整

**今天对照检查**：你的 tool 循环有没有"前后各一次 check_run_state"？工具失败时是"写 JSON 继续"还是"break loop"？真实版是前者。

### 6.9 真实源码解析：错误路径的 drop_unfinished_tail

读 [src/core/agent_loop.c](../src/core/agent_loop.c#L426) 的收尾逻辑，这是 Day 3 提到但今天才完整理解的设计：

```c
if (ret == AGENT_OK) {
    arena_end(agent);
    return AGENT_OK;
}

/* 错误路径：删除尾部未完成 turn，避免下次 run 看到孤立 tool_calls */
agent_session_drop_unfinished_tail(agent, session);
arena_end(agent);
return ret ? ret : AGENT_ERROR_LIMIT;
```

读 [src/memory/session_mgr.c](../src/memory/session_mgr.c#L837) 的 `drop_unfinished_tail`：

```c
int agent_session_drop_unfinished_tail(agent_t *agent, agent_session_t *session)
{
    uint32_t turn_start = UINT32_MAX;
    uint32_t i;

    /* 从尾部向前扫描 */
    for (i = session->count; i > 0u; i--) {
        agent_session_entry_t *entry = &session->entries[i - 1u];

        if (entry_is_assistant_final(entry)) {
            return AGENT_OK;   /* 已是完整 turn，无需删除 */
        }
        if (entry->turn_boundary) {
            turn_start = i - 1u;
            break;
        }
    }

    if (turn_start == UINT32_MAX) {
        return AGENT_OK;       /* 没找到 user 起点，啥也不删 */
    }

    remove_count = session->count - turn_start;
    memset(&session->entries[turn_start], 0,
           remove_count * sizeof(agent_session_entry_t));
    session->count = turn_start;
    return AGENT_OK;
}
```

**为什么必须 drop**：

- 如果 agent_run 中途失败（cancel/timeout/limit），session 里可能留下 `user → assistant(tool_calls)` 但没有对应的 `tool` 消息
- 下次 agent_run 时 `session_has_unfinished_turn` 会拒绝追加新 user
- 不 drop 的话用户必须手动 reset session，体验崩坏

**今天对照检查**：你的错误路径有没有调用 `drop_unfinished_tail`？成功路径**不能**调用（否则丢失合法历史），只有 `ret != AGENT_OK` 才调用。

### 6.10 验收标准

- [ ] 5 道检查点全部实现，位置和真实版一致
- [ ] `agent_cancel` 用 critical section 设置 `cancel_requested`，并调 `agent_model_cancel`
- [ ] budget_retry 在 context_build / session_build / model_complete 三处都触发淘汰
- [ ] context overflow 不静默截断，返回 `AGENT_ERROR_CONTEXT_OVERFLOW`
- [ ] critical provider 失败传播，非 critical provider 失败跳过
- [ ] tool 循环前后各一次 `check_run_state`
- [ ] 工具自身失败写 JSON 让模型继续，不 break loop
- [ ] 错误路径调用 `drop_unfinished_tail`，成功路径不调用
- [ ] `max_steps` 用尽返回 `AGENT_ERROR_LIMIT`，不返回 OK

### 6.11 对照反思

读 [src/core/agent_loop.c](../src/core/agent_loop.c) 的 `agent_loop_run` 完整版：

1. 我的 5 道检查点哪几道漏了？为什么漏？
2. 我的 budget_retry 是"三处都淘汰"还是"只在 model.complete 失败时淘汰"？真实版为什么三处都要？
3. 我的 context_builder 对 critical / 非 critical provider 区分了吗？没区分会有什么安全风险？
4. 我的 tool 循环 cancel 检查点是"前后各一次"还是"只前不后"？长工具结束后能不能立即响应 cancel？
5. 我的错误路径有没有 drop_unfinished_tail？不 drop 会导致下次 agent_run 怎样？
6. 真实版 `ret ? ret : AGENT_ERROR_LIMIT` 这个三元运算防止什么？我写对了吗？

***

## Day 7: 测试 + runtime 抽象 + 反思

### 7.1 今日目标

最后一天做三件事：**补单元测试固化行为**、**实现 posix runtime adapter 验证跨平台抽象**、**写完整反思日志**。今天不再加新功能，是把前 6 天的代码"加固"。

### 7.2 要写的代码

```
tests/
├── test_agent lifecycle.c      ← create/destroy/run_simple
├── test_session.c              ← append/evict/drop_tail
├── test_arena.c                ← align/alloc/peak
├── test_tool_registry.c        ← register/find/unregister/dirty
├── test_loop_mock_react.c      ← 完整 ReAct 用 mock model
└── test_cancel_timeout.c       ← cancel + timeout 边界
src/runtime/
├── runtime.c                   ← 已有，今天补 mutex/critical fallback
└── runtime_openvela.c          ← 选做：实际接 NuttX syslog + mbedTLS
docs/
└── my-reflection.md            ← 7 天反思日志
```

### 7.3 C 语言教程：mock 测试法

cAGENT 的测试不依赖真实网络，全部用 mock model + mock tool。**今天必须掌握这个套路**：

```c
/* tests/test_loop_mock_react.c */
static int echo_handler(const agent_tool_call_t *call,
                        agent_tool_result_t *result,
                        void *user_data)
{
    (void)call; (void)user_data;
    result->content_json = "{\"ok\":true,\"value\":\"echo_done\"}";
    return AGENT_OK;
}

static void test_full_react_loop(void)
{
    /* 1. 准备 step 脚本：先 tool_call，再 final */
    agent_model_mock_step_t steps[] = {
        { AGENT_MODEL_MOCK_TOOL_CALL,
          NULL, "call_1", "echo", "{\"value\":\"hi\"}" },
        { AGENT_MODEL_MOCK_FINAL,
          "echo finished", NULL, NULL, NULL },
    };
    agent_model_mock_config_t mock_cfg = {
        .steps = steps,
        .step_count = sizeof(steps) / sizeof(steps[0]),
        .repeat_last = 0,
    };

    /* 2. 创建 agent + 注入 mock + 注册 tool */
    agent_config_t cfg = agent_config_default();
    agent_t *agent = agent_create(&cfg);
    agent_model_t *model = agent_model_mock_create(&mock_cfg, NULL);
    agent_set_model_owned(agent, model);
    agent_register_tool_simple(agent, "echo", "echo input",
                                NULL, echo_handler, NULL,
                                AGENT_TOOL_FLAG_LLM_VISIBLE);

    /* 3. 跑一轮，断言 */
    char output[256];
    int ret = agent_run_simple(agent, "echo hi", output, sizeof(output));
    assert(ret == AGENT_OK);
    assert(strcmp(output, "echo finished") == 0);
    assert(agent->stats.iterations == 2u);   /* 一次 tool_call + 一次 final */
    assert(agent->stats.model_calls == 2u);

    agent_destroy(agent);
}
```

**mock 测试三件套**：

1. **mock model 用 step 脚本**：精确控制每一轮返回 final 还是 tool_call
2. **mock tool 不依赖外部资源**：echo / counter / static_response 三个就够覆盖 90% 场景
3. **断言 stats 字段**：`iterations`、`model_calls`、`completed_runs` 是验证 loop 行为的硬指标

读 [src/llm/model_mock.c](../src/llm/model_mock.c#L26) 的 `default_steps`，这是真实仓库提供的"开箱即用 ReAct 脚本"，今天照抄：

```c
static const agent_model_mock_step_t default_steps[] = {
    { AGENT_MODEL_MOCK_TOOL_CALL,
      NULL, "mock_call_1", "demo_echo",
      "{\"value\":\"demo_tool_ok\"}" },
    { AGENT_MODEL_MOCK_FINAL,
      "mock final reply", NULL, NULL, NULL },
};
```

### 7.4 C 语言教程：跨平台 runtime 抽象（fallback 链）

cAGENT 的 runtime 抽象是"应用层注入 > 平台层填充 > libc fallback"三级链。**今天必须实现完整的 fallback 链**。

读 [src/runtime/runtime.c](../src/runtime/runtime.c#L87) 的 `agent_runtime_fill_defaults`：

```c
void agent_runtime_fill_defaults(agent_runtime_t *runtime)
{
    if (!runtime) return;

    if (!runtime->malloc_fn)  runtime->malloc_fn  = default_malloc;   /* libc */
    if (!runtime->free_fn)    runtime->free_fn    = default_free;
    if (!runtime->now_ms)     runtime->now_ms     = default_now_ms;   /* time(NULL)*1000 */
    if (!runtime->log)        runtime->log        = default_log;      /* fprintf stderr */
    if (!runtime->http_post)  runtime->http_post  = default_http_post;/* 返回 NOTSUP */
}
```

**关键设计点**：

1. **每个回调都 `if (!runtime->xxx)` 检查**：应用层注入的不覆盖
2. **`default_http_post` 返回 `AGENT_ERROR_NOTSUP`**：libc 没有标准 HTTP，必须应用层或平台层注入
3. **`default_now_ms` 精度只到秒**：`time(NULL) * 1000`，生产环境必须由平台层覆盖

读 [src/runtime/runtime.c](../src/runtime/runtime.c#L129) 的封装函数，这是"NULL 安全"的最后一道防线：

```c
void *agent_runtime_malloc(agent_runtime_t *runtime, size_t size)
{
    if (!runtime || !runtime->malloc_fn) {
        return NULL;   /* runtime 或回调为 NULL，返回安全默认值 */
    }
    return runtime->malloc_fn(size, runtime->user_data);
}

uint64_t agent_runtime_now_ms(agent_runtime_t *runtime)
{
    if (!runtime || !runtime->now_ms) {
        return 0u;     /* NULL 时返回 0，让 deadline 永不到期 */
    }
    return runtime->now_ms(runtime->user_data);
}

int agent_runtime_mutex_lock(agent_runtime_t *runtime, void *mutex, uint32_t timeout_ms)
{
    if (!runtime || !mutex || !runtime->mutex_lock) {
        return AGENT_OK;   /* 单线程模式：mutex 为 NULL 直接 OK */
    }
    return runtime->mutex_lock(mutex, timeout_ms, runtime->user_data);
}
```

**NULL 安全的返回值约定**：

| 回调          | NULL 时返回   | 理由                                  |
|---------------|---------------|---------------------------------------|
| `malloc_fn`   | `NULL`        | 让调用者走 OOM 错误路径                |
| `now_ms`      | `0u`          | `run_deadline_ms != 0u` 永远为 false，等价"不限时" |
| `http_post`   | `NOTSUP`      | 显式错误，不让应用以为网络可用        |
| `mutex_lock`  | `OK`          | 单线程平台无 mutex，直接放行          |
| `enter_critical` | `0u`       | 单线程平台无临界区，state=0 是无操作  |

**今天必须实现**：上面的封装函数 + fallback 表，确保你的库在"runtime 全 NULL"时也能跑（单线程裸机场景）。

### 7.5 设计模式：Runtime Abstraction（完整版）

**应用场景**：同一份 core 代码跑在 posix / openvela / esp-idf / stm32 四个平台，平台特定代码隔离在 `runtime_*.c`。

**cAGENT 真实位置**：

- [src/runtime/runtime.c](../src/runtime/runtime.c)：posix fallback + 封装函数
- [src/runtime/runtime_openvela.c](../src/runtime/runtime_openvela.c)：openvela/NuttX 适配
- [src/runtime/runtime_espidf.c](../src/runtime/runtime_espidf.c)：ESP-IDF 适配
- [src/runtime/runtime_stm32.c](../src/runtime/runtime_stm32.c)：STM32 HAL 适配

**平台适配层的统一结构**（每个 `runtime_xxx.c` 都长这样）：

```c
/* src/runtime/runtime_openvela.c */
static void *ov_malloc(size_t size, void *user_data) { return malloc(size); }
static void  ov_free(void *ptr, void *user_data)     { free(ptr); }
static uint64_t ov_now_ms(void *user_data) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (uint64_t)time(NULL) * 1000u;   /* fallback to秒级 */
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static void ov_log(int level, const char *tag, const char *message, void *user_data) {
    int priority = (level == 0) ? LOG_ERR : (level == 1) ? LOG_WARNING : LOG_INFO;
    syslog(priority, "[%s] %s\n", tag ? tag : "cagent", message ? message : "");
}

void agent_runtime_openvela_fill(agent_runtime_t *runtime)
{
    if (!runtime) return;
    if (!runtime->malloc_fn)  runtime->malloc_fn  = ov_malloc;
    if (!runtime->free_fn)    runtime->free_fn    = ov_free;
    if (!runtime->now_ms)     runtime->now_ms     = ov_now_ms;
    if (!runtime->sleep_ms)   runtime->sleep_ms   = ov_sleep_ms;
    if (!runtime->log)        runtime->log        = ov_log;
#ifdef CAGENT_RUNTIME_OPENVELA_TLS
    if (!runtime->http_post)  runtime->http_post  = ov_http_post;  /* mbedTLS */
#endif
    if (!runtime->mutex_create) runtime->mutex_create = ov_mutex_create;
    /* ... */
}
```

**关键设计点**：

1. **每个平台适配层都用 `if (!runtime->xxx)` 守卫**：和 `fill_defaults` 一样的语义，应用层注入优先
2. **openvela 的 `now_ms` 用 `CLOCK_MONOTONIC`**：不受系统时间跳变影响，deadline 计算稳定
3. **`http_post` 用 `#ifdef CAGENT_RUNTIME_OPENVELA_TLS` 包裹**：mbedTLS 是可选依赖，没启用时 `http_post` 留给 libc fallback（返回 NOTSUP）
4. **平台 include 隔离在 `runtime_xxx.c`**：`<syslog.h>`、`<pthread.h>` 不出现在 core 里

**今天对照检查**：你的 core 代码里有没有 `#include <pthread.h>` 或 `<syslog.h>`？有的话架构就破了，必须挪到 `runtime_xxx.c`。

### 7.6 真实源码解析：openvela TLS 适配的关键取舍

读 [src/runtime/runtime_openvela.c](../src/runtime/runtime_openvela.c#L130) 的 `ov_tls_connect`，今天选读，理解 cAGENT 在嵌入式 TLS 上的关键取舍：

```c
/* 非阻塞 connect + select 超时 */
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
ret = connect(fd, res->ai_addr, res->ai_addrlen);
if (ret < 0 && errno == EINPROGRESS) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    ret = select(fd + 1, NULL, &wfds, NULL, &tv_conn);
    /* ... */
}
```

**为什么不用 `mbedtls_net_connect` 直接阻塞连接**：注释里写得很清楚——NuttX 上当远程端口被防火墙过滤（无 SYN-ACK 也无 RST）时，`mbedtls_net_connect` 会无限阻塞，loop 的 `timeout_ms` 检查点形同虚设。所以 cAGENT 自己用非阻塞 connect + select 实现超时。

**为什么 `mbedtls_ssl_recv` 不设 `SO_RCVTIMEO`**：mbedTLS 期望 `mbedtls_net_recv` 在无数据时返回 `WANT_READ`，而不是 `ETIMEDOUT`。设了 socket 超时会让 mbedTLS 把超时当致命错误，无法重试。

**这两个取舍的教训**：嵌入式网络栈和桌面/服务器有微妙差异，照搬 posix 经验会踩坑。cAGENT 的注释把这些坑都写下来了，今天读源码时把这些注释抄到反思日志里。

### 7.7 单元测试清单（最小集）

今天至少写这 5 个测试，每个测试一个 `assert` 失败就修到通过：

```c
/* test_agent_lifecycle.c */
void test_create_destroy(void) {
    agent_t *a = agent_create(NULL);
    assert(a != NULL);
    agent_destroy(a);   /* 不崩溃 */
}

/* test_session.c */
void test_evict_oldest_turn(void) {
    /* 填满 session → 触发 evict → 验证只剩最后一个完整 turn */
}

/* test_arena.c */
void test_arena_align_and_peak(void) {
    /* 分配 9 字节 → used 应该是 16（align=8） */
    /* 分配到满 → 返回 NULL */
    /* arena_end 后 peak 写入 stats */
}

/* test_tool_registry.c */
void test_register_find_dirty(void) {
    /* 注册 3 个 tool → find 第 2 个 */
    /* 重名注册返回 INVALID */
    /* 注册后 tool_schema_dirty == true */
}

/* test_loop_mock_react.c */
void test_full_react_with_tool_call(void) {
    /* 见 7.3 节代码 */
}

/* test_cancel_timeout.c */
void test_cancel_between_tools(void) {
    /* mock model 返回 2 个 tool_call */
    /* 在第 1 个 tool 里调 agent_cancel */
    /* 断言 ret == AGENT_ERROR_CANCELLED，stats.cancelled_runs == 1 */
}
```

### 7.8 反思日志模板

今天写 `docs/my-reflection.md`，把 7 天的反思日志汇总。每天至少回答 3 个问题：

```markdown
# cAGENT 手写实现反思日志

## Day 1（公共类型与生命周期）
- 我的三级宏映射写对了吗？哪个漏了？
- opaque handle 比 void* 好在哪？我之前为什么不用？
- agent_create 的 runtime 双层填充，我有没有照抄？没抄会有什么 bug？

## Day 2（agent_loop 骨架）
- 4 个 stub 的签名和真实版一致吗？哪个我发明了接口？
- Walking Skeleton 让我提前发现了什么接口设计问题？
- 错误路径 arena_end 我写对了吗？

## Day 3（session + arena）
- arena 对齐的位运算我理解了吗？align=8 时 9 → 16 怎么算的？
- turn 淘汰为什么不能删孤立 tool message？
- budget_retry 我留 TODO 了吗？位置对吗？

## Day 4（tool/skill registry）
- 我有没有手贱去写链表？为什么 cAGENT 选固定数组？
- tool_schema_dirty 我想到了吗？没有的话每次 loop 都重新生成 schema
- mock model 的 step 脚本我用了 repeat_last 吗？

## Day 5（OpenAI adapter）
- ops vtable 比 switch-case 好在哪？
- 三级错误分层我区分清楚了吗？工具级错误为什么不能崩 loop？
- openai_ops 和 mock_ops 能不能无感切换？

## Day 6（边界加固）
- 5 道检查点我漏了哪几道？为什么漏？
- budget_retry 是"三处都淘汰"还是"只 model 失败时淘汰"？
- drop_unfinished_tail 我加了吗？不加会导致什么？

## Day 7（测试 + runtime 抽象）
- mock 测试法比"连真实 API 测"好在哪？
- runtime fallback 链我能画出来吗？应用层 > 平台层 > libc 的优先级
- openvela TLS 的两个取舍（非阻塞 connect + 不设 SO_RCVTIMEO）我理解了吗？

## 总反思
- 7 天里最难的一天是哪天？为什么？
- 我手写的版本和真实 cAGENT 还差哪些功能？
- 下一步想补什么？（streaming / multi-modal / RAG / 并行 tool_call）
```

### 7.9 验收标准

- [ ] 5 个单元测试全部通过
- [ ] `runtime` 全 NULL 时 agent 仍能跑（单线程裸机模式）
- [ ] `runtime` 注入 mock 后所有测试不依赖网络
- [ ] core 代码无任何 `#include <pthread.h>` / `<syslog.h>` / `<nuttx/...>`
- [ ] `docs/my-reflection.md` 7 天反思全部填完
- [ ] 总反思里列出"和真实 cAGENT 还差哪些功能"至少 3 条

### 7.10 对照反思

读 [src/runtime/runtime.c](../src/runtime/runtime.c) 和 [src/runtime/runtime_openvela.c](../src/runtime/runtime_openvela.c)：

1. 我的 fallback 链优先级和真实版一致吗？（应用层 > 平台层 > libc）
2. 我的 NULL 安全返回值和真实版一致吗？特别是 `now_ms` 返回 0u 而非当前时间
3. 我的平台适配层有没有把 `<syslog.h>` 隔离在 `runtime_xxx.c`？core 里干净吗？
4. 真实版 openvela 用 `CLOCK_MONOTONIC`，我用了什么？为什么 MONOTONIC 比 REALTIME 更适合 deadline？
5. 我有没有写 `default_http_post` 返回 NOTSUP？没写的话裸机平台会怎样？

***

## 8. 总反思与下一步

### 8.1 7 天回顾

| Day | 核心收获                                | 最容易踩的坑                          |
|-----|-----------------------------------------|---------------------------------------|
| 1   | 三级宏映射、opaque handle、函数指针 typedef | 函数指针 typedef 括号写错              |
| 2   | Walking Skeleton、stub 契约即接口契约    | stub 签名自己发明，Day 3 全推翻        |
| 3   | arena 对齐、turn 边界、budget_retry 留位 | turn 淘汰删了孤立 tool message         |
| 4   | 固定数组 vs 链表、dirty 缓存、step 脚本  | 手贱写链表，引入 malloc 碎片           |
| 5   | ops vtable、三级错误、JSON 契约          | 工具失败崩 loop，破坏 ReAct 鲁棒性     |
| 6   | 5 道检查点、三处 budget_retry、drop_tail | 错误路径不 drop_tail，下次 run 卡死    |
| 7   | mock 测试、fallback 链、平台隔离         | core 里混入 `<pthread.h>`，架构破      |

### 8.2 和真实 cAGENT 还差什么

7 天手写版本覆盖了 cAGENT 的"灵魂"（loop / arena / registry / ops vtable / cooperative cancel / runtime 抽象），但还差这些功能：

- **Streaming 响应**：cAGENT 真实版有 `AGENT_EVENT_MODEL_TOKEN` 事件，手写版只支持 final 一次性返回
- **并行 tool_call**：真实版 P1 阶段顺序执行，P2 会引入并行；手写版只有顺序
- **Skill 系统**：手写版只占位，真实版有 skill router 和动态加载
- **Context provider 优先级排序**：手写版简化，真实版有 `priority` 字段 + 稳定排序
- **Trace/event 系统**：手写版只 emit 事件，真实版有 trace_id 贯穿和 event buffer
- **多 session 并发**：手写版单 agent 单 session 跑，真实版支持 `CAGENT_MAX_SESSIONS=4` 并发
- **持久化**：session 持久化到 flash / KV，手写版纯内存

### 8.3 推荐的下一步练习

1. **加 streaming**：扩展 `agent_model_ops_t` 加 `complete_stream` 回调，emit `MODEL_TOKEN` 事件
2. **加并行 tool**：用 `agent_runtime_create_task` 把 tool 执行并行化
3. **移植到真实硬件**：选 openvela / esp-idf / stm32 之一，把 `runtime_xxx.c` 真正跑起来
4. **加 RAG**：注册一个 `retrieve` context_provider，从向量库取相关文档拼到 context
5. **对比 ai_agent**：读 `packages/ai_agent` 的实现，对比 cAGENT 的设计差异，写一篇对比文章
