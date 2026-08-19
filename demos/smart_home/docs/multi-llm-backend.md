# smart_home 多 LLM 后端与安全管控技术指导

## 文档定位

本文档是 smart_home demo 新增“多 LLM 后端适配与安全管控”能力的技术指导文档，
面向开发者说明如何在不大改 cAGENT core 的前提下，为 smart_home 提供可配置的
OpenAI-compatible 后端、统一的模型配置入口、稳定的工具调用行为，以及面向嵌入式
demo 的安全边界。

本文档不再只是设置页 dropdown 的实现草案，而是覆盖以下新增功能：

- 多 LLM 后端预设与自定义后端接入。
- 设置页运行时修改 host、path、port、model、API key、timeout 等参数。
- 不同后端在 tool calling、响应格式、超时和 buffer 上的兼容处理。
- 命令黑白名单、文件访问权限、tool 调用策略等安全机制。
- 面向 openvela/RTOS 场景的性能和内存优化建议。

## 当前方案审查结论

现有方案的核心方向是合理的：cAGENT 的 `agent_model_openai_create()` 已经把模型
后端抽象为 OpenAI Chat Completions 兼容 provider，smart_home 在应用层维护后端预设
表即可完成 DeepSeek、MiMo、通义千问、OpenAI 以及自定义 endpoint 的切换。

但原文档存在几个不足，需要补齐后才能作为新增功能的技术指导：

| 项目 | 现状 | 结论 |
|------|------|------|
| 架构边界 | 主要描述 UI dropdown 和 preset 表 | 方向正确，但缺少配置流、运行时生效路径和错误恢复策略 |
| 接口规范 | 示例接口只覆盖 host/model | 需要明确 host/path/port/model/key/timeout/buffer 的完整结构 |
| 兼容性 | 默认假设所有后端完全兼容 OpenAI | 需要说明 tool calling、字段差异、HTTP 错误、JSON 解析失败的处理 |
| 性能 | 只提到 `.rodata` 开销 | 需要补充 request/response buffer、schema cache、summary_only skill、超时策略 |
| 安全 | 只覆盖 API key 不打印 | 不足，需要覆盖命令黑白名单、文件访问、tool policy 和日志脱敏 |

因此，推荐架构是“cAGENT core 保持 vendor-neutral，smart_home 应用层负责配置、预设、
安全策略和 UI 管理”。这样既不污染 cAGENT 通用接口，也能满足 demo 的产品化体验。

## 总体架构

多后端适配应分为四层：

```text
Settings UI / config.json
    |
    v
smart_home model config manager
    |
    v
cAGENT OpenAI-compatible model provider
    |
    v
runtime HTTP/TLS adapter
```

各层职责如下：

| 层级 | 责任 | 不应承担 |
|------|------|----------|
| Settings UI | 编辑和展示后端配置，隐藏 API key，触发 Apply | 不直接操作 provider 内部结构 |
| config manager | 校验配置、展开 preset、保存应用态配置、调用 cAGENT API | 不解析 LLM 响应 |
| cAGENT provider | 序列化 messages/tools，发起 HTTP 请求，解析 tool_calls/content | 不内置具体厂商逻辑 |
| runtime adapter | 提供 TLS、HTTP、时间、日志、内存能力 | 不理解 Agent 语义 |

推荐数据流：

```text
用户选择后端
  -> UI 填充 preset
  -> 用户输入 API key / model / timeout
  -> Apply
  -> smart_home_agent_app_apply_model_config()
  -> agent_model_openai_set_backend()
  -> agent_model_openai_set_api_key()
  -> agent_model_openai_set_model()
  -> agent_set_limits()
  -> 下一次 agent_run 生效
```

## 多 LLM 后端完整技术方案

### 后端预设

smart_home 应维护一张只读预设表。预设表放在应用层，例如
`src/config/smart_home_backends.c`，不要放进 cAGENT core。

```c
typedef struct {
    const char *id;
    const char *name;
    const char *host;
    const char *path;
    const char *port;
    const char *default_model;
    uint32_t default_timeout_ms;
    uint32_t flags;
} smart_home_llm_backend_preset_t;
```

推荐预设：

