# smart_home 持久化配置草案

本目录只保存供审查和测试的 version 1 JSON 模板，不保存真实凭据。真机/模拟器实际
部署输入位于 `../res/config/`：BOX-3 LittleFS 脚本从该目录打包，运行时统一落到
`/data/smart_home/`。实现加载与保存后，模板对应的设备端运行时路径为：

| 模板 | 运行时文件 | 内容 |
|---|---|---|
| `backends.example.json` | `/data/smart_home/backends.json` | LLM 后端预设库（可写回，新增后端不需重编译） |
| `settings.example.json` | `/data/smart_home/settings.json` | 当前选中的后端 ID（`active_backend_id`） |
| `state.example.json` | `/data/smart_home/state.json` | 本地设备和模拟环境状态 |
| `policy.example.json` | `/data/smart_home/policy.json` | 本地工具开关和远程来源默认策略 |
| `timers.example.json` | `/data/smart_home/timers.json` | 需要跨重启恢复的定时器 |
| `secrets.example.json` | `/data/smart_home/secrets.json` | 敏感配置的专用加载路径；仅用于无安全存储平台的明文后备方案 |

`/data/wapi.conf` 继续由 WAPI 独立管理，不复制到这些文件中。Skill、字体和图标仍位于
`/data/res`，该目录不承载应用运行时写入。

## backends.json 语义

后端预设库与"当前选择"分离：`backends.json` 管理多份预设（新增后端 = 编辑 JSON +
刷新，不重编译），`settings.json` 只存当前选中的 `active_backend_id`。编译期保留
当前内置预设表（DeepSeek、MiMo、Qwen、OpenAI、Custom）是 JSON 缺失、损坏或清空时的
唯一业务默认来源；实现时应由该表构造内存默认值，不能再维护一份容易漂移的静态 JSON。

预设字段与 `smart_home_llm_backend_preset_t` 一一对应：
`backend_id`（唯一）、`label`、`host`、`path`、`port`、`default_model`、
`default_timeout_ms`。`backend_id` 必须匹配 `^[a-z0-9_-]+$` 且长度 ≤ 32；
`backends` 数组上限为 `CONFIG_SMART_HOME_MAX_BACKENDS`（默认 8）。

API key 不写入本文件，位于 `secrets.json`（敏感隔离）。

## 通用规则

- 所有文件必须包含整数 `version`；当前只接受 `1`，不静默兼容未知版本。
- 文件大小、数组数量、字符串长度和枚举值必须有固定上限。
- 保存使用同目录、带唯一 generation 的临时文件，完整写入并 `fsync` 后原子 `rename`，
  不直接覆盖现有文件。
- 配置损坏时始终保留原文件。backends/settings/state/timers 可回退内置非敏感默认值；
  policy 必须 fail closed，远程副作用工具默认禁用。
- 模板中的远程来源默认值同样为禁用；部署者经审查后才可在 `policy.json` 中显式启用
  Node 或 MCP 工具。
- JSON 中出现未知字段时记录告警，未知的安全相关字段应拒绝。
- `secrets.json`、API key、共享 token 和 Authorization header 不得写入日志。

## state.json 语义

当前 `state.json` 只描述 smart_home 本地模拟设备。接入 Node/MCP 后，远程设备的在线状态、
`connection_gen`、动态工具 descriptor 和实时 reported state 不写入本文件，而是在 provider
重连后重新发现。若 UI 需要展示离线缓存，必须同时保存采样时间并明确标记为 stale。

设备 `id` 必须为正数且唯一；加载后 `next_device_id` 由最大 id 加一推导，不作为独立字段
持久化。灯光亮度范围为 0 到 100；空调温度范围为 16 到 30；mode 仅接受
`cool/heat/dry/fan/auto`，fan_speed 仅接受 `low/medium/high/auto`。

## timers.json 记录格式

空模板不会创建演示定时器。实际记录采用绝对时间，避免重启后重新从完整 delay 开始：

```json
{
  "id": 1,
  "name": "sleep_reminder",
  "message": "Time to sleep",
  "created_epoch": 1785945600,
  "due_epoch": 1785945900
}
```

加载时需要定义过期策略：建议已经过期的定时器标记为 missed 并交给 UI 展示，不在启动
瞬间自动执行设备控制。

## sessions.json 状态

当前没有 `sessions.example.json`。它依赖 cAGENT 的 session export/import API，待
`docs/design/session-persistence-plan.md` 的内核 P1 完成后，才作为第六类非敏感
配置接入统一 store；在此之前，设备重启会创建新会话。

## secrets.json 限制

`secrets.example.json` 中的敏感值必须保持为空。模型密钥按 `backend_id` 放入
`model_api_keys` 映射；Node/MCP 凭据位于各自命名空间，避免把所有 token 混为一个字段。
普通 `smart_home_config_store` 不加载、回退或写回此文件；缺少密钥只能禁用关联
backend/provider，绝不能回退到源码或模板中的默认值。

启用 `SMART_HOME_MCP_BRIDGE` 后，`mcp_bridge` 同时承载受保护的 Authorization 值和
非敏感 endpoint/allowlist。`authorization` 是完整的 HTTP Authorization **值**（例如
`Bearer <limited-token>`），为空表示不发送该 header；它不得含换行。`tools` 是精确
allowlist，最多 4 项；每项必须给出稳定 `name`、风险 `read_only` / `side_effect` /
`dangerous` 与 100–60000 ms 的 `timeout_ms`。MVP 演示只应配置 `read_only` 工具。

MCP bridge 配置拆为两个文件：**非敏感 server 配置**（`/data/smart_home/mcp_bridge.json`，
可进 git）与**敏感 header 值**（`/data/smart_home/secrets.json`，不提交）。
`headers` 中的值若以 `${VAR}` 形式引用，则从 secrets 的
`mcp_bridge.header_values.<VAR>` 解析真实值（如 `${CAIYUN_KEY}`）。

`scheme` 为 `http`（明文，仅限 QEMU/隔离 LAN）或 `https`（TLS，经 cAGENT runtime
的 mbedTLS 复用；当前为 VERIFY_OPTIONAL 演示级证书校验）。`mcp_bridge.json` 示例：

```json
{
  "version": 1,
  "mcp_bridge": {
    "scheme": "https",
    "host": "mcp-weather.caiyunapp.com",
    "port": 443,
    "path": "/mcp",
    "server_id": "caiyun",
    "authorization": "",
    "headers": { "X-Caiyun-API-Key": "${CAIYUN_KEY}" },
    "tools": [
      {"name": "get_realtime_weather", "risk": "read_only", "timeout_ms": 10000}
    ]
  }
}
```

对应 secrets.json：

```json
{
  "version": 1,
  "mcp_bridge": { "header_values": { "CAIYUN_KEY": "replace-with-caiyun-key" } }
}
```

真实设备应优先使用芯片安全存储；只有比赛演示或 Goldfish 环境允许部署
`res/config/secrets.json`。设备端 `/data` 文件和 Goldfish data 镜像仍然是明文，不能视为
安全介质。

模型密钥由专用只读 loader 消费：每次启动或选择内置 backend 时，按
`model_api_keys.<backend_id>` 重新读取。文件缺失、JSON 损坏、版本错误、对应条目缺失或
为空都会仅禁用该模型，不会回退到源码 key，也不会复用上一个模型的 key。设置页只显示
“已配置/未配置”，不显示或编辑内置后端的明文 key。
