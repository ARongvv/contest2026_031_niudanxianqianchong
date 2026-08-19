# smart_home 标准 MCP 直连联调

> 状态：2026-08-06，MCP 发现改为 Settings 页手动触发。启动只装配 bridge，网络
> 健康不会阻塞 UI 首屏；支持 `https`（复用 cAGENT openvela runtime 的 mbedTLS）与
> SSE 响应解析。
>
> MVP 实现 `2025-03-26` MCP Streamable HTTP 的 tools-only Profile。设备直接发送
> `initialize`、`notifications/initialized`、`tools/list` 与 `tools/call`；不是私有
> `/v1/*` 协议，也不需要 sidecar。

## 1. 启用与配置

Goldfish smart_home defconfig 需要开启（`SMART_HOME_MCP_BRIDGE` 会 select
`CAGENT_ADDONS_MCP`）：

```text
CONFIG_SMART_HOME_MCP_BRIDGE=y
CONFIG_CAGENT_MAX_TOOLS=24
```

配置拆为两个运行时文件（模板见 `config/mcp_bridge.example.json` 与
`config/secrets.example.json`）：

- `/data/smart_home/mcp_bridge.json`（非敏感）：endpoint、scheme、allowlist、
  自定义 header 结构。header 值以 `${VAR}` 形式引用，不落真实凭据；
- `/data/smart_home/secrets.json`（敏感）：`mcp_bridge.header_values` 保存
  `${VAR}` 对应的真实值（如 `CAIYUN_KEY`）。

彩云天气 MCP 的实例如下（QEMU 与真机同构，https 直连）：

```json
// mcp_bridge.json
{
  "version": 1,
  "mcp_bridge": {
    "scheme": "https",
    "host": "mcp-weather.caiyunapp.com",
    "port": 443,
    "path": "/mcp",
    "server_id": "caiyun",
    "headers": { "X-Caiyun-API-Key": "${CAIYUN_KEY}" },
    "tools": [
      { "name": "get_realtime_weather", "risk": "read_only", "timeout_ms": 10000 },
      { "name": "get_weekly_forecast",  "risk": "read_only", "timeout_ms": 10000 }
    ]
  }
}
```

```json
// secrets.json（片段）
{ "version": 1, "mcp_bridge": { "header_values": { "CAIYUN_KEY": "<真实 key>" } } }
```

allowlist 是设备 policy：Server 即使发布其他工具也不会注册。`authorization`
与 `headers` 的值禁止含换行；真实 key 只出现在 secrets.json，不写日志。

## 2. 启动与手动发现

`smart_home_agent_app_init()` 只校验配置并创建 MCP bridge 和 operation worker，
随即返回；不会在启动阶段发起 DNS、TLS 或 HTTP。Settings 的 **Discover MCP** 按钮
才会唤醒 operation worker，在其独立大栈中同步执行 `initialize`、
`notifications/initialized` 和 `tools/list`。因此网络操作不会运行在 LVGL 线程。

栈大小由 `SMART_HOME_MCP_BRIDGE_DISCOVERY_STACKSIZE` 配置，默认 16 KB；Goldfish
排障可暂设为 64 KB。该名称为兼容已有配置保留，实际含义是手动 MCP operation worker。

状态机（Settings 页每秒刷新，可反复触发）：

| 状态 | 含义 |
| --- | --- |
| `Manual` | bridge 已装配，尚未请求网络发现 |
| `Connecting...` | operation worker 正在执行发现 |
| `Connected` | 发现成功，allowlist 内工具已注册（`mcp_<server>_<tool>`） |
| `Failed (<err>)` | 发现失败；本地与 Node 功能不受影响 |

日志序列：`MCP bridge ready; discovery is manual` → 点击 **Discover MCP** →
`MCP discovery requested` → `MCP discovery succeeded` →
`MCP tool registered: mcp_caiyun_get_realtime_weather`。

验收时先确认首屏立刻显示、MCP 状态为 `Manual`；随后点击按钮。即使 DNS、TLS 或
`tools/list` 失败，状态也只会收敛到 `Failed (<err>)`，不应影响本地工具、Node gateway
或 LVGL 响应。再次点击会清除上次 session 和远程目录，并执行一次完整重新协商。

Console UI 使用同一份 bridge，但没有 Settings 页面。启动交互式 `smart_home` 后，使用
以下本地命令触发和观察发现，不会把命令发送给模型：

```text
home> mcp status
MCP: Manual (last_error=0)
home> mcp discover
MCP discovery requested. Use 'mcp status' to check progress.
home> mcp status
MCP: Connected (last_error=0)
```

`mcp discover` 只将工作投递到 MCP operation worker 并立即返回；连接、初始化和
`tools/list` 仍在该 worker 的独立栈中完成。状态为 `Connected` 后，再输入天气等请求，
模型才可以调用已注册的 `mcp_<server>_<tool>`。

## 3. QEMU fake server

### 3.1 独立协议探针

如果需要排除 LVGL、Node gateway 或模型线程，使用独立的 `mcp_probe` 应用。它与
smart_home 共用同一份 `smart_home_mcp_bridge.c`，但只执行 MCP 发现：

```bash
ln -sfnT ../../openvela_smarthome/demos/mcp_probe packages/demos/mcp_probe
```

Goldfish 启动后在 NSH 执行：

```sh
mcp_probe
```

`PASS` 表示 DNS/TLS（或 plain HTTP）、MCP initialize/tools/list 以及 catalog mutation
均完成；`FAIL` 后的 `state`、`error` 和 `request` 用于定位失败阶段。该应用不依赖
LVGL 首屏，因此可以先验证设备到 MCP Server 的直连链路，再回到 Settings 页验证
smart_home 集成。

在宿主机启动仓库自带、符合相同 Profile 的 fake server：

```bash
cd /home/arongw/openvela/openvela_smarthome
python3 tests/integration/mock_mcp_server.py --port 18800
```

此时 `mcp_bridge.json` 使用 `"scheme": "http"`、`"host": "10.0.2.2"`、
`"port": 18800`。启动 smart_home 后，进入 Settings，展开 System Status 并点击
**Discover MCP**；日志应依次出现发现成功与 `mcp_weather_get_weather` 注册。使用模型对话询问天气，可验证 tool call；
fake server 返回确定性文本，便于录制演示证据。

## 4. 边界与故障预期

- 缺少、损坏或空 allowlist 的配置会禁用 MCP bridge（启动配置校验失败）；网络
  发现失败只更新 MCP 状态，本地和 Node 功能始终可用。
- HTTP 401/403、协议版本错误、超长 header/body 均结构化失败，不注册未验证工具。
- `http`（plain HTTP/1.1，关闭连接）仅限 QEMU/隔离 LAN；`https` 复用 cAGENT
  openvela runtime 的 mbedTLS，并在非阻塞 TLS 握手中遵守请求 deadline。当前 runtime
  使用 `MBEDTLS_SSL_VERIFY_OPTIONAL`，因此它提供传输加密但尚不是可用于公网的服务器
  身份校验方案；产品化前必须配置 CA 链并改为强制验证。
- SSE（`text/event-stream`）响应按 `data:` 帧解析；chunked 无限流仍拒绝。
- `Mcp-Session-Id` 会在后续请求携带；Server 返回 404 时 client 重新 initialize 一次。
- 真实第三方 server 的 `tools/list` 响应可能较大，必要时上调
  `CAGENT_ADDONS_MCP_RESPONSE_BUF_SIZE`（默认 4096，上限 16384）。

完整的协议、内存上限与安全边界见
[`docs/plans/mcp-extension-plan.md`](../../../docs/plans/mcp-extension-plan.md)。