| id | name | host | path | 默认 model | 说明 |
|----|------|------|------|------------|------|
| `deepseek` | DeepSeek | `api.deepseek.com` | `/v1/chat/completions` | `deepseek-v4-flash` | 默认后端，中文和工具调用表现稳定 |
| `mimo` | MiMo | `api.xiaomimimo.com` | `/v1/chat/completions` | `mimo-v2.5` | 国产后端，适合中文对话对比 |
| `qwen` | Qwen | `dashscope.aliyuncs.com` | `/compatible-mode/v1/chat/completions` | `qwen-turbo` | 阿里云 compatible mode |
| `openai` | OpenAI | `api.openai.com` | `/v1/chat/completions` | `gpt-4o-mini` | 原生 OpenAI API |
| `custom` | Custom | 用户填写 | 用户填写 | 用户填写 | 任意 OpenAI-compatible 服务 |

预设切换规则：

- 选中具名 preset 时，自动填充 host、path、port、model、timeout。
- 选中 preset 时不覆盖 API key。
- 选中 custom 时，保留用户当前输入；如果当前为空，提示用户手动填写。
- Apply 成功后下一次 `agent_run()` 生效，不强制清空 session。
- 当后端 tool calling 兼容性差异较大时，UI 应提示“建议新建会话后继续测试”。

### 运行时配置结构

原 `smart_home_model_config_t` 只覆盖 host、api_key、model、timeout。为了完整支持
多后端，应扩展为：

```c
typedef struct {
    char backend_id[32];
    char host[128];
    char path[96];
    char port[8];
    char api_key[256];
    char model[96];
    int timeout_ms;
    uint32_t request_buffer_size;
    uint32_t response_buffer_size;
    uint32_t max_output_tokens;
} smart_home_model_config_t;
```

字段约束：

| 字段 | 约束 |
|------|------|
| `backend_id` | 用于 UI 反选 preset；custom 时为 `custom` |
| `host` | 只能是域名或 IP，不允许包含 `/`、空格、控制字符 |
| `path` | 必须以 `/` 开头，禁止包含 CR/LF |
| `port` | 默认 `443`，只允许数字 |
| `api_key` | 可为空；为空时不发送 Authorization header |
| `model` | 必填，长度受结构体限制 |
| `timeout_ms` | 建议 5000 到 60000 |
| `request_buffer_size` | 不小于 cAGENT 编译期默认值 |
| `response_buffer_size` | 不小于 cAGENT 编译期默认值 |
| `max_output_tokens` | 映射到 provider request，避免回复过长 |

### 接口规范

应用层建议提供以下接口：

```c
const smart_home_llm_backend_preset_t *
smart_home_llm_backend_get(size_t index);

int smart_home_llm_backend_count(void);

int smart_home_model_config_from_preset(
    smart_home_model_config_t *config,
    const smart_home_llm_backend_preset_t *preset,
    bool keep_api_key);

int smart_home_model_config_validate(
    const smart_home_model_config_t *config,
    char *error,
    size_t error_size);

int smart_home_agent_app_apply_model_config(
    smart_home_agent_app_t *app,
    const smart_home_model_config_t *config);
```

`smart_home_agent_app_apply_model_config()` 应只通过 cAGENT 公共 API 修改运行时状态：

```c
agent_model_openai_set_backend(model,
                               config->host,
                               config->path,
                               config->port);
agent_model_openai_set_api_key(model, config->api_key);
agent_model_openai_set_model(model, config->model);
agent_set_limits(agent, &limits);
```

不要让 UI 或配置模块直接访问 `agent_t` 内部字段。

### UI 行为

Settings 页建议拆成三组：

| 区域 | 内容 |
|------|------|
| Backend | preset dropdown、host、path、port |
| Model | model、timeout、max output tokens、request/response buffer 显示 |
| Secret | API key 输入、遮盖显示、Apply、状态提示 |

UI 规则：

- API key 默认遮盖，只显示最后 4 位。
- 日志、trace card、status bar 不显示 API key。
- Apply 前做本地校验，失败时不调用 cAGENT。
- Apply 中禁止重复点击，避免边改配置边发起请求。
- Apply 成功后显示当前后端和 model，但不显示 key。

