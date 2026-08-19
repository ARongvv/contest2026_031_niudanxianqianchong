# cAGENT 函数参考文档

> 自动生成自 cAGENT 源码，反映当前代码状态。

---

## 目录

- [1. 模块总览与调用关系图谱](#1-模块总览与调用关系图谱)
- [2. Agent 生命周期 (agent_core.c)](#2-agent-生命周期-agent_corec)
- [3. ReAct 推理循环 (agent_loop.c)](#3-react-推理循环-agent_loopc)
- [4. 事件系统 (agent_event.c)](#4-事件系统-agent_eventc)
- [5. 配置加载 (config_load.c)](#5-配置加载-config_loadc)
- [6. 上下文构建 (context_builder.c)](#6-上下文构建-context_builderc)
- [7. 模型抽象层 (model.c)](#7-模型抽象层-modelc)
- [8. OpenAI 兼容模型 (model_openai.c)](#8-openai-兼容模型-model_openaic)
- [9. Mock 模型 (model_mock.c)](#9-mock-模型-model_mockc)
- [10. LLM 响应解析 (llm_parse.c)](#10-llm-响应解析-llm_parsec)
- [11. Runtime 抽象层 (runtime.c)](#11-runtime-抽象层-runtimec)
- [12. OpenVela Runtime 适配 (runtime_openvela.c)](#12-openvela-runtime-适配-runtime_openvelac)
- [13. 工具注册表 (tool_registry.c)](#13-工具注册表-tool_registryc)
- [14. 工具守卫与执行 (tool_guard.c)](#14-工具守卫与执行-tool_guardc)
- [15. 工具 Schema 构建 (tool_schema.c)](#15-工具-schema-构建-tool_schemac)
- [16. Skill 注册表 (skill_registry.c)](#16-skill-注册表-skill_registryc)
- [17. Session 管理器 (session_mgr.c)](#17-session-管理器-session_mgrc)
- [18. Memory 快照 (memory_store.c)](#18-memory-快照-memory_storec)
- [19. 错误码参考](#19-错误码参考)
- [20. 编译期常量参考](#20-编译期常量参考)
- [21. 完整使用示例](#21-完整使用示例)

---

## 1. 模块总览与调用关系图谱

### 1.1 模块依赖关系

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (demo/app)                      │
└──────────────┬──────────────────────┬───────────────────┘
               │                      │
               ▼                      ▼
┌──────────────────────┐  ┌──────────────────────────────┐
│   agent.h (公共 API)  │  │  model_openai.h (模型便捷API) │
└──────────┬───────────┘  └──────────────┬───────────────┘
           │                             │
           ▼                             ▼
┌──────────────────────────────────────────────────────────┐
│                    agent_core.c (生命周期)                  │
│  agent_create / agent_destroy / agent_run / agent_cancel  │
└────┬──────────┬──────────┬──────────┬────────────────────┘
     │          │          │          │
     ▼          ▼          ▼          ▼
┌─────────┐ ┌────────┐ ┌──────────┐ ┌───────────────┐
│agent_   │ │agent_  │ │context_  │ │tool_registry/ │
│loop.c   │ │event.c │ │builder.c │ │guard/schema   │
└────┬────┘ └────────┘ └──────────┘ └───────────────┘
     │
     ├──────────────┬───────────────┐
     ▼              ▼               ▼
┌──────────┐ ┌────────────┐ ┌──────────────┐
│model.c   │ │session_mgr │ │skill_registry│
│(抽象层)  │ │.c          │ │.c            │
└────┬─────┘ └────────────┘ └──────────────┘
     │
     ├──────────────┬──────────────┐
     ▼              ▼              ▼
┌────────────┐ ┌──────────┐ ┌──────────┐
│model_      │ │model_    │ │model_    │
│openai.c    │ │mock.c    │ │(router)  │
└────┬───────┘ └──────────┘ └──────────┘
     │
     ▼
┌──────────────────────────────────────────┐
│         runtime.c (抽象封装)               │
│  agent_runtime_malloc / http_post / ...   │
└──────────────┬───────────────────────────┘
               │
       ┌───────┴────────┐
       ▼                ▼
┌──────────────┐ ┌──────────────────┐
│runtime_      │ │POSIX fallback    │
│openvela.c    │ │(default_*)       │
│(mbedTLS+     │ │                  │
│ pthread)     │ │                  │
└──────────────┘ └──────────────────┘
```

### 1.2 核心调用链

**agent_run 完整调用链：**

```
agent_run()
  ├── agent_runtime_mutex_lock()          // 加锁
  ├── agent_event_emit(RUN_START)
  ├── agent_loop_run()                    // ReAct 循环
  │     ├── check_run_state()             // 检查取消/超时
  │     ├── arena_begin()                 // 分配请求 arena
  │     ├── allocate_loop_buffers()       // 分配 context/tools/messages 缓冲区
  │     ├── agent_session_find_or_create()
  │     ├── agent_session_append()        // 添加 user 消息
  │     │
  │     └── for iter < max_steps:         // 迭代循环
  │           ├── check_run_state()
  │           ├── agent_event_emit(ITERATION_START)
  │           ├── agent_context_build()   // 构建系统消息
  │           │     ├── append_text(system_prompt)
  │           │     ├── append_registered_providers()
  │           │     └── agent_skill_context_build()
  │           ├── agent_tool_schema_build() // 构建工具 JSON
  │           ├── agent_session_build_model_messages()
  │           ├── agent_model_complete()    // 调用模型
  │           │     └── ops.complete()      // → openai_compat_complete()
  │           │           ├── build_request_json()
  │           │           └── agent_runtime_http_post()
  │           │                 └── runtime->http_post()  // → ov_http_post()
  │           │                       ├── ov_tls_connect()
  │           │                       ├── ov_tls_write_request()
  │           │                       └── ov_tls_read_response()
  │           ├── extract_tool_calls()     // 解析工具调用
  │           ├── agent_session_add_assistant_tool_calls()
  │           ├── for each tool_call:
  │           │     ├── agent_tool_execute()
  │           │     │     ├── agent_tool_registry_find()
  │           │     │     ├── check_policy()
  │           │     │     └── tool.execute()
  │           │     └── agent_session_add_tool()
  │           └── (最终回复) copy_final_output()
  │
  ├── agent_event_emit(RUN_DONE)
  ├── agent_runtime_mutex_unlock()
  └── return
```

---

## 2. Agent 生命周期 (agent_core.c)

> 文件路径: `src/core/agent_core.c`
> 头文件: `include/agent.h`, `include/cagent/config.h`

### 2.1 `agent_config_default`

```c
agent_config_t agent_config_default(void);
```

**功能**: 生成标准默认配置。

**返回值**: 填充了默认值的 `agent_config_t` 结构体。

**默认值**:
- `name`: `"cagent"`
- `system_prompt`: `"You are a helpful embedded agent."`
- `limits`: `AGENT_LIMITS_DEFAULT` 宏（max_steps=8, timeout_ms=30000, per_model_timeout_ms=15000, per_tool_timeout_ms=3000, max_tool_calls=4, max_output_tokens=512）
- `runtime`: 由 `agent_runtime_fill_defaults()` 填充 POSIX fallback

**调用关系**: → `default_limits()`, `agent_runtime_fill_defaults()`

**使用示例**:
```c
agent_config_t cfg = agent_config_default();
cfg.name = "my_agent";
cfg.system_prompt = "You are a device controller.";
agent_t *agent = agent_create(&cfg);
```

---

### 2.2 `agent_config_tiny`

```c
agent_config_t agent_config_tiny(void);
```

**功能**: 生成精简配置，适用于小 MCU / bare-metal 场景。

**返回值**: 更小 limits 的 `agent_config_t`。

**与 default 的差异**:
| 参数 | default | tiny |
|------|---------|------|
| max_steps | 8 | 4 |
| timeout_ms | 30000 | 10000 |
| per_model_timeout_ms | 15000 | 5000 |
| per_tool_timeout_ms | 3000 | 1000 |
| max_tool_calls | 4 | 2 |
| max_output_tokens | 512 | 128 |

**调用关系**: → `agent_config_default()`

**使用示例**:
```c
/* 适用于 RAM 受限的 MCU 场景 */
agent_config_t cfg = agent_config_tiny();
agent_t *agent = agent_create(&cfg);
```

---

### 2.3 `agent_create`

```c
agent_t *agent_create(const agent_config_t *config);
```

**功能**: 创建 agent 实例。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| config | `const agent_config_t *` | 配置，NULL 时使用默认配置 |

**返回值**: 成功返回 agent 指针，失败返回 NULL。

**核心逻辑**:
1. config 为 NULL 时调用 `agent_config_default()`
2. 若 `runtime.malloc_fn` 为 NULL，自动调用 `agent_runtime_fill_platform()` 填充平台回调
3. 调用 `agent_runtime_fill_defaults()` 补齐剩余 NULL 回调
4. 使用 runtime 的 `malloc_fn` 分配 `agent_t` 结构体
5. 初始化各字段，设置 `tool_schema_dirty = true`
6. 若 `mutex_create` 已设置，创建互斥量

**调用关系**: → `agent_config_default()`, `agent_runtime_fill_platform()`, `agent_runtime_fill_defaults()`, `agent_runtime_malloc()`, `runtime.mutex_create()`

**使用示例**:
```c
agent_config_t cfg = agent_config_default();
cfg.name = "my_agent";
agent_t *agent = agent_create(&cfg);
```

---

### 2.4 `agent_create_simple`

```c
agent_t *agent_create_simple(const char *name, const char *system_prompt);
```

**功能**: 便捷创建 agent，只需提供 name 和 system_prompt。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| name | `const char *` | Agent 名称，NULL 时使用默认 "cagent" |
| system_prompt | `const char *` | 系统提示词，NULL 时使用默认值 |

**返回值**: 成功返回 agent 指针，失败返回 NULL。

**调用关系**: → `agent_config_default()`, `agent_create()`

**使用示例**:
```c
agent_t *agent = agent_create_simple("my_agent", "You are a device controller.");
```

---

### 2.5 `agent_destroy`

```c
void agent_destroy(agent_t *agent);
```

**功能**: 销毁 agent，释放所有内部资源。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| agent | `agent_t *` | agent 指针，NULL 时安全返回 |

**核心逻辑**:
1. 若 `model_owned == true` 且 model 非 NULL，调用 `agent_model_destroy()` 销毁模型
2. 若 mutex 非 NULL 且 `mutex_destroy` 已设置，销毁互斥量
3. 调用 `agent_runtime_free()` 释放 agent 自身

**调用关系**: → `agent_model_destroy()`, `runtime.mutex_destroy()`, `agent_runtime_free()`

**使用示例**:
```c
/* agent_destroy 会自动销毁 owned model，无需手动管理 */
agent_t *agent = agent_create_simple("demo", "You are helpful.");
agent_attach_openai(agent, "api.deepseek.com", "sk-xxx", "deepseek-v4-flash");

agent_run_simple(agent, "Hello", output, sizeof(output));

agent_destroy(agent);  /* 自动销毁 model + mutex + agent 自身 */
agent = NULL;          /* 防止悬垂指针 */
```

---

### 2.6 `agent_run`

```c
int agent_run(agent_t *agent,
              const agent_request_t *request,
              agent_response_t *response);
```

**功能**: 执行一次同步 ReAct 推理。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| agent | `agent_t *` | agent 实例 |
| request | `const agent_request_t *` | 请求，`input` 不可为 NULL |
| response | `agent_response_t *` | 响应，调用者分配 output 缓冲区 |

**返回值**: `AGENT_OK` 成功，或 `agent_error_t` 错误码。

**核心逻辑**:
1. 参数校验（agent、request、request->input、response 均不可为 NULL）
2. mutex lock → busy 检查 → 设置 busy
3. 保存当前 limits，允许 request->limits 临时覆盖
4. 重置 cancel flag，计算 deadline
5. emit `RUN_START` 事件
6. 调用 `agent_loop_run()` 执行 ReAct 循环
7. 更新 stats（completed/failed/cancelled/timeout 计数）
8. emit `RUN_DONE` 事件
9. 恢复 limits，清除 busy，mutex unlock

**调用关系**: → `agent_runtime_mutex_lock()`, `agent_event_emit()`, `agent_loop_run()`, `agent_runtime_now_ms()`, `agent_runtime_mutex_unlock()`

**异常处理**:
- `AGENT_ERROR_INVALID`: 参数为 NULL
- `AGENT_ERROR_BUSY`: agent 正忙或 mutex lock 失败
- 其他错误码由 `agent_loop_run()` 传播

**性能考量**: mutex lock 使用 timeout=0（非阻塞），避免死锁。

**使用示例**:
```c
/* 完整用法：自定义 request/response */
agent_request_t request;
agent_response_t response;
char output[4096];

memset(&request, 0, sizeof(request));
request.input = "What time is it?";
request.session_id = "default";

memset(&response, 0, sizeof(response));
response.output = output;
response.output_size = sizeof(output);

int ret = agent_run(agent, &request, &response);
if (ret == AGENT_OK) {
    printf("Result: %s\n", output);
}

/* 临时覆盖 limits */
agent_limits_t override = {
    .max_steps = 2,
    .timeout_ms = 5000,
    .per_model_timeout_ms = 3000,
    .per_tool_timeout_ms = 1000,
    .max_tool_calls = 1,
    .max_output_tokens = 256
};
request.limits = &override;
ret = agent_run(agent, &request, &response);
/* 请求结束后 agent limits 自动恢复 */
```

---

### 2.7 `agent_run_simple`

```c
int agent_run_simple(agent_t *agent,
                     const char *input,
                     char *output,
                     size_t output_size);
```

**功能**: 便捷运行 agent，自动构造 request/response。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| agent | `agent_t *` | agent 实例 |
| input | `const char *` | 用户输入文本 |
| output | `char *` | 输出缓冲区 |
| output_size | `size_t` | 缓冲区大小 |

**返回值**: `AGENT_OK` 或 `agent_error_t`。

**调用关系**: → `agent_run()`

**使用示例**:
```c
char output[4096];
int ret = agent_run_simple(agent, "What time is it?", output, sizeof(output));
```

---

### 2.8 `agent_cancel`

```c
int agent_cancel(agent_t *agent);
```

**功能**: 协作式取消正在运行的 agent_run。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| agent | `agent_t *` | agent 实例 |

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**核心逻辑**:
1. 通过 `enter_critical` / `exit_critical` 安全设置 `cancel_requested`（ISR 安全）
2. 调用 `agent_model_cancel()` 通知模型 provider
3. 实际取消发生在 loop 的下一个检查点（`check_run_state()`）

**调用关系**: → `agent_runtime_enter_critical()`, `agent_runtime_exit_critical()`, `agent_model_cancel()`

**使用示例**:
```c
/* 从另一个线程/ISR/按钮回调中取消正在运行的 agent */
void on_cancel_button(void)
{
    agent_cancel(agent);
    /* 实际取消发生在 loop 的下一个检查点 */
}

/* 在 watchdog 中检测超时并取消 */
void watchdog_handler(void)
{
    agent_stats_t stats;
    agent_get_stats(agent, &stats);
    if (stats.last_run_elapsed_ms > 60000) {
        agent_cancel(agent);
    }
}
```

---

### 2.9 `agent_reset`

```c
int agent_reset(agent_t *agent);
```

**功能**: 重置 agent 状态（stats、cancel flag、deadline、sessions）。不清除已注册的 tools/skills/model/runtime。

**返回值**: `AGENT_OK`、`AGENT_ERROR_INVALID` 或 `AGENT_ERROR_BUSY`。

**调用关系**: → `agent_session_clear_all()`

**使用示例**:
```c
/* 重置 agent 状态，开始全新对话 */
agent_reset(agent);
/* tools/skills/model/runtime 保留，stats 和 sessions 清空 */
```

---

### 2.10 `agent_set_model`

```c
int agent_set_model(agent_t *agent, agent_model_t *model);
```

**功能**: 将模型 attach 到 agent，替换已有模型。model 生命周期由调用者管理。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**使用示例**:
```c
/* 手动管理模型生命周期 */
agent_model_t *model = agent_model_openai_create_simple("api.deepseek.com",
                                                         "sk-xxx",
                                                         "deepseek-v4-flash");
agent_set_model(agent, model);

/* 使用完毕后手动销毁 */
agent_run_simple(agent, "Hello", output, sizeof(output));
agent_model_destroy(model);  /* 必须手动销毁 */
model = NULL;
agent_destroy(agent);
```

---

### 2.11 `agent_set_model_owned`

```c
int agent_set_model_owned(agent_t *agent, agent_model_t *model);
```

**功能**: 将模型 attach 到 agent 并转移所有权。`agent_destroy()` 时自动调用 `agent_model_destroy(model)`。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**使用示例**:
```c
/* 转移所有权，agent_destroy 自动销毁模型 — 推荐方式 */
agent_model_t *model = agent_model_openai_create_simple("api.deepseek.com",
                                                         "sk-xxx",
                                                         "deepseek-v4-flash");
agent_set_model_owned(agent, model);

/* cleanup 时只需 agent_destroy，模型自动销毁 */
agent_destroy(agent);  /* 内部调用 agent_model_destroy(model) */
```

---

### 2.12 `agent_get_model`

```c
agent_model_t *agent_get_model(agent_t *agent);
```

**功能**: 安全获取 agent 当前绑定的模型。

**返回值**: 模型指针，未设置时返回 NULL。指针在 `agent_destroy` 或重新 `set_model` 后失效。

**使用示例**:
```c
/* 获取模型后修改后端配置 */
agent_model_t *model = agent_get_model(agent);
if (model) {
    agent_model_openai_set_backend(model, "api.newhost.com",
                                   "/v1/chat/completions", "443");
}
```

---

### 2.13 `agent_set_event_callback`

```c
int agent_set_event_callback(agent_t *agent, agent_event_cb_t cb, void *user_data);
```

**功能**: 注册事件回调，替换已有回调。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**使用示例**:
```c
/* demo 中的事件回调实现 */
static const char *event_type_name(agent_event_type_t type)
{
    switch (type) {
    case AGENT_EVENT_RUN_START:       return "RUN_START";
    case AGENT_EVENT_RUN_DONE:        return "RUN_DONE";
    case AGENT_EVENT_ITERATION_START: return "ITER_START";
    case AGENT_EVENT_MODEL_REQUEST:   return "MODEL_REQ";
    case AGENT_EVENT_MODEL_RESPONSE:  return "MODEL_RESP";
    case AGENT_EVENT_TOOL_CALL:       return "TOOL_CALL";
    case AGENT_EVENT_TOOL_RESULT:     return "TOOL_RESULT";
    case AGENT_EVENT_ERROR:           return "ERROR";
    case AGENT_EVENT_CANCELLED:       return "CANCELLED";
    case AGENT_EVENT_TIMEOUT:         return "TIMEOUT";
    default:                          return "UNKNOWN";
    }
}

static void demo_event_cb(const agent_event_t *event, void *user_data)
{
    (void)user_data;
    printf("[event] %-12s iter=%u err=%d tool=%s call_id=%s msg=%s\n",
           event_type_name(event->type),
           event->iteration,
           event->error_code,
           event->tool_name ? event->tool_name : "-",
           event->tool_call_id ? event->tool_call_id : "-",
           event->message ? event->message : "-");
}

/* 注册回调 */
agent_set_event_callback(agent, demo_event_cb, NULL);
```

---

### 2.14 `agent_set_policy_callback`

```c
int agent_set_policy_callback(agent_t *agent, agent_policy_cb_t cb, void *user_data);
```

**功能**: 注册策略回调，替换已有回调。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**使用示例**:
```c
/* 策略回调：拒绝有副作用的工具（如 OTA/重启），需要用户确认 */
static agent_policy_decision_t demo_policy_cb(const agent_policy_request_t *request,
                                               void *user_data)
{
    (void)user_data;

    if (request->action == AGENT_POLICY_ACTION_TOOL_CALL) {
        const char *name = request->tool_call->name;

        /* 只读工具直接放行 */
        if (strcmp(name, "echo") == 0 || strcmp(name, "get_time") == 0) {
            return AGENT_POLICY_ALLOW;
        }

        /* 有副作用的工具需要确认（MVP 等同 DENY） */
        if (strcmp(name, "ota_update") == 0 || strcmp(name, "reboot") == 0) {
            return AGENT_POLICY_REQUIRE_CONFIRM;
        }
    }

    return AGENT_POLICY_ALLOW;
}

agent_set_policy_callback(agent, demo_policy_cb, NULL);
```

---

### 2.15 `agent_set_limits`

```c
int agent_set_limits(agent_t *agent, const agent_limits_t *limits);
```

**功能**: 持久修改 agent limits，同时更新 config.limits 和 agent->limits。不影响正在执行的 agent_run。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**使用示例**:
```c
/* 根据场景动态调整 limits */
agent_limits_t limits = {
    .max_steps = 8,
    .timeout_ms = 60000,
    .per_model_timeout_ms = 45000,
    .per_tool_timeout_ms = 5000,
    .max_tool_calls = 10,
    .max_output_tokens = 1024
};
agent_set_limits(agent, &limits);
```

---

### 2.16 `agent_get_stats`

```c
int agent_get_stats(agent_t *agent, agent_stats_t *stats);
```

**功能**: 读取 agent 累计统计快照。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_INVALID`。

**使用示例**:
```c
agent_stats_t stats;
agent_get_stats(agent, &stats);
printf("Runs: %u completed, %u failed, %u cancelled, %u timeout\n",
       stats.completed_runs, stats.failed_runs,
       stats.cancelled_runs, stats.timeout_runs);
printf("Model calls: %u, Tool calls: %u\n",
       stats.model_calls, stats.tool_calls);
printf("Last run: %llu ms, arena peak: %zu bytes\n",
       (unsigned long long)stats.last_run_elapsed_ms,
       stats.arena_peak_bytes);
```

---

## 3. ReAct 推理循环 (agent_loop.c)

> 文件路径: `src/core/agent_loop.c`

### 3.1 `agent_loop_run`

```c
int agent_loop_run(agent_t *agent,
                   const agent_request_t *request,
                   agent_response_t *response);
```

**功能**: ReAct 推理循环执行体，由 `agent_run()` 调用。

**核心算法 — ReAct 循环**:
1. 检查运行状态（取消/超时）
2. 分配 request arena 和 loop 缓冲区（context、tools_json、messages_json）
3. 查找或创建 session，追加 user 消息
4. **迭代循环**（iter < max_steps）:
   a. 检查运行状态
   b. 构建 context（system_prompt + providers + skills）
   c. 构建 tool schema JSON
   d. 构建 session messages JSON
   e. 调用 `agent_model_complete()` 执行模型推理
   f. 解析模型响应：
      - 有 tool_calls → 追加 assistant tool_calls → 逐个执行工具 → 追加 tool result → continue
      - 无 tool_calls（最终回复）→ 追加 assistant 消息 → copy 到 response → break
5. 迭代超限时返回 `AGENT_ERROR_LIMIT`

**调用关系**: → `check_run_state()`, `arena_begin()`, `allocate_loop_buffers()`, `agent_session_find_or_create()`, `agent_session_append()`, `agent_context_build()`, `agent_tool_schema_build()`, `agent_session_build_model_messages()`, `agent_model_complete()`, `agent_event_emit()`, `extract_tool_calls()`, `agent_session_add_assistant_tool_calls()`, `agent_tool_execute()`, `agent_session_add_tool()`, `agent_session_add_assistant()`, `copy_final_output()`, `arena_end()`

**异常处理**:
- 每个步骤失败立即 break，返回对应错误码
- 工具执行失败时以 tool result JSON 写回 session，让模型在下一轮自行处理
- arena 在循环结束后统一释放（`arena_end()`）

**性能考量**:
- 使用 arena 分配器避免频繁 malloc/free，arena 在 loop 结束时一次性释放
- tool schema 有缓存机制，仅在 registry 变更时重新构建

---

### 3.2 Arena 管理函数

| 函数 | 功能 |
|------|------|
| `arena_begin(agent, required_size)` | 分配 arena 缓冲区，检查大小是否足够 |
| `arena_end(agent)` | 释放 arena，更新峰值统计 |
| `arena_alloc(agent, size)` | 从 arena 分配对齐的零初始化内存块 |
| `allocate_loop_buffers(agent, ...)` | 从 arena 分配 context/tools/messages 三个缓冲区 |

**Arena 分配策略**: 线性分配，对齐到 `sizeof(void*)`，不支持释放单个块。arena 大小默认由 system/context、tool schema、messages 三块 buffer 之和自动计算，可通过 `CAGENT_REQUEST_ARENA_EXTRA_SIZE` 增加额外余量。

**使用示例**:
```c
/* Arena 由 agent_loop_run 内部管理，应用层通常不直接使用 */
/* 每次 agent_run 开始时分配 arena，结束时释放 */
/* 如需额外 arena 余量，修改 CAGENT_REQUEST_ARENA_EXTRA_SIZE 常量 */
```

---

## 4. 事件系统 (agent_event.c)

> 文件路径: `src/core/agent_event.c`

### 4.1 `agent_event_emit`

```c
void agent_event_emit(agent_t *agent,
                      agent_event_type_t type,
                      const agent_request_t *request,
                      uint32_t iteration,
                      int error_code,
                      const char *message,
                      const char *tool_name,
                      const char *tool_call_id);
```

**功能**: 统一事件派发。所有内部模块通过此函数 emit 事件。

**核心逻辑**:
1. 若 agent 或 event_cb 为 NULL，直接返回（无开销）
2. 自动填充 `timestamp_ms`（`runtime.now_ms()`）、`trace_id`、`session_id`
3. 调用 `event_cb(&event, event_user_data)`

**事件类型**:
| 类型 | 触发时机 |
|------|----------|
| `AGENT_EVENT_RUN_START` | agent_run 入口 |
| `AGENT_EVENT_RUN_DONE` | agent_run 出口 |
| `AGENT_EVENT_ITERATION_START` | 每轮迭代开始 |
| `AGENT_EVENT_MODEL_REQUEST` | 即将调用 model.complete() |
| `AGENT_EVENT_MODEL_RESPONSE` | model.complete() 返回 |
| `AGENT_EVENT_TOOL_CALL` | 即将执行工具 |
| `AGENT_EVENT_TOOL_RESULT` | 工具执行完成 |
| `AGENT_EVENT_ERROR` | 致命错误 |
| `AGENT_EVENT_CANCELLED` | 请求被取消 |
| `AGENT_EVENT_TIMEOUT` | 请求超时 |

**调用关系**: → `agent_runtime_now_ms()`, `agent->event_cb()`

**性能考量**: callback 为 NULL 时仅一次指针检查即返回，零开销。

**使用示例**:
```c
/* 通常不直接调用，由 agent 内部模块自动 emit */
/* 应用层通过 agent_set_event_callback 接收事件 */
/* demo 中的完整事件回调实现见 2.13 节 */

/* 事件触发时序示例:
 * [event] RUN_START     iter=0  → agent_run 入口
 * [event] ITER_START    iter=1  → 第 1 轮迭代
 * [event] MODEL_REQ     iter=1  → 即将调用模型
 * [event] MODEL_RESP    iter=1  → 模型返回（含 tool_calls）
 * [event] TOOL_CALL     iter=1  → 即将执行工具 "echo"
 * [event] TOOL_RESULT   iter=1  → 工具执行完成
 * [event] ITER_START    iter=2  → 第 2 轮迭代
 * [event] MODEL_REQ     iter=2  → 即将调用模型
 * [event] MODEL_RESP    iter=2  → 模型返回（最终回复）
 * [event] RUN_DONE      iter=2  → agent_run 出口
 */
```

---

## 5. 配置加载 (config_load.c)

> 文件路径: `src/core/config_load.c`
> 头文件: `include/cagent/config.h`

### 5.1 `agent_config_load`

```c
int agent_config_load(const char *path,
                      const char *env_prefix,
                      agent_config_kv_fn callback,
                      void *user_data);
```

**功能**: 从 key-value 文本源加载配置，支持文件 + 环境变量。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| path | `const char *` | 配置文件路径，NULL 跳过文件读取 |
| env_prefix | `const char *` | 环境变量前缀，如 `"CAGENT_"`，NULL 跳过环境变量 |
| callback | `agent_config_kv_fn` | key-value 回调，不可为 NULL |
| user_data | `void *` | 透传给 callback |

**返回值**: `AGENT_OK` 或 `agent_error_t`。

**解析规则**:
- 每行格式: `key=value`
- 行首/行尾空白被 trim
- 空行和 `#` 开头的行被忽略
- 环境变量名 = `env_prefix` + KEY 大写，非字母数字转下划线
- 环境变量优先级高于文件（后加载覆盖）

**内置环境变量 key**: `api_key`, `host`, `path`, `port`, `model`, `timeout_ms`

**调用关系**: → `load_file()`, `load_env()`

**使用示例**:
```c
/* demo 中的配置加载实现 */
typedef struct {
    char host[128];
    char api_key[256];
    char model[64];
    uint32_t timeout_ms;
} app_config_t;

static int app_kv_handler(const char *key, const char *value, void *user_data)
{
    app_config_t *cfg = (app_config_t *)user_data;

    if (strcmp(key, "api_key") == 0) {
        strncpy(cfg->api_key, value, sizeof(cfg->api_key) - 1);
    } else if (strcmp(key, "host") == 0) {
        strncpy(cfg->host, value, sizeof(cfg->host) - 1);
    } else if (strcmp(key, "model") == 0) {
        strncpy(cfg->model, value, sizeof(cfg->model) - 1);
    } else if (strcmp(key, "timeout_ms") == 0) {
        cfg->timeout_ms = (uint32_t)strtoul(value, NULL, 10);
    }

    return 0;
}

/* 从文件 + 环境变量加载配置 */
app_config_t cfg = {0};
strncpy(cfg.host, "api.deepseek.com", sizeof(cfg.host) - 1);
strncpy(cfg.model, "deepseek-v4-flash", sizeof(cfg.model) - 1);
cfg.timeout_ms = 30000;

agent_config_load("/data/cagent_openai.conf", "CAGENT_OPENAI_",
                  app_kv_handler, &cfg);

/* 环境变量 CAGENT_OPENAI_API_KEY, CAGENT_OPENAI_HOST 等会覆盖文件值 */
```

---

### 5.2 内部辅助函数

| 函数 | 功能 |
|------|------|
| `trim_inplace(s)` | 原地去除字符串首尾空白 |
| `load_file(path, callback, user_data)` | 逐行解析配置文件 |
| `key_to_env_name(prefix, key, buf, buf_size)` | 将 key 转换为环境变量名 |
| `load_env(prefix, callback, user_data)` | 遍历内置 key 读取环境变量 |

---

## 6. 上下文构建 (context_builder.c)

> 文件路径: `src/core/context_builder.c`
> 头文件: `include/cagent/context.h`

### 6.1 `agent_context_build`

```c
int agent_context_build(agent_t *agent,
                        const agent_request_t *request,
                        char *buffer,
                        size_t buffer_size,
                        size_t *written);
```

**功能**: 构建发送给模型的系统消息（system message）。

**构建顺序**:
1. `config.system_prompt`（CRITICAL 级，不可裁剪）
2. 已注册 context providers（按 priority 降序，同优先级按注册顺序）
3. 已启用 skills（通过 `agent_skill_context_build()`）

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| agent | `agent_t *` | agent 实例 |
| request | `const agent_request_t *` | 当前请求（当前未使用） |
| buffer | `char *` | 输出缓冲区 |
| buffer_size | `size_t` | 缓冲区大小 |
| written | `size_t *` | 实际写入字节数，可为 NULL |

**返回值**: `AGENT_OK`、`AGENT_ERROR_INVALID` 或 `AGENT_ERROR_CONTEXT_OVERFLOW`。

**调用关系**: → `append_text()`, `append_registered_providers()`, `agent_skill_context_build()`

**异常处理**:
- CRITICAL provider 溢出 → 返回 `AGENT_ERROR_CONTEXT_OVERFLOW`
- 非 CRITICAL provider 溢出 → 跳过该 provider，继续构建

**使用示例**:
```c
/* 通常不直接调用，由 agent_loop_run 内部调用 */
/* 上下文构建顺序: system_prompt → context_providers → skills */
/* 可通过 agent_register_context_provider 添加自定义上下文 */
```

---

### 6.2 `agent_register_context_provider`

```c
int agent_register_context_provider(agent_t *agent,
                                    const agent_context_provider_t *provider);
```

**功能**: 注册 context provider。name 重复或达到上限时返回错误。

**返回值**: `AGENT_OK`、`AGENT_ERROR_INVALID` 或 `AGENT_ERROR_LIMIT`。

**使用示例**:
```c
/* 注册设备状态上下文 provider，让模型了解设备当前状态 */
static int device_status_provider(const agent_t *agent,
                                  char *buffer, size_t size,
                                  size_t *written, void *user_data)
{
    (void)agent; (void)user_data;
    const char *status = get_device_status_json();  /* 用户自定义 */
    size_t len = strlen(status);
    if (len >= size) { len = size - 1; }
    memcpy(buffer, status, len);
    buffer[len] = '\0';
    if (written) { *written = len; }
    return AGENT_OK;
}

agent_context_provider_t provider = {
    .name = "device_status",
    .priority = 100,
    .flags = AGENT_CONTEXT_FLAG_CRITICAL,
    .provide = device_status_provider,
    .user_data = NULL
};
agent_register_context_provider(agent, &provider);
```

---

### 6.3 `agent_unregister_context_provider`

```c
int agent_unregister_context_provider(agent_t *agent, const char *name);
```

**功能**: 注销 context provider，通过 memmove 保持数组连续。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_NOTFOUND`。

**使用示例**:
```c
agent_unregister_context_provider(agent, "device_status");
```

---

## 7. 模型抽象层 (model.c)

> 文件路径: `src/llm/model.c`
> 头文件: `include/cagent/model.h`

### 7.1 `agent_model_create`

```c
agent_model_t *agent_model_create(const agent_model_ops_t *ops, void *provider);
```

**功能**: 创建模型句柄。使用标准 `malloc` 分配。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| ops | `const agent_model_ops_t *` | provider 操作集，`complete` 不可为 NULL |
| provider | `void *` | provider 上下文，传给 ops 回调 |

**返回值**: 成功返回模型指针，失败返回 NULL。

**使用示例**:
```c
/* 自定义模型 provider（如本地推理引擎） */
static int my_model_complete(void *provider_data,
                             agent_runtime_t *runtime,
                             const agent_model_request_t *request,
                             agent_model_response_t *response)
{
    my_model_ctx_t *ctx = (my_model_ctx_t *)provider_data;
    /* ... 调用本地推理引擎 ... */
    response->content = "local inference result";
    response->finish_reason = AGENT_FINISH_STOP;
    return AGENT_OK;
}

static void my_model_destroy(void *provider_data)
{
    my_model_ctx_t *ctx = (my_model_ctx_t *)provider_data;
    /* ... 释放推理引擎资源 ... */
}

agent_model_ops_t ops = {
    .complete = my_model_complete,
    .cancel = NULL,
    .destroy = my_model_destroy
};

my_model_ctx_t ctx = {0};
agent_model_t *model = agent_model_create(&ops, &ctx);
agent_set_model_owned(agent, model);
```

---

### 7.2 `agent_model_destroy`

```c
void agent_model_destroy(agent_model_t *model);
```

**功能**: 销毁模型句柄。调用 `ops.destroy()`（如存在），然后 `free(model)`。

**使用示例**:
```c
/* 手动管理模型生命周期时需要显式销毁 */
agent_model_t *model = agent_model_openai_create_simple("api.deepseek.com",
                                                         "sk-xxx",
                                                         "deepseek-v4-flash");
agent_set_model(agent, model);  /* 非_owned，需手动销毁 */

agent_run_simple(agent, "Hello", output, sizeof(output));

agent_model_destroy(model);  /* 手动销毁 */
agent_destroy(agent);
```

---

### 7.3 `agent_model_complete`

```c
int agent_model_complete(agent_model_t *model,
                         agent_runtime_t *runtime,
                         const agent_model_request_t *request,
                         agent_model_response_t *response);
```

**功能**: 执行一次模型推理。供 core 或上层 router provider 复用。

**返回值**: `ops.complete()` 的返回值，或 `AGENT_ERROR_INVALID`。

**调用关系**: → `model->ops.complete()`

**使用示例**:
```c
/* 直接调用模型推理（不经过 ReAct loop） */
agent_model_request_t req = {0};
agent_model_response_t resp = {0};

req.system_message = "You are a helpful assistant.";
req.user_message = "What is 2+2?";
req.max_tokens = 128;

int ret = agent_model_complete(model, &agent->runtime, &req, &resp);
if (ret == AGENT_OK) {
    printf("Model reply: %s\n", resp.content);
}
```

---

### 7.4 `agent_model_cancel`

```c
int agent_model_cancel(agent_model_t *model);
```

**功能**: 请求取消模型调用。provider 未实现 cancel 时返回 `AGENT_ERROR_NOTSUP`。

**使用示例**:
```c
/* 从另一个线程取消正在进行的模型调用 */
agent_model_cancel(agent_get_model(agent));
```

---

## 8. OpenAI 兼容模型 (model_openai.c)

> 文件路径: `src/llm/model_openai.c`
> 头文件: `include/cagent/model_openai.h`

### 8.1 `agent_model_openai_config_default`

```c
agent_model_openai_config_t agent_model_openai_config_default(void);
```

**功能**: 生成 OpenAI 模型默认配置。

**默认值**:
- `path`: `"/v1/chat/completions"`
- `port`: `"443"`
- `timeout_ms`: `30000`
- 其余字段为 0/NULL

**使用示例**:
```c
/* 获取默认配置后自定义 */
agent_model_openai_config_t cfg = agent_model_openai_config_default();
cfg.host = "api.deepseek.com";
cfg.api_key = "sk-xxx";
cfg.model = "deepseek-v4-flash";
cfg.timeout_ms = 60000;  /* 覆盖默认超时 */
agent_model_t *model = agent_model_openai_create(&cfg);
```

---

### 8.2 `agent_model_openai_create`

```c
agent_model_t *agent_model_openai_create(const agent_model_openai_config_t *config);
```

**功能**: 创建 OpenAI-compatible 模型实例。

**核心逻辑**:
1. 分配 `openai_compat_provider_t`，设置 magic
2. 拷贝 host/path/port/api_key/model 到内嵌缓冲区
3. 创建 `agent_model_t`，ops 为 `openai_compat_complete/cancel/destroy`

**返回值**: 成功返回模型指针，失败返回 NULL。

**使用示例**:
```c
/* 完整配置创建 OpenAI 模型 */
agent_model_openai_config_t cfg = agent_model_openai_config_default();
cfg.host = "api.deepseek.com";
cfg.path = "/v1/chat/completions";
cfg.port = "443";
cfg.api_key = "sk-xxx";
cfg.model = "deepseek-v4-flash";
cfg.timeout_ms = 45000;

agent_model_t *model = agent_model_openai_create(&cfg);
if (!model) {
    fprintf(stderr, "Failed to create OpenAI model\n");
    return EXIT_FAILURE;
}
agent_set_model_owned(agent, model);
```

---

### 8.3 `agent_model_openai_create_simple`

```c
agent_model_t *agent_model_openai_create_simple(const char *host,
                                                 const char *api_key,
                                                 const char *model);
```

**功能**: 便捷创建 OpenAI 模型，只需 host/api_key/model 三个必填参数。

**调用关系**: → `agent_model_openai_config_default()`, `agent_model_openai_create()`

**使用示例**:
```c
/* 最简方式创建 OpenAI 模型 */
agent_model_t *model = agent_model_openai_create_simple(
    "api.deepseek.com",     /* host */
    "sk-xxx",               /* api_key */
    "deepseek-v4-flash"     /* model name */
);
agent_set_model_owned(agent, model);
```

---

### 8.4 `agent_attach_openai`

```c
int agent_attach_openai(agent_t *agent,
                        const char *host,
                        const char *api_key,
                        const char *model);
```

**功能**: 便捷创建 OpenAI 模型并绑定到 agent（转移所有权）。

**调用关系**: → `agent_model_openai_create_simple()`, `agent_set_model_owned()`

**使用示例**:
```c
agent_attach_openai(agent, "api.deepseek.com", "sk-xxx", "deepseek-v4-flash");
```

---

### 8.5 `openai_compat_complete` (内部)

```c
static int openai_compat_complete(void *provider_data,
                                  agent_runtime_t *runtime,
                                  const agent_model_request_t *request,
                                  agent_model_response_t *response);
```

**功能**: OpenAI-compatible 模型推理实现。

**核心逻辑**:
1. 参数校验（magic、必填字段）
2. 分配 request body 缓冲区，调用 `build_request_json()` 构建 JSON
3. 分配 response body 缓冲区
4. 构建 HTTP headers（Content-Type + Authorization Bearer）
5. 调用 `agent_runtime_http_post()` 发送请求
6. 检查 cancel flag
7. 检查 HTTP 状态码（非 2xx 返回 `AGENT_ERROR_NETWORK`）
8. 调用 `extract_tool_calls()` 解析工具调用
9. 有 tool_calls → 填充 response.tool_calls
10. 无 tool_calls → 调用 `extract_content_string()` 提取最终回复

**调用关系**: → `build_request_json()`, `agent_runtime_malloc()`, `agent_runtime_http_post()`, `agent_runtime_free()`, `extract_tool_calls()`, `extract_content_string()`

**异常处理**:
- `AGENT_ERROR_INVALID`: 参数缺失或 magic 不匹配
- `AGENT_ERROR_NOMEM`: 缓冲区分配失败
- `AGENT_ERROR_CANCELLED`: 请求被取消
- `AGENT_ERROR_NETWORK`: HTTP 请求失败或状态码非 2xx
- `AGENT_ERROR_PARSE`: 响应 JSON 解析失败

---

### 8.6 JSON 构建与解析辅助函数

| 函数 | 功能 |
|------|------|
| `build_request_json(provider, request, buffer, buffer_size)` | 构建 Chat Completions JSON body |
| `json_escape_copy(src, dst, dst_size)` | JSON 字符串转义拷贝 |
| `alloc_escaped(text)` | 分配并转义 JSON 字符串 |
| `extract_content_string(json)` | 从响应中提取 content 字段 |
| `extract_tool_calls(provider, json, count)` | 从响应中解析 tool_calls 数组 |
| `parse_tool_call_item(provider, item, index)` | 解析单个 tool_call 对象 |
| `find_key(json, key)` | 在 JSON 中查找 key 的值位置 |
| `parse_json_string_inplace(p)` | 原地解析 JSON 字符串（处理转义） |
| `find_matching_json_end(p)` | 查找匹配的 JSON 对象/数组结束位置 |
| `copy_json_value(dst, dst_size, value)` | 拷贝 JSON 值到目标缓冲区 |

---

### 8.7 `agent_model_openai_set_backend`

```c
int agent_model_openai_set_backend(agent_model_t *model,
                                   const char *host,
                                   const char *path,
                                   const char *port);
```

**功能**: 运行时修改 OpenAI 模型的后端地址。

**返回值**: `AGENT_OK`、`AGENT_ERROR_INVALID` 或 `AGENT_ERROR_NOTSUP`（非 OpenAI 模型）。

**使用示例**:
```c
/* demo 中：当配置文件指定了非默认 path 或 port 时，覆盖后端 */
if (strcmp(cfg.path, "/v1/chat/completions") != 0 ||
    strcmp(cfg.port, "443") != 0) {
    agent_model_openai_set_backend(agent_get_model(agent),
                                   cfg.host, cfg.path, cfg.port);
}

/* 切换到其他 OpenAI 兼容服务 */
agent_model_openai_set_backend(agent_get_model(agent),
                               "api.openai.com",
                               "/v1/chat/completions",
                               "443");
```

---

### 8.8 `agent_model_openai_set_api_key` / `agent_model_openai_set_model`

```c
int agent_model_openai_set_api_key(agent_model_t *model, const char *api_key);
int agent_model_openai_set_model(agent_model_t *model, const char *model_name);
```

**功能**: 运行时修改 API key 或模型名称。

**使用示例**:
```c
/* 运行时切换模型 */
agent_model_openai_set_model(agent_get_model(agent), "gpt-4o");

/* 运行时更新 API key（如 key 轮换） */
agent_model_openai_set_api_key(agent_get_model(agent), "sk-new-key");
```

---

## 9. Mock 模型 (model_mock.c)

> 文件路径: `src/llm/model_mock.c`
> 头文件: `include/cagent/model.h`

### 9.1 `agent_model_mock_create`

```c
agent_model_t *agent_model_mock_create(const agent_model_mock_config_t *config,
                                       agent_model_mock_t **mock_out);
```

**功能**: 创建脚本式 mock model，用于测试。

**参数**:
| 参数 | 类型 | 说明 |
|------|------|------|
| config | `const agent_model_mock_config_t *` | 步骤配置，NULL 使用默认脚本 |
| mock_out | `agent_model_mock_t **` | 输出 mock 内部指针，可用于读取调用次数 |

**默认脚本**:
1. 步骤 1: TOOL_CALL（name="demo_echo", args=`{"value":"demo_tool_ok"}`）
2. 步骤 2: FINAL（content="mock final reply"）

**返回值**: 成功返回模型指针，失败返回 NULL。

**使用示例**:
```c
/* 使用默认脚本创建 mock model（用于测试） */
agent_model_mock_t *mock;
agent_model_t *model = agent_model_mock_create(NULL, &mock);
agent_set_model_owned(agent, model);

/* 自定义脚本：先调用工具，再返回最终回复 */
agent_model_mock_step_t steps[] = {
    { .type = AGENT_MOCK_TOOL_CALL,
      .tool_name = "get_time",
      .tool_args = "{}" },
    { .type = AGENT_MOCK_FINAL,
      .content = "The current time has been retrieved." }
};
agent_model_mock_config_t cfg = {
    .steps = steps,
    .step_count = 2
};
model = agent_model_mock_create(&cfg, &mock);
agent_set_model_owned(agent, model);

/* 运行后检查调用次数 */
agent_run_simple(agent, "test input", output, sizeof(output));
printf("Mock called %u times\n", agent_model_mock_call_count(mock));
```

---

### 9.2 `agent_model_mock_call_count`

```c
uint32_t agent_model_mock_call_count(const agent_model_mock_t *mock);
```

**功能**: 读取 mock model 的 complete() 调用次数。

**使用示例**:
```c
agent_model_mock_t *mock;
agent_model_t *model = agent_model_mock_create(NULL, &mock);
agent_set_model_owned(agent, model);

agent_run_simple(agent, "test", output, sizeof(output));
assert(agent_model_mock_call_count(mock) == 2);  /* 默认脚本调用 2 次 */
```

---

## 10. LLM 响应解析 (llm_parse.c)

> 文件路径: `src/llm/llm_parse.c`

### 10.1 `llm_parse_response_text`

```c
int llm_parse_response_text(const char *text, agent_llm_parse_result_t *result);
```

**功能**: 解析 LLM 文本响应，支持 mock/internal 格式和极小 OpenAI-compatible 子集。

**支持的格式**:
- `final|<content>` → 最终回复
- `tool|<id>|<name>|<args>` → 工具调用
- JSON 含 `"content"` 字段 → 提取 content

**调用关系**: → `after_prefix()`, `parse_tool_pipe()`, `find_json_string_value()`

**使用示例**:
```c
/* 通常不直接调用，由 model provider 内部使用 */
agent_llm_parse_result_t result;
int ret = llm_parse_response_text("final|Hello world", &result);
/* result.type = AGENT_LLM_PARSE_FINAL, result.content = "Hello world" */

ret = llm_parse_response_text("tool|call_1|echo|{\"message\":\"hi\"}", &result);
/* result.type = AGENT_LLM_PARSE_TOOL_CALL, result.tool_call.name = "echo" */
```

---

## 11. Runtime 抽象层 (runtime.c)

> 文件路径: `src/runtime/runtime.c`
> 内部头文件: `src/runtime/runtime.h`

### 11.1 `agent_runtime_fill_platform`

```c
void agent_runtime_fill_platform(agent_runtime_t *runtime);
```

**功能**: 填充平台特定 runtime 回调。根据编译配置自动选择：
- 启用 `CAGENT_RUNTIME_OPENVELA` 时调用 `agent_runtime_openvela_fill()`
- 否则不做任何操作

已设置的回调不被覆盖。`agent_create()` 在 `malloc_fn` 为 NULL 时自动调用。

**使用示例**:
```c
/* 通常不直接调用，agent_create 内部自动调用 */
/* 启用 CONFIG_CAGENT_RUNTIME_OPENVELA 后，自动填充 OpenVela 平台回调 */
/* 如需手动控制，可在 agent_create 前填充自定义 runtime */
agent_config_t cfg = agent_config_default();
cfg.runtime.malloc_fn = my_custom_malloc;  /* 自定义 malloc */
cfg.runtime.free_fn = my_custom_free;      /* 自定义 free */
agent_runtime_fill_platform(&cfg.runtime); /* 填充其余平台回调 */
agent_runtime_fill_defaults(&cfg.runtime); /* 填充 POSIX fallback */
agent_t *agent = agent_create(&cfg);
```

---

### 11.2 `agent_runtime_fill_defaults`

```c
void agent_runtime_fill_defaults(agent_runtime_t *runtime);
```

**功能**: 为 runtime 中 NULL 的回调填充 POSIX fallback。

| 回调 | Fallback 实现 |
|------|---------------|
| `malloc_fn` | `malloc()` |
| `free_fn` | `free()` |
| `now_ms` | `time(NULL) * 1000`（秒级精度） |
| `log` | `fprintf(stderr, ...)` |
| `http_post` | 返回 `AGENT_ERROR_NOTSUP` |

**使用示例**:
```c
/* 通常不直接调用，agent_create 内部自动调用 */
/* POSIX fallback 提供基本的 malloc/free/log/now_ms */
/* http_post 默认返回 NOTSUP，需启用平台适配层（如 OpenVela） */
```

---

### 11.3 Runtime 封装函数

所有封装函数提供 NULL 安全检查，runtime 或回调为 NULL 时返回安全默认值。

| 函数 | 功能 | NULL 时行为 |
|------|------|-------------|
| `agent_runtime_malloc(runtime, size)` | 内存分配 | 返回 NULL |
| `agent_runtime_free(runtime, ptr)` | 内存释放 | 不操作 |
| `agent_runtime_now_ms(runtime)` | 获取单调时钟 | 返回 0 |
| `agent_runtime_sleep_ms(runtime, ms)` | 休眠 | 不操作 |
| `agent_runtime_log(runtime, level, tag, msg)` | 日志输出 | 不操作 |
| `agent_runtime_http_post(runtime, req, resp)` | HTTP POST | 返回 `AGENT_ERROR_NOTSUP` |
| `agent_runtime_mutex_lock(runtime, mutex, timeout_ms)` | 加锁 | 返回 `AGENT_OK`（单线程模式） |
| `agent_runtime_mutex_unlock(runtime, mutex)` | 解锁 | 不操作 |
| `agent_runtime_enter_critical(runtime)` | 进临界区 | 返回 0 |
| `agent_runtime_exit_critical(runtime, state)` | 恢复临界区 | 不操作 |

**使用示例**:
```c
/* Runtime 封装函数由 cAGENT 内部模块调用，应用层通常不直接使用 */
/* 示例：内部模块如何使用 runtime 封装 */
void *buf = agent_runtime_malloc(&agent->runtime, 1024);  /* 分配内存 */
agent_runtime_log(&agent->runtime, 2, "TAG", "message");  /* INFO 日志 */
uint64_t now = agent_runtime_now_ms(&agent->runtime);      /* 获取时间 */
agent_runtime_free(&agent->runtime, buf);                  /* 释放内存 */
```

---

## 12. OpenVela Runtime 适配 (runtime_openvela.c)

> 文件路径: `src/runtime/runtime_openvela.c`
> 头文件: `src/runtime/runtime_openvela.h`

### 12.1 `agent_runtime_openvela_fill`

```c
void agent_runtime_openvela_fill(agent_runtime_t *runtime);
```

**功能**: 用 OpenVela/NuttX 平台回调填充 agent_runtime_t。

**实现的回调**:

| 回调 | 实现 | 说明 |
|------|------|------|
| `malloc_fn` | `ov_malloc` | 标准 `malloc()` |
| `free_fn` | `ov_free` | 标准 `free()` |
| `now_ms` | `ov_now_ms` | `clock_gettime(CLOCK_MONOTONIC)`，毫秒精度 |
| `sleep_ms` | `ov_sleep_ms` | `usleep()` |
| `log` | `ov_log` | `syslog()`，level 映射：0→ERR, 1→WARNING, 2→INFO, 其他→DEBUG |
| `http_post` | `ov_http_post` | mbedTLS HTTPS（需 `CONFIG_CAGENT_RUNTIME_OPENVELA_TLS`） |
| `mutex_create` | `ov_mutex_create` | `pthread_mutex_t` |
| `mutex_destroy` | `ov_mutex_destroy` | `pthread_mutex_destroy` + `free` |
| `mutex_lock` | `ov_mutex_lock` | `pthread_mutex_lock` 或 `pthread_mutex_timedlock`（Linux） |
| `mutex_unlock` | `ov_mutex_unlock` | `pthread_mutex_unlock` |
| `enter_critical` | `ov_enter_critical` | 全局 `pthread_mutex_t`（非 ISR 安全，单核适用） |
| `exit_critical` | `ov_exit_critical` | 解锁全局 mutex |

**使用示例**:
```c
/* 通常不直接调用，由 agent_runtime_fill_platform 内部自动调用 */
/* 启用 CONFIG_CAGENT_RUNTIME_OPENVELA 后，agent_create 自动使用 */
/* 如需手动配置 OpenVela runtime */
agent_runtime_t runtime;
memset(&runtime, 0, sizeof(runtime));
agent_runtime_openvela_fill(&runtime);
agent_runtime_fill_defaults(&runtime);

/* 可在 .config 中启用 TLS 支持:
 * CONFIG_CAGENT_RUNTIME_OPENVELA=y
 * CONFIG_CAGENT_RUNTIME_OPENVELA_TLS=y
 */
```

---

### 12.2 `ov_http_post` (内部)

```c
static int ov_http_post(const agent_http_request_t *request,
                         agent_http_response_t *response,
                         void *user_data);
```

**功能**: 基于 mbedTLS 的 HTTPS POST 实现。

**核心流程**:
1. 参数校验
2. `ov_tls_connect()` — DNS 解析 + TCP 连接 + TLS 握手
3. `ov_tls_write_request()` — 写 HTTP/1.1 请求
4. `ov_tls_read_response()` — 读 HTTP/1.1 响应
5. `ov_tls_ctx_free()` — 清理 TLS 上下文

**调用关系**: → `ov_tls_connect()`, `ov_tls_write_request()`, `ov_tls_read_response()`, `ov_tls_ctx_free()`

**使用示例**:
```c
/* 内部函数，由 ov_http_post 调用，应用层不直接使用 */
/* 完整 HTTPS 请求流程:
 * 1. ov_tls_connect()   → DNS + TCP + TLS 握手
 * 2. ov_tls_write_request() → 写 HTTP/1.1 请求
 * 3. ov_tls_read_response() → 读 HTTP/1.1 响应
 * 4. ov_tls_ctx_free()  → 清理 TLS 上下文
 */
```

---

### 12.3 `ov_tls_connect` (内部)

```c
static int ov_tls_connect(ov_tls_ctx_t *ctx,
                           const char *host,
                           const char *port,
                           uint32_t timeout_ms);
```

**功能**: 建立 TLS 连接。

**核心流程**:
1. 初始化 mbedTLS 结构体（ssl、cfg、net、ctr_drbg、entropy）
2. `mbedtls_ctr_drbg_seed()` — 初始化随机数生成器
3. `mbedtls_net_connect()` — TCP 连接
4. 设置 socket 超时（`SO_RCVTIMEO` / `SO_SNDTIMEO`）
5. `mbedtls_ssl_config_defaults()` — 配置 TLS 客户端
6. 设置 TLS 1.2 最低版本，ALPN `http/1.1`
7. `mbedtls_ssl_handshake()` — TLS 握手
8. 日志输出协商结果（TLS 版本 + 密码套件）

**异常处理**:
- 任何 mbedTLS 步骤失败 → 日志输出错误码 → 返回 `AGENT_ERROR_NETWORK`

**使用示例**:
```c
/* 内部函数，由 ov_http_post 调用 */
/* TLS 连接过程: TCP connect → TLS 1.2+ 握手 → ALPN http/1.1 */
/* 失败时返回 AGENT_ERROR_NETWORK，日志输出 mbedTLS 错误码 */
```

---

### 12.4 `ov_tls_write_request` / `ov_tls_read_response` (内部)

**`ov_tls_write_request`**: 构建 HTTP/1.1 请求头 + body，通过 `mbedtls_ssl_write()` 发送。

**`ov_tls_read_response`**: 读取 HTTP/1.1 响应，支持：
- Content-Length 定长读取
- Transfer-Encoding: chunked 分块解码（`ov_decode_chunked()`）
- 连接关闭定界

**使用示例**:
```c
/* 内部函数，由 ov_http_post 调用 */
/* ov_tls_write_request: 构建 HTTP/1.1 请求头 + body，通过 mbedtls_ssl_write 发送 */
/* ov_tls_read_response: 读取 HTTP/1.1 响应，支持 Content-Length/chunked/close 三种定界 */
```

---

## 13. 工具注册表 (tool_registry.c)

> 文件路径: `src/tools/tool_registry.c`
> 头文件: `include/cagent/tools.h`

### 13.1 `agent_register_tool`

```c
int agent_register_tool(agent_t *agent, const agent_tool_t *tool);
```

**功能**: 注册工具到 agent。name 重复或达到上限时返回错误。

**校验**: `tool->name` 和 `tool->execute` 不可为 NULL。

**返回值**: `AGENT_OK`、`AGENT_ERROR_INVALID` 或 `AGENT_ERROR_LIMIT`。

**副作用**: 调用 `agent_tool_schema_mark_dirty()` 使 schema 缓存失效。

**使用示例**:
```c
/* 使用完整结构体注册工具（适合需要精细控制的场景） */
static int my_tool_execute(const agent_tool_call_t *call,
                           agent_tool_result_t *result,
                           void *user_data)
{
    result->status = AGENT_OK;
    result->content_json = "{\"result\":42}";
    result->error_message = NULL;
    return AGENT_OK;
}

agent_tool_t tool = {
    .name = "my_tool",
    .description = "A custom tool",
    .input_schema_json = "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\"}}}",
    .execute = my_tool_execute,
    .user_data = NULL,
    .flags = AGENT_TOOL_FLAG_LLM_VISIBLE | AGENT_TOOL_FLAG_READ_ONLY
};
agent_register_tool(agent, &tool);
```

---

### 13.2 `agent_register_tool_simple`

```c
int agent_register_tool_simple(agent_t *agent,
                                const char *name,
                                const char *description,
                                const char *input_schema_json,
                                agent_tool_fn execute,
                                void *user_data,
                                uint32_t flags);
```

**功能**: 便捷注册工具（参数式），自动填充 `agent_tool_t` 结构体。

**调用关系**: → `agent_register_tool()`

**使用示例**:
```c
/* demo 中的工具注册：echo 工具 */
static int tool_echo_execute(const agent_tool_call_t *call,
                             agent_tool_result_t *result,
                             void *user_data)
{
    (void)user_data;
    printf("[tool] echo called, args: %s\n",
           call->arguments_json ? call->arguments_json : "(null)");
    result->status = AGENT_OK;
    result->content_json = call->arguments_json
                               ? call->arguments_json
                               : "{\"echo\":\"(empty)\"}";
    result->error_message = NULL;
    return AGENT_OK;
}

static const char *const SCHEMA_ECHO =
    "{\"type\":\"object\",\"properties\":{"
    "\"message\":{\"type\":\"string\"}},"
    "\"required\":[\"message\"],"
    "\"additionalProperties\":false}";

uint32_t read_only = AGENT_TOOL_FLAG_LLM_VISIBLE | AGENT_TOOL_FLAG_READ_ONLY;
agent_register_tool_simple(agent, "echo",
    "Echo back the input arguments as-is.",
    SCHEMA_ECHO, tool_echo_execute, NULL, read_only);

/* demo 中的工具注册：calc 工具 */
static int tool_calc_execute(const agent_tool_call_t *call,
                             agent_tool_result_t *result,
                             void *user_data)
{
    const char *args = call->arguments_json ? call->arguments_json : "{}";
    int a = 0, b = 0;
    char op = '+';
    const char *p;

    p = strstr(args, "\"a\"");
    if (p) { sscanf(p, "\"a\":%d", &a); }
    p = strstr(args, "\"b\"");
    if (p) { sscanf(p, "\"b\":%d", &b); }
    p = strstr(args, "\"op\"");
    if (p) {
        const char *q = strchr(p + 4, '"');
        if (q) { q = strchr(q + 1, '"'); if (q) { op = *(q + 1); } }
    }

    static char buf[64];
    int value = (op == '+') ? a + b :
                (op == '-') ? a - b :
                (op == '*') ? a * b :
                (b != 0)   ? a / b : 0;
    snprintf(buf, sizeof(buf), "{\"result\":%d}", value);

    result->status = AGENT_OK;
    result->content_json = buf;
    result->error_message = NULL;
    return AGENT_OK;
}

static const char *const SCHEMA_CALC =
    "{\"type\":\"object\",\"properties\":{"
    "\"a\":{\"type\":\"integer\"},"
    "\"b\":{\"type\":\"integer\"},"
    "\"op\":{\"type\":\"string\",\"enum\":[\"+\",\"-\",\"*\",\"/\"]}},"
    "\"required\":[\"a\",\"b\",\"op\"],"
    "\"additionalProperties\":false}";

agent_register_tool_simple(agent, "calc",
    "Perform simple arithmetic. Supported ops: +, -, *, /.",
    SCHEMA_CALC, tool_calc_execute, NULL, read_only);
```

---

### 13.3 `agent_unregister_tool`

```c
int agent_unregister_tool(agent_t *agent, const char *name);
```

**功能**: 注销工具，通过 memmove 保持数组连续。

**返回值**: `AGENT_OK` 或 `AGENT_ERROR_NOTFOUND`。

**使用示例**:
```c
/* 运行时动态卸载工具 */
agent_unregister_tool(agent, "echo");
/* schema 缓存自动失效，下次 agent_run 时重新构建 */
```

---

### 13.4 `agent_tool_set_enabled`

```c
int agent_tool_set_enabled(agent_t *agent, const char *name, int enabled);
```

**功能**: 启用/禁用工具。禁用后不参与 schema 生成和执行。

**使用示例**:
```c
/* 临时禁用有副作用的工具 */
agent_tool_set_enabled(agent, "ota_update", 0);

/* 安全模式下只启用只读工具 */
agent_tool_set_enabled(agent, "reboot", 0);
agent_tool_set_enabled(agent, "factory_reset", 0);

/* 重新启用 */
agent_tool_set_enabled(agent, "ota_update", 1);
```

---

### 13.5 `agent_tool_is_enabled`

```c
int agent_tool_is_enabled(const agent_t *agent, const char *name, int *enabled);
```

**功能**: 查询单个工具当前是否启用。`enabled` 输出 `1` 表示启用，`0` 表示禁用。

**返回值**:

| 返回值 | 说明 |
|--------|------|
| `AGENT_OK` | 查询成功 |
| `AGENT_ERROR_INVALID` | 参数非法 |
| `AGENT_ERROR_NOTFOUND` | 工具不存在 |

**使用示例**:
```c
int enabled = 0;

if (agent_tool_is_enabled(agent, "set_ac", &enabled) == AGENT_OK &&
    enabled) {
    /* set_ac 当前可用 */
}
```

---

### 13.6 内部查找函数

| 函数 | 功能 |
|------|------|
| `agent_tool_registry_find(agent, name)` | 按名称查找工具（可变） |
| `agent_tool_registry_find_const(agent, name)` | 按名称查找工具（只读） |
| `agent_tool_entry_is_enabled(entry)` | 检查工具是否启用 |
| `agent_tool_entry_is_llm_visible(entry)` | 检查工具是否对 LLM 可见 |

---

## 14. 工具守卫与执行 (tool_guard.c)

> 文件路径: `src/tools/tool_guard.c`

### 14.1 `agent_tool_execute`

```c
int agent_tool_execute(agent_t *agent,
                       const agent_tool_call_t *call,
                       agent_tool_result_t *result);
```

**功能**: 执行单个工具调用，完整执行链：registry lookup → policy → guard → handler → 归一化 result。

**核心逻辑**:
1. 参数校验
2. `agent_tool_registry_find()` — 查找工具
3. 检查工具是否启用
4. 检查工具是否有 execute 回调
5. 检查参数大小限制（`CAGENT_TOOL_ARGS_MAX_SIZE`）
6. 检查工具调用次数限制（`limits.max_tool_calls`）
7. `check_policy()` — 策略检查
8. 递增调用计数，执行工具
9. 检查工具超时（协作式，执行后检测）
10. 归一化结果：成功时 `content_json` 为空则设为 `{"ok":true}`，检查输出大小限制

**调用关系**: → `agent_tool_registry_find()`, `check_policy()`, `entry->def.execute()`, `agent_runtime_now_ms()`, `set_tool_error()`

**异常处理**: 所有错误通过 `set_tool_error()` 生成结构化 JSON 错误结果，返回对应错误码。工具级错误不会终止 ReAct loop，而是以 tool result 写回 session。

**使用示例**:
```c
/* 通常不直接调用，由 agent_loop_run 内部调用 */
/* 工具执行链: registry lookup → policy → guard → handler → 归一化 result */
/* 错误时 result.content_json 为 {"error":"...","code":-11}，不终止 loop */
```

---

### 14.2 `check_policy` (内部)

```c
static int check_policy(agent_t *agent,
                        const agent_tool_call_t *call,
                        agent_tool_result_t *result);
```

**功能**: 调用策略回调检查工具调用是否允许。无策略回调时默认 ALLOW。

**决策**:
- `AGENT_POLICY_ALLOW` → 继续
- `AGENT_POLICY_DENY` / `AGENT_POLICY_REQUIRE_CONFIRM` → 返回 `AGENT_ERROR_POLICY_DENIED`

**使用示例**:
```c
/* 内部函数，由 agent_tool_execute 调用 */
/* 无策略回调时默认 ALLOW，有回调时由用户策略决定 */
/* 详见 2.14 agent_set_policy_callback 的策略回调示例 */
```

---

## 15. 工具 Schema 构建 (tool_schema.c)

> 文件路径: `src/tools/tool_schema.c`

### 15.1 `agent_tool_schema_build`

```c
int agent_tool_schema_build(agent_t *agent,
                            char *buffer,
                            size_t buffer_size,
                            size_t *written);
```

**功能**: 将已注册且 LLM 可见的工具转换为 OpenAI-compatible tools JSON 数组。

**缓存机制**: schema 缓存在 `agent->tool_schema_cache` 中，仅在 registry 变更时（`tool_schema_dirty == true`）重新构建。

**输出格式**:
```json
[
  {
    "type": "function",
    "function": {
      "name": "echo",
      "description": "Echo back input",
      "parameters": {"type":"object","properties":{...}}
    }
  }
]
```

**调用关系**: → `build_schema_uncached()`, `copy_schema_cache()`, `validate_schema_object()`

**使用示例**:
```c
/* 通常不直接调用，由 agent_loop_run 内部调用 */
/* 注册/注销工具后 schema 缓存自动失效，下次构建时重新生成 */
/* 输出格式为 OpenAI-compatible tools JSON 数组 */
```

---

### 15.2 JSON Writer 辅助

| 函数 | 功能 |
|------|------|
| `writer_putc(writer, c)` | 写入单字符 |
| `writer_append(writer, text)` | 追加字符串 |
| `writer_printf(writer, fmt, ...)` | 格式化追加 |
| `writer_append_json_string(writer, text)` | 追加 JSON 转义字符串 |

**使用示例**:
```c
/* 内部辅助函数，由 build_schema_uncached 使用 */
/* 用于构建 OpenAI-compatible tools JSON */
```

---

### 15.3 JSON 校验

`validate_schema_object(schema)` 使用递归下降解析器校验 `input_schema_json` 是否为合法 JSON 对象。支持 string、number、object、array、boolean、null 类型。

**使用示例**:
```c
/* 内部函数，由 agent_register_tool 调用 */
/* 注册工具时自动校验 input_schema_json 是否为合法 JSON */
/* 非法 JSON 时注册失败，返回 AGENT_ERROR_INVALID */
```

---

## 16. Skill 注册表 (skill_registry.c)

> 文件路径: `src/skills/skill_registry.c`
> 头文件: `include/cagent/skill.h`

### 16.1 `agent_register_skill`

```c
int agent_register_skill(agent_t *agent, const agent_skill_t *skill);
```

**功能**: 注册 skill。name 重复或达到上限时返回错误。

**校验**: `skill->name` 和 `skill->context_text` 不可为 NULL。

**使用示例**:
```c
/* 注册技能：为模型提供特定领域的知识和指令 */
agent_skill_t skill = {
    .name = "device_control",
    .description = "IoT device control skill",
    .context_text = "When controlling devices, always confirm with the user "
                    "before executing destructive actions. "
                    "Report device status after each action.",
    .priority = 100,
    .flags = 0,
    .user_data = NULL
};
agent_register_skill(agent, &skill);

/* 注册多个技能，priority 越高越先出现在上下文中 */
agent_skill_t safety_skill = {
    .name = "safety",
    .description = "Safety guidelines",
    .context_text = "Never perform actions that could damage hardware. "
                    "Always respect rate limits on API calls.",
    .priority = 200,  /* 比 device_control 更高优先级 */
    .flags = 0,
    .user_data = NULL
};
agent_register_skill(agent, &safety_skill);
```

---

### 16.2 `agent_unregister_skill`

```c
int agent_unregister_skill(agent_t *agent, const char *name);
```

**功能**: 注销 skill。

**使用示例**:
```c
agent_unregister_skill(agent, "device_control");
```

---

### 16.3 `agent_skill_context_build` (内部)

```c
int agent_skill_context_build(agent_t *agent,
                              char *buffer,
                              size_t buffer_size,
                              size_t *written);
```

**功能**: 构建所有已启用 skill 的上下文文本，按 priority 降序排列。

**输出格式**:
```
Skill: <name>
Description: <description>
Instructions:
<context_text>

Skill: <name2>
...
```

**使用示例**:
```c
/* 内部函数，由 agent_context_build 调用 */
/* 将所有已注册 skill 的上下文文本按优先级拼接 */
/* priority 越高越先出现在上下文中 */
```

---

## 17. Session 管理器 (session_mgr.c)

> 文件路径: `src/memory/session_mgr.c`
> 内部头文件: `src/memory/memory_internal.h`
> 公共头文件: `include/cagent/session.h`

### 17.1 `agent_session_find_or_create` (内部)

```c
agent_session_t *agent_session_find_or_create(agent_t *agent, const char *session_id);
```

**功能**: 按 id 查找 session，不存在时自动创建。session 数组已满时返回 NULL。

**使用示例**:
```c
/* 通常不直接调用，由 agent_session_append 内部使用 */
/* session_id 为 NULL 时使用 "default" */
```

---

### 17.2 `agent_session_append` (内部)

```c
int agent_session_append(agent_t *agent,
                         const char *session_id,
                         const agent_message_t *message);
```

**功能**: 追加消息到 session（公共 API 入口），根据 role 分发到内部 add 函数。

**使用示例**:
```c
/* 通常不直接调用，由 agent_loop_run 内部使用 */
/* 消息链完整性由内部函数保证 */
```

---

### 17.3 消息追加函数 (内部)

| 函数 | 功能 |
|------|------|
| `agent_session_add_user(agent, session, content)` | 追加 user 消息，标记 turn_boundary |
| `agent_session_add_assistant(agent, session, content)` | 追加 assistant final 消息，标记 turn 完成 |
| `agent_session_add_assistant_tool_calls(agent, session, calls, count)` | 追加 assistant tool_calls 消息 |
| `agent_session_add_tool(agent, session, tool_call_id, content)` | 追加 tool result 消息 |

**消息链完整性保证**:
- user 消息前必须无未完成 turn
- assistant final 消息前必须有未完成 turn 且无 pending tool_calls
- assistant tool_calls 消息前必须有未完成 turn 且无 pending tool_calls
- tool result 消息前必须有对应的 pending tool_call

**使用示例**:
```c
/* 内部函数，由 agent_session_append 根据 role 自动分发 */
/* 消息链完整性由框架保证，应用层无需关心 */
/* 消息追加顺序: user → assistant(tool_calls) → tool → assistant(final) */
```

---

### 17.4 `agent_session_evict_oldest_turn` (内部)

```c
int agent_session_evict_oldest_turn(agent_t *agent, agent_session_t *session);
```

**功能**: 淘汰最早的完整 turn。

**淘汰规则**:
1. 从头部扫描找到第一个 turn_boundary（user message）
2. 继续扫描找到对应的 assistant final 消息
3. 删除从 user 到 assistant(final) 的所有消息
4. 尾部未完成 turn 不参与淘汰
5. 无完整 turn 时返回 `AGENT_ERROR_LIMIT`

**使用示例**:
```c
/* 内部函数，当 session 消息数达到上限时由框架自动调用 */
/* 淘汰最早的完整对话轮次（user → assistant final） */
/* 尾部未完成的 turn（等待 tool result）不会被淘汰 */
/* 如需调整消息上限，修改 CAGENT_MAX_SESSION_MESSAGES 常量 */
```

---

### 17.5 `agent_session_build_model_messages` (内部)

```c
int agent_session_build_model_messages(agent_t *agent,
                                       agent_session_t *session,
                                       char *buffer,
                                       size_t buffer_size,
                                       size_t *written);
```

**功能**: 构建 provider 可消费的 OpenAI-compatible messages JSON 数组。

**输出格式**:
```json
[
  {"role":"user","content":"..."},
  {"role":"assistant","content":null,"tool_calls":[...]},
  {"role":"tool","tool_call_id":"...","content":"..."},
  {"role":"assistant","content":"final reply"}
]
```

**使用示例**:
```c
/* 内部函数，由 agent_loop_run 调用 */
/* 将 session 中的消息构建为 OpenAI-compatible JSON 数组 */
/* 包含完整的对话历史：user → assistant → tool → assistant → ... */
```

---

### 17.6 公共 Session API

| 函数 | 功能 |
|------|------|
| `agent_session_clear(agent, session_id)` | 清除指定 session 的所有消息 |
| `agent_session_clear_all(agent)` | 清除所有 session 的消息 |
| `agent_session_count(agent, count)` | 查询当前 session 数量 |

**使用示例**:
```c
/* 清除指定 session 的对话历史，开始新对话 */
agent_session_clear(agent, "default");

/* 清除所有 session */
agent_session_clear_all(agent);

/* 查询当前 session 数量 */
uint32_t count = 0;
agent_session_count(agent, &count);
printf("Active sessions: %u\n", count);

/* 使用不同 session_id 管理多轮对话 */
agent_request_t req1 = { .input = "Hello", .session_id = "user_1" };
agent_request_t req2 = { .input = "Hello", .session_id = "user_2" };
agent_run(agent, &req1, &resp1);
agent_run(agent, &req2, &resp2);
/* user_1 和 user_2 有独立的对话历史 */
```

---

## 18. Memory 快照 (memory_store.c)

> 文件路径: `src/memory/memory_store.c`
> 头文件: `include/cagent/memory.h`

### 18.1 `agent_memory_snapshot`

```c
int agent_memory_snapshot(agent_t *agent, agent_memory_snapshot_t *snapshot);
```

**功能**: 创建 agent 当前状态快照。**MVP 返回 `AGENT_ERROR_NOTSUP`**。

**使用示例**:
```c
/* MVP 阶段暂不支持，预留接口 */
agent_memory_snapshot_t snapshot;
int ret = agent_memory_snapshot(agent, &snapshot);
if (ret == AGENT_ERROR_NOTSUP) {
    printf("Snapshot not supported in this version\n");
}
```

---

### 18.2 `agent_memory_restore`

```c
int agent_memory_restore(agent_t *agent, const agent_memory_snapshot_t *snapshot);
```

**功能**: 从快照恢复 agent 状态。**MVP 返回 `AGENT_ERROR_NOTSUP`**。

**使用示例**:
```c
/* MVP 阶段暂不支持，预留接口 */
agent_memory_snapshot_t snapshot;
int ret = agent_memory_restore(agent, &snapshot);
/* 未来版本：从快照恢复对话历史、工具状态等 */
```

---

### 18.3 `agent_memory_snapshot_free`

```c
void agent_memory_snapshot_free(agent_t *agent, agent_memory_snapshot_t *snapshot);
```

**功能**: 释放快照数据。当前无操作。

**使用示例**:
```c
agent_memory_snapshot_t snapshot;
agent_memory_snapshot_free(agent, &snapshot);  /* 当前为空操作 */
```

---

## 19. 错误码参考

| 错误码 | 值 | 说明 |
|--------|-----|------|
| `AGENT_OK` | 0 | 成功 |
| `AGENT_ERROR` | -1 | 未分类错误 |
| `AGENT_ERROR_NOMEM` | -2 | 内存分配失败 |
| `AGENT_ERROR_INVALID` | -3 | 参数无效 |
| `AGENT_ERROR_BUSY` | -4 | agent 正忙，不可重入 |
| `AGENT_ERROR_LIMIT` | -5 | 迭代/缓冲区/消息上限溢出 |
| `AGENT_ERROR_TIMEOUT` | -6 | 整体/模型/工具超时 |
| `AGENT_ERROR_CANCELLED` | -7 | 外部请求取消 |
| `AGENT_ERROR_CONTEXT_OVERFLOW` | -8 | context 拼接溢出，不可静默截断 |
| `AGENT_ERROR_MODEL` | -9 | 模型 provider 返回错误 |
| `AGENT_ERROR_NETWORK` | -10 | 网络传输失败 |
| `AGENT_ERROR_TOOL` | -11 | 工具执行失败 |
| `AGENT_ERROR_POLICY_DENIED` | -12 | 策略回调拒绝 |
| `AGENT_ERROR_NOTFOUND` | -13 | 工具/skill/session 未找到 |
| `AGENT_ERROR_NOTSUP` | -14 | 功能未实现或未启用 |
| `AGENT_ERROR_PARSE` | -15 | JSON/模型响应解析失败 |

---

## 20. 编译期常量参考

所有常量支持三级映射：直接定义 > Kconfig (`CONFIG_*`) > 默认值。

| 常量 | 默认值 | 说明 |
|------|--------|------|
| `CAGENT_MAX_TOOLS` | 12 | 最大注册工具数 |
| `CAGENT_MAX_SKILLS` | 8 | 最大注册 skill 数 |
| `CAGENT_MAX_CONTEXT_PROVIDERS` | 8 | 最大 context provider 数 |
| `CAGENT_MAX_SESSIONS` | 4 | 最大 session 数 |
| `CAGENT_MAX_SESSION_MESSAGES` | 24 | 每 session 最大消息数 |
| `CAGENT_CONTEXT_BUFFER_SIZE` | 4096 | split buffer 的兼容基础大小 |
| `CAGENT_REQUEST_ARENA_EXTRA_SIZE` | 0 | 请求 arena 自动计算后的额外余量 |
| `CAGENT_TOOL_ARGS_MAX_SIZE` | 1024 | 工具参数 JSON 最大大小 |
| `CAGENT_TOOL_OUTPUT_MAX_SIZE` | 1024 | 工具输出 JSON 最大大小 |
| `CAGENT_SESSION_CONTENT_MAX_SIZE` | 512 | Session 消息内容缓冲区上限 |
| `CAGENT_SESSION_TOOL_CALL_MAX_SIZE` | 256 | Session 中每个 tool_call 序列化上限 |
| `CAGENT_SESSION_MAX_TOOL_CALLS` | 4 | assistant 消息中最大 tool_call 数 |
| `CAGENT_DEFAULT_MAX_STEPS` | 8 | 默认最大迭代轮数 |
| `CAGENT_DEFAULT_TIMEOUT_MS` | 30000 | 默认整体超时 |
| `CAGENT_DEFAULT_MODEL_TIMEOUT_MS` | 15000 | 默认模型调用超时 |
| `CAGENT_DEFAULT_TOOL_TIMEOUT_MS` | 3000 | 默认工具执行超时 |
| `CAGENT_DEFAULT_MAX_OUTPUT_TOKENS` | 512 | 默认模型响应 token 预算 |
| `CAGENT_MODEL_OPENAI_HOST_MAX` | 128 | OpenAI host 缓冲区大小 |
| `CAGENT_MODEL_OPENAI_PATH_MAX` | 128 | OpenAI path 缓冲区大小 |
| `CAGENT_MODEL_OPENAI_PORT_MAX` | 8 | OpenAI port 缓冲区大小 |
| `CAGENT_MODEL_OPENAI_KEY_MAX` | 256 | OpenAI API key 缓冲区大小 |
| `CAGENT_MODEL_OPENAI_MODEL_MAX` | 64 | OpenAI model name 缓冲区大小 |
| `CAGENT_MODEL_OPENAI_MAX_TOOL_CALLS` | 4 | OpenAI 响应最大 tool_call 数 |

---

## 21. 完整使用示例

> 以下示例基于 `cagent_demo`，展示所有公共 API 的典型组合用法。

### 21.1 最简用法（3 行代码启动 Agent）

```c
#include <agent.h>

int main(void)
{
    agent_t *agent = agent_create_simple("my_app",
        "You are a helpful assistant.");

    agent_attach_openai(agent, "api.deepseek.com",
                        "sk-xxx", "deepseek-v4-flash");

    char output[4096];
    int ret = agent_run_simple(agent, "Hello!", output, sizeof(output));
    printf("Result: %s\n", ret == AGENT_OK ? output : "error");

    agent_destroy(agent);
    return ret;
}
```

### 21.2 完整 Demo（含配置加载、事件回调、工具注册）

```c
#include <agent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 工具定义 ─────────────────────────────────────── */

static int tool_echo_execute(const agent_tool_call_t *call,
                             agent_tool_result_t *result,
                             void *user_data)
{
    (void)user_data;
    result->status = AGENT_OK;
    result->content_json = call->arguments_json
                               ? call->arguments_json
                               : "{\"echo\":\"(empty)\"}";
    result->error_message = NULL;
    return AGENT_OK;
}

static const char *const SCHEMA_ECHO =
    "{\"type\":\"object\",\"properties\":{"
    "\"message\":{\"type\":\"string\"}},"
    "\"required\":[\"message\"],"
    "\"additionalProperties\":false}";

static int tool_get_time_execute(const agent_tool_call_t *call,
                                 agent_tool_result_t *result,
                                 void *user_data)
{
    static char buf[64];
    (void)call; (void)user_data;
    uint64_t now_ms = (uint64_t)time(NULL) * 1000u;
    snprintf(buf, sizeof(buf), "{\"timestamp_ms\":%llu}",
             (unsigned long long)now_ms);
    result->status = AGENT_OK;
    result->content_json = buf;
    result->error_message = NULL;
    return AGENT_OK;
}

static const char *const SCHEMA_GET_TIME =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";

static void register_tools(agent_t *agent)
{
    uint32_t flags = AGENT_TOOL_FLAG_LLM_VISIBLE | AGENT_TOOL_FLAG_READ_ONLY;
    agent_register_tool_simple(agent, "echo",
        "Echo back the input arguments as-is.",
        SCHEMA_ECHO, tool_echo_execute, NULL, flags);
    agent_register_tool_simple(agent, "get_time",
        "Get the current device timestamp in milliseconds.",
        SCHEMA_GET_TIME, tool_get_time_execute, NULL, flags);
}

/* ── 事件回调 ─────────────────────────────────────── */

static void event_cb(const agent_event_t *event, void *user_data)
{
    (void)user_data;
    printf("[event] type=%d iter=%u err=%d msg=%s\n",
           event->type, event->iteration,
           event->error_code,
           event->message ? event->message : "-");
}

/* ── 配置加载 ─────────────────────────────────────── */

typedef struct {
    char host[128];
    char api_key[256];
    char model[64];
} app_config_t;

static int kv_handler(const char *key, const char *value, void *user_data)
{
    app_config_t *cfg = (app_config_t *)user_data;
    if (strcmp(key, "api_key") == 0) strncpy(cfg->api_key, value, sizeof(cfg->api_key) - 1);
    else if (strcmp(key, "host") == 0) strncpy(cfg->host, value, sizeof(cfg->host) - 1);
    else if (strcmp(key, "model") == 0) strncpy(cfg->model, value, sizeof(cfg->model) - 1);
    return 0;
}

/* ── 主函数 ───────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *input = (argc > 1) ? argv[1] : "What time is it?";

    /* 1. 加载配置 */
    app_config_t cfg = {0};
    strncpy(cfg.host, "api.deepseek.com", sizeof(cfg.host) - 1);
    strncpy(cfg.model, "deepseek-v4-flash", sizeof(cfg.model) - 1);
    agent_config_load("/data/cagent_openai.conf", "CAGENT_OPENAI_",
                      kv_handler, &cfg);

    if (cfg.model[0] == '\0') {
        fprintf(stderr, "model name required\n");
        return EXIT_FAILURE;
    }

    /* 2. 创建 Agent */
    agent_t *agent = agent_create_simple("demo",
        "You are a demo agent. Use tools when they help.");

    /* 3. 注册事件回调 */
    agent_set_event_callback(agent, event_cb, NULL);

    /* 4. 注册工具 */
    register_tools(agent);

    /* 5. 绑定模型（转移所有权，agent_destroy 自动销毁） */
    int ret = agent_attach_openai(agent, cfg.host, cfg.api_key, cfg.model);
    if (ret != AGENT_OK) {
        fprintf(stderr, "attach_openai failed: %d\n", ret);
        agent_destroy(agent);
        return EXIT_FAILURE;
    }

    /* 6. 运行推理 */
    char output[4096];
    ret = agent_run_simple(agent, input, output, sizeof(output));

    if (ret == AGENT_OK) {
        printf("\n[result] %s\n", output);
    } else {
        printf("\n[result] ERROR (%d): %s\n", ret, output);
    }

    /* 7. 查看统计 */
    agent_stats_t stats;
    agent_get_stats(agent, &stats);
    printf("[stats] model_calls=%u tool_calls=%u elapsed=%llums\n",
           stats.model_calls, stats.tool_calls,
           (unsigned long long)stats.last_run_elapsed_ms);

    /* 8. 清理 */
    agent_destroy(agent);
    return (ret == AGENT_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

### 21.3 高级用法（策略回调 + 自定义 Limits + 多 Session）

```c
#include <agent.h>

/* 策略回调：拒绝危险操作 */
static agent_policy_decision_t safety_policy(const agent_policy_request_t *req,
                                              void *user_data)
{
    (void)user_data;
    if (req->action == AGENT_POLICY_ACTION_TOOL_CALL) {
        if (strcmp(req->tool_call->name, "reboot") == 0 ||
            strcmp(req->tool_call->name, "factory_reset") == 0) {
            return AGENT_POLICY_DENY;
        }
    }
    return AGENT_POLICY_ALLOW;
}

void advanced_example(agent_t *agent)
{
    /* 设置策略回调 */
    agent_set_policy_callback(agent, safety_policy, NULL);

    /* 自定义 limits */
    agent_limits_t limits = {
        .max_steps = 4,
        .timeout_ms = 30000,
        .per_model_timeout_ms = 20000,
        .per_tool_timeout_ms = 5000,
        .max_tool_calls = 6,
        .max_output_tokens = 256
    };
    agent_set_limits(agent, &limits);

    /* 多 session 对话 */
    agent_request_t req;
    agent_response_t resp;
    char output[4096];

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    resp.output = output;
    resp.output_size = sizeof(output);

    /* Session A */
    req.input = "Hello from session A";
    req.session_id = "session_a";
    agent_run(agent, &req, &resp);

    /* Session B（独立对话历史） */
    req.input = "Hello from session B";
    req.session_id = "session_b";
    agent_run(agent, &req, &resp);

    /* 清除 Session A */
    agent_session_clear(agent, "session_a");

    /* 重置 Agent（清空所有 session 和 stats） */
    agent_reset(agent);
}
```