## 兼容性处理

### OpenAI-compatible 差异

不同服务商即使都声称兼容 OpenAI Chat Completions，也可能存在差异：

| 差异 | 处理策略 |
|------|----------|
| path 不同 | preset 明确配置 path，不在代码中硬编码 `/v1/chat/completions` |
| tool calling 字段差异 | cAGENT 统一解析 `choices[].message.tool_calls[]`；不兼容时返回 parse/model error |
| JSON arguments 被模型输出为非标准 JSON | 工具 handler 必须严格校验参数，失败返回结构化 tool error |
| content 与 tool_calls 同时存在 | ReAct loop 优先执行 tool_calls，最终回复由下一轮模型生成 |
| HTTP error body 格式不同 | provider 只提取有限错误摘要，不打印完整敏感 body |
| 模型不支持 tools | UI 或 preset flag 标注“chat only”，并在 tool demo 场景提示不推荐 |

建议在 preset 中加入 flags：

```c
#define SMART_HOME_LLM_FLAG_TOOL_CALLING  (1u << 0)
#define SMART_HOME_LLM_FLAG_CHAT_ONLY     (1u << 1)
#define SMART_HOME_LLM_FLAG_CUSTOM        (1u << 2)
```

Chat 页需要工具调用时，应优先选择带 `SMART_HOME_LLM_FLAG_TOOL_CALLING` 的后端。

### 会话兼容

切换后端时不强制清空 session，原因是开发者可能希望对比连续上下文。但存在风险：

- 不同模型对历史 tool_calls 的容忍度不同。
- 有些后端对长上下文或 tool result 更敏感。
- 自定义后端可能不支持多轮 tool message。

因此 UI 应提供“Clear session”按钮。切换到 custom 或 chat-only 后端时，建议提示用户清空会话。

### 错误分类

推荐在 UI 中将错误分为：

| 错误 | 常见原因 | 用户提示 |
|------|----------|----------|
| `AGENT_ERROR_INVALID` | host/path/model 配置不合法 | 检查模型配置 |
| `AGENT_ERROR_CONTEXT_OVERFLOW` | context/messages/tools schema 超出 buffer | 清空会话或调大 buffer |
| `AGENT_ERROR_MODEL` | 后端 HTTP 非 2xx 或模型返回错误 | 检查 API key、model、服务商状态 |
| `AGENT_ERROR_NETWORK` | DNS/TLS/网络失败 | 检查网络和 host |
| `AGENT_ERROR_PARSE` | 响应 JSON 不兼容 | 更换模型或关闭 tool 场景 |
| `AGENT_ERROR_LIMIT` | tool 参数/结果/轮数超限 | 缩短输入或减少工具调用 |

## 性能优化策略

### Buffer 策略

smart_home 属于 tool calling 较重的 demo，不应使用过小的默认 buffer。建议：

| buffer | 建议值 | 说明 |
|--------|--------|------|
| system context | 4096 到 8192 | system prompt + skills summary + device context |
| tool schema | 8192 到 16384 | 多工具场景容易膨胀 |
| messages | 8192 到 16384 | 多轮聊天和 tool result 历史 |
| HTTP request | 12288 起 | OpenAI-compatible 请求体 |
| HTTP response | 8192 起 | tool_calls + final answer |
| tool output | 1024 到 2048 | 单个 tool result 不应无限扩张 |

上层 demo 可以在创建 OpenAI provider 时覆盖 request/response buffer：

```c
model_config.request_buffer_size =
    max_u32(CAGENT_HTTP_REQUEST_BUFFER_SIZE, 12288u);
model_config.response_buffer_size =
    max_u32(CAGENT_HTTP_RESPONSE_BUFFER_SIZE, 8192u);
```

编译期 Kconfig 仍负责 agent 内部固定数组上限；运行时 model config 负责 provider HTTP
buffer。两者不要混为一个配置项。

### Prompt 与 Skill 压缩

smart_home 已经引入 Markdown 文件式 Skill。多后端方案下应继续使用：

- `summary_only` skill 默认只注入摘要。
- 详细规则通过 `read_skill` 工具按需读取。
- system prompt 保持短句，不把每个设备和场景规则硬塞进去。
- tool description 尽量短，参数约束放入 JSON schema。

### Tools Schema Cache

工具集合稳定时，不应每轮重复构造完整 tools schema。cAGENT 当前已有 schema dirty 机制：

- register/unregister tool 时标记 dirty。
- enable/disable tool 时标记 dirty。
- 未变化时复用 schema cache。

smart_home 的 tool 注册应集中在启动期，运行中尽量只启停工具，不频繁重建工具表。

### 超时和输出控制

推荐默认值：

| 配置 | 建议 |
|------|------|
| `timeout_ms` | 30000 |
| `per_model_timeout_ms` | 等于后端 timeout |
| `per_tool_timeout_ms` | 3000 到 5000 |
| `max_steps` | 6 到 8 |
| `max_tool_calls` | 6 到 8 |
| `max_output_tokens` | 512 到 1024 |

对于慢后端，只提高 model timeout，不要无上限提高 max_steps 或 max_tool_calls。

## 安全机制

### 安全原则

smart_home 是嵌入式 Agent demo，安全边界应遵循：

- LLM 只能通过注册 tool 影响外部世界。
- tool 必须是最小能力接口，不暴露通用 shell 或任意文件读写。
- 所有 tool 参数必须本地校验，不能相信模型输出。
- 所有高风险动作必须经过 policy 或 UI 确认。
- 日志和 trace 不能泄露 API key、token、完整 HTTP header。

### 命令黑白名单策略

当前 smart_home 不应提供通用 `exec_shell`、`system`、`popen` 类 LLM-visible 工具。
如果后续为了调试或 MCP 扩展引入命令工具，必须采用“默认拒绝 + 白名单允许”的策略。

命令策略建议：

| 类别 | 策略 |
|------|------|
| 默认 | deny |
| 允许命令 | 只允许固定命令 ID，不允许模型传任意 shell 字符串 |
| 参数 | 每个命令定义参数 schema 和取值范围 |
| 危险字符 | 禁止 `;`、`&&`、`||`、反引号、`$()`、重定向、管道 |
| 超时 | 每条命令必须有短 timeout |
| 输出 | 限制输出长度，超出截断并标记 truncated |
| 日志 | 记录命令 ID、状态码、耗时，不记录敏感参数 |

推荐接口不是：

```json
{"cmd":"rm -rf /data"}
```

而是：

```json
{"command":"list_res_icons","args":{"dir":"icons"}}
```

白名单定义示例：

```c
typedef struct {
    const char *id;
    const char *argv[6];
    uint32_t timeout_ms;
    uint32_t max_output_bytes;
    bool llm_visible;
} smart_home_command_rule_t;
```

黑名单只作为第二道防线，用来拦截明显危险输入。不能依赖黑名单作为主要安全机制。

### 文件访问权限控制

smart_home 中存在资源文件、配置文件和 Markdown skill 文件。文件访问必须分目录授权。

推荐文件域：

| 域 | 路径 | 权限 | 用途 |
|----|------|------|------|
| resources | `/data/res` | read-only | PNG、字体、默认配置 |
| skills | `/data/res/skills` | read-only | Markdown skill |
| config | `/data/res/config.json` 或应用私有路径 | read/write by UI only | 模型配置 |
| temp | `/tmp/smart_home` | read/write limited | 临时文件 |

文件访问规则：

- LLM-visible tool 默认不能读取任意路径。
- `read_skill` 只能通过 skill id 读取 registry 中已注册的 skill，不能接受任意 path。
- 禁止 `../` 路径穿越。
- 禁止绝对路径透传，除非调用方是非 LLM 的系统模块。
- 限制单文件读取大小，例如 8KB 或 16KB。
- 对配置文件写入采用临时文件 + rename，避免掉电损坏。
- API key 若需要持久化，应使用平台安全存储；demo 中至少禁止日志打印。

路径校验示例：

```c
static bool path_is_under_root(const char *root, const char *path)
{
    return root && path &&
           strncmp(path, root, strlen(root)) == 0 &&
           strstr(path, "/../") == NULL &&
           strstr(path, "../") == NULL;
}
```

更推荐的方式是“不让模型传 path”，而是传受控 id：

```json
{"skill_id":"smart_home_device_control"}
```

### Tool 调用安全管控

cAGENT 已经具备基础 guard：

- 工具必须在 registry 中存在。
- disabled tool 不参与 schema 和执行。
- tool arguments 超过 `CAGENT_TOOL_ARGS_MAX_SIZE` 会拒绝。
- 单次请求 tool 调用次数超过 `max_tool_calls` 会拒绝。
- tool timeout 会返回结构化错误。
- tool output 超过 `CAGENT_TOOL_OUTPUT_MAX_SIZE` 会拒绝。
- 应用可通过 `agent_set_policy_callback()` 实现产品策略。

smart_home 应在这些基础上补应用侧 policy：

| 工具 | 建议策略 |
|------|----------|
| `get_home_status` | read-only，允许 |
| `set_light` | side-effect，允许但校验 room、brightness |
| `set_ac` | side-effect，允许但校验 mode、fan_speed、temperature |
| `set_timer` | side-effect，限制数量和时间范围 |
| `get_weather` | read-only，允许；明确是 demo 模拟天气 |
| `read_skill` | read-only，只允许读取已注册 skill id |
| 设备增删改 | 不注册为 LLM-visible tool，归 Panel UI 管理 |
| 环境传感器修改 | 不注册为 LLM-visible tool，归 Panel UI 管理 |

policy 回调示例：

```c
static agent_policy_decision_t smart_home_policy_cb(
    const agent_policy_request_t *request,
    void *user_data)
{
    const agent_tool_call_t *call;

    if (!request || request->action != AGENT_POLICY_ACTION_TOOL_CALL) {
        return AGENT_POLICY_DENY;
    }

    call = request->tool_call;
    if (!call || !call->name) {
        return AGENT_POLICY_DENY;
    }

    if (strcmp(call->name, "get_home_status") == 0 ||
        strcmp(call->name, "get_weather") == 0 ||
        strcmp(call->name, "read_skill") == 0) {
        return AGENT_POLICY_ALLOW;
    }

    if (strcmp(call->name, "set_light") == 0 ||
        strcmp(call->name, "set_ac") == 0 ||
        strcmp(call->name, "set_timer") == 0) {
        return AGENT_POLICY_ALLOW;
    }

    return AGENT_POLICY_DENY;
}
```

更细粒度的参数校验不应放在 policy 里做字符串猜测，而应放在各 tool handler 中用 JSON
解析后校验：

- room 必须属于已存在房间。
- device 必须存在且类型匹配。
- brightness 范围 0 到 100。
- AC temperature 只在 cool/heat/auto/dry 下生效，范围 16 到 30。
- fan_speed 只能是 auto/low/medium/high。
- timer 数量和延迟时间必须有上限。

### Tool 结果安全

tool result 是下一轮模型输入的一部分，必须控制大小和内容：

- 成功结果返回短 JSON，例如 `{"ok":true,"room":"living_room","on":true}`。
- 错误结果返回结构化错误，不返回内部堆栈。
- 不把 API key、完整文件内容、HTTP header 放进 tool result。
- 长文本结果必须截断并标记，例如 `"truncated":true`。
- tool handler 返回的 JSON 必须是 UTF-8。

### 日志与 trace 脱敏

日志允许记录：

- backend id、host、model。
- tool name、call id、status、耗时。
- error code、短错误摘要。

日志禁止记录：

- API key、Authorization header。
- 完整 HTTP request body 中的敏感内容。
- 用户输入中的明显 token 或 key。
- 任意文件完整内容。

推荐统一提供脱敏函数：

```c
void smart_home_secret_mask(const char *src, char *dst, size_t dst_size);
```

显示规则：长度小于等于 8 时全部显示 `****`，长度大于 8 时显示前 2 位和后 4 位。

## 配置持久化

demo 阶段可以只在运行期保存配置；如果需要重启后恢复，应将非敏感配置和敏感配置分开：

| 数据 | 存储方式 |
|------|----------|
| backend_id、host、path、port、model、timeout | `config.json` |
| API key | 平台安全存储；没有安全存储时不持久化 |
| session history | 默认不持久化 |
| UI 当前 tab | 可不持久化 |

`config.json` 示例：

```json
{
  "model": {
    "backend_id": "deepseek",
    "host": "api.deepseek.com",
    "path": "/v1/chat/completions",
    "port": "443",
    "model": "deepseek-v4-flash",
    "timeout_ms": 30000,
    "max_output_tokens": 512
  }
}
```

## 实施步骤

### 阶段一：后端预设与 Apply 链路

- 新增 `smart_home_backends.h/.c`。
- 扩展 `smart_home_model_config_t`，加入 path、port、backend_id。
- 修改 `smart_home_agent_app_apply_model_config()`，不再硬编码 path。
- Settings 页增加 backend dropdown。
- Apply 前校验 host/path/port/model。

### 阶段二：安全策略

- 注册 `agent_set_policy_callback()`。
- 为每个 smart_home tool 补齐 flags：read-only、side-effect、llm-visible。
- `read_skill` 只接受 skill id，不接受 path。
- 明确 Panel UI 的设备管理和环境模拟不暴露给 LLM tool。
- 增加日志脱敏函数。

### 阶段三：兼容与性能优化

- 增加 preset flags，标注 tool calling 能力。
- 设置页显示 request/response buffer 当前值。
- 对 `AGENT_ERROR_CONTEXT_OVERFLOW` 给出清空会话或调大 buffer 的提示。
- 使用 summary_only skill，避免系统上下文膨胀。
- 按[多 LLM 后端工具调用对比测试方案](../../../docs/plans/llm-backend-toolcall-comparison.md)
  建立冻结用例、状态夹具、脱敏 JSONL trace 与批量汇总；现有 Console/LVGL 事件链只
  记录工具名和结果码，不能单独用作参数和副作用的自动判定证据。

## 验收标准

基础功能：

- Settings 页可以选择 DeepSeek、MiMo、Qwen、OpenAI、Custom。
- 切换 preset 后 host/path/port/model 自动填充。
- preset 切换不会覆盖 API key。
- Custom 可以手动填写 host/path/port/model。
- Apply 后下一次 chat 使用新后端。

兼容性：

- DeepSeek 下可完成 `get_home_status`、`set_light`、`set_ac` 工具调用。
- 至少另一个后端在相同工具 schema、Skill、prompt 和初始设备状态下完成冻结的工具
  调用基准；对比任务成功率、参数正确率、稳定性、首工具时间和端到端时间。
- 对不支持 tool calling 的后端，UI 有明确提示或工具场景返回可理解错误。
- HTTP/network/parse/context overflow 错误在 UI 中有分类提示。

安全：

- 日志和 trace 中不出现 API key。
- 没有 LLM-visible 任意命令执行工具。
- 没有 LLM-visible 任意文件读取工具。
- `read_skill` 只能读取注册 skill。
- 设备增删改和环境传感器修改只能通过 Panel UI 完成。
- tool 参数非法时返回结构化错误，不修改设备状态。

性能：

- 多轮 chat 不因一两个 tool result 迅速触发 `AGENT_ERROR_CONTEXT_OVERFLOW`。
- request/response buffer 大小可被 demo 上层配置。
- 长 tool output 被限制或截断。
- Apply 和 agent_run 不会并发修改同一个 model 配置。

## 风险与取舍

- 不建议在 cAGENT core 中加入服务商枚举。服务商变化快，放在 demo 应用层更灵活。
- 不建议一开始实现自动 failover。不同模型的上下文、tool calling 能力和价格差异很大，
  自动切换会让调试复杂度显著上升。
- 不建议把 API key 持久化到普通 JSON 文件。demo 可接受运行时输入，产品化应接入安全存储。
- 不建议让 LLM 管理设备列表或环境传感器值。设备管理和环境模拟属于 UI/系统输入，LLM
  只通过受控 tool 查询和控制设备。

这个方案的关键点是：多 LLM 后端是配置能力，安全边界是产品能力。前者让开发者更容易
接不同模型，后者保证模型再聪明也只能在 smart_home 明确定义的能力范围内行动。
