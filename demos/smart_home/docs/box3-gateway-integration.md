# ESP32-S3-BOX-3 MCP、Node 与手机 App 联调手册

本文用于真机上验证 Smart Home 的三条扩展链路：

- MCP：设备主动连接外部 MCP Server，发现并注册允许的工具。
- Node Gateway：远端 Node 通过 WebSocket 接入设备，动态发布工具。
- 手机 App Bridge：手机或 PC 通过 HTTP/WebSocket 读取状态、下发控制和订阅事件。

建议首先使用 Console UI 进行联调：它不启动 LVGL、FreeType 和 LCD 刷新任务，
可以避免把 UI 的资源与调度问题混入网络协议验证。

## 1. 前置检查

### 1.1 设备端网络与服务

在 NSH 中连接 WLAN 后启动应用：

```text
nsh> ifup wlan0
nsh> renew wlan0
nsh> ifconfig
nsh> smart_home
```

记录 `wlan0` 的 IPv4 地址。以下命令以 `192.168.1.103` 为例，请替换为设备实际地址。

设备应先输出以下网络诊断，且 IP、网关非空：

```text
[smart_home_net] init-begin if=wlan0 ip=192.168.1.103 gateway=192.168.1.1 ...
[smart_home_net] init-end if=wlan0 ip=192.168.1.103 gateway=192.168.1.1 ...
```

### 1.2 PC 终端公共变量

在与设备同一局域网的 PC 终端执行。App token 从设备
`/data/smart_home/secrets.json` 的 `app_bridge.shared_token` 获取，不要将 token
粘贴到 shell 历史、日志或 Git 文件中。

```bash
export IP=192.168.1.103
read -rsp 'App Bridge Token: ' APP_TOKEN; echo
```

可选地先确认连通性：

```bash
ping "$IP"
```

## 2. 手机 App Bridge

当前 Smart Home 配置使用 App Bridge 的 HTTP/WebSocket 服务：

```text
CONFIG_SMART_HOME_APP_BRIDGE=y
CONFIG_SMART_HOME_APP_BRIDGE_PORT=8080
CONFIG_SMART_HOME_APP_BRIDGE_WORKER=y
CONFIG_SMART_HOME_APP_BRIDGE_WORKER_ACCEPT=y
```

服务地址为 `http://<设备 IP>:8080`，所有接口均使用：

```text
Authorization: Bearer <app_bridge.shared_token>
```

### 2.1 HTTP 冒烟测试

读取全量状态：

```bash
curl -i -H "Authorization: Bearer $APP_TOKEN" \
  "http://$IP:8080/v1/home/snapshot"
```

预期：`HTTP/1.1 200 OK`，返回 `revision`、`devices` 与 `environment`。

读取设备、工具、技能及事件历史：

```bash
curl -i -H "Authorization: Bearer $APP_TOKEN" \
  "http://$IP:8080/v1/devices/1"

curl -i -H "Authorization: Bearer $APP_TOKEN" \
  "http://$IP:8080/v1/catalog/tools"

curl -i -H "Authorization: Bearer $APP_TOKEN" \
  "http://$IP:8080/v1/catalog/skills"

curl -i -H "Authorization: Bearer $APP_TOKEN" \
  "http://$IP:8080/v1/history"
```

控制客厅灯亮度：

```bash
curl -i -X POST "http://$IP:8080/v1/commands" \
  -H "Authorization: Bearer $APP_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"deviceId":"1","action":"set_brightness","value":60}'
```

预期：`HTTP/1.1 202 Accepted`，返回新的 `revision`。再次读取 snapshot 与
history，状态和 `device_state_changed` 事件应一致。

### 2.2 WebSocket 事件测试

安装 `websocat` 后，在终端 A 订阅事件：

```bash
websocat -H="Authorization: Bearer $APP_TOKEN" \
  "ws://$IP:8080/v1/events"
```

连接成功后应先收到全量快照。保持终端 A 不退出，在终端 B 再次发送一条控制命令：

```bash
curl -sS -X POST "http://$IP:8080/v1/commands" \
  -H "Authorization: Bearer $APP_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"deviceId":"2","action":"turn_on"}'
```

终端 A 应收到增量事件；真机串口应看到 `bridge-http-request`、
`bridge-ws-snapshot` 或 `bridge-event-*` 的内存区域诊断，当前大块协议缓冲应标记为
`PSRAM`。

### 2.3 Chat：提交与获得最终结果

Chat 是异步接口；需启用：

```text
CONFIG_SMART_HOME_APP_BRIDGE_CHAT=y
```

提交消息只表示设备已接受任务，不会在该 HTTP 连接中等待模型回复：

```bash
curl -i -X POST "http://$IP:8080/v1/conversations/default/messages" \
  -H "Authorization: Bearer $APP_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"message":"hi"}'
```

预期立即返回 `202 Accepted`：

```json
{"runId":"run-1","status":"queued"}
```

`runId` 是后续取回结果或匹配实时事件的关联标识。设备侧应依次看到
`RUN_START`、`MODEL_REQ`、`MODEL_RESP` 和 `RUN_DONE`；`RUN_DONE` 为 `err=0` 表示
模型调用完成。

#### 方式一：轮询 Run 结果

使用提交接口返回的 `runId` 轮询：

```bash
curl -i -H "Authorization: Bearer $APP_TOKEN" \
  "http://$IP:8080/v1/runs/run-1"
```

执行中会返回 `queued` 或 `running`，此时 `output` 为空；完成后预期为：

```json
{"runId":"run-1","status":"succeeded","output":"你好！有什么可以帮你的吗？"}
```

失败时 `status` 为 `failed`。`GET /v1/runs/<runId>/trace` 当前是预留接口，返回空
trace；Run 结果仅保留在 RAM 中，且当前实现只保存最近一条 Run，设备重启或下一条消息
提交后不能作为历史记录依赖。

#### 方式二：WebSocket 实时订阅（手机 App 推荐）

先在终端 A 建立订阅，再在终端 B 提交消息。连接成功后，服务端先推送一次全量设备
快照；随后推送设备事件和 Chat 的 Run 生命周期事件。

若主机安装了 `websocat`：

```bash
websocat -H="Authorization: Bearer $APP_TOKEN" \
  "ws://$IP:8080/v1/events"
```

若主机已有 Node.js，也可使用 `wscat`：

```bash
npm install -g wscat
wscat -c "ws://$IP:8080/v1/events" \
  -H "Authorization: Bearer $APP_TOKEN"
```

没有 `websocat` 时，可在主机 Python 虚拟环境安装并使用 `websocket-client`：

```bash
python -m pip install websocket-client

python - <<'PY'
import os
import websocket

ws = websocket.create_connection(
    f"ws://{os.environ['IP']}:8080/v1/events",
    header=[f"Authorization: Bearer {os.environ['APP_TOKEN']}"],
)
print("WebSocket connected; waiting for snapshot and events...")
while True:
    print(ws.recv())
PY
```

终端 B 提交消息后，终端 A 预期收到类似：

```json
{"type":"agent_run_started","runId":"run-1","data":{"status":"running"}}
{"type":"agent_message","runId":"run-1","data":{"content":"你好！有什么可以帮你的吗？"}}
{"type":"agent_done","runId":"run-1","data":{"status":"succeeded"}}
```

断线或 App 在后台时，应使用“方式一”轮询 `/v1/runs/<runId>` 兜底。当前 bridge 只维护
一个 WebSocket 订阅者，新订阅会替换旧订阅，且不重放连接前产生的 Run 事件。因此必须先
订阅再提交消息。

同时维持监听 socket、WebSocket、提交请求和设备到模型服务的 HTTPS 出站连接时，
`CONFIG_NET_TCP_ALLOC_CONNS=1` 不足；BOX-3 演示配置建议设置为至少 `4`，推荐 `6`。

## 3. MCP

MCP 是设备到 Server 的出向 Streamable HTTP 客户端，并不在设备上开放 MCP 监听端口。
配置来自：

```text
/data/smart_home/mcp_bridge.json
/data/smart_home/secrets.json
```

其中 `mcp_bridge.json` 定义 endpoint、allowlist 和 `${VAR}` 形式的 header 引用，
真实 header 值在 secrets 的 `mcp_bridge.header_values` 中。

### 3.1 独立协议探针

当前 defconfig 启用了 `CONFIG_SMART_HOME_MCP_PROBE=y`。在 NSH 中执行：

```text
nsh> mcp_probe
```

它会依次执行 MCP `initialize`、`notifications/initialized` 和 `tools/list`，并将
allowlist 中的工具注入一个最小 cAGENT 实例。预期末尾为：

```text
PASS: MCP discovery and catalog mutation completed
```

失败时保存完整串口输出。该探针不启动 LVGL、Node Gateway 或模型请求，因此可优先用
于定位 DNS、TLS、HTTP 与 MCP 协议问题。

### 3.2 Smart Home 集成验证

启动 LVGL 版 `smart_home` 后，在 Settings 页面点击 **Discover MCP**。串口预期按顺序
输出：

```text
smart_home: MCP discovery requested
smart_home: MCP discovery succeeded
smart_home: MCP tool registered: mcp_<server>_<tool>
```

Console UI 当前没有 MCP discover 命令；需要使用 `mcp_probe` 验证底层协议，或切换
到 LVGL Settings 触发 Smart Home 内的发现流程。

## 4. Node Gateway

Node Gateway 是设备的入向 WebSocket 服务，默认端口为 `18790`。它与 App Bridge
不是同一个协议，也不使用 App token。

### 4.1 当前固件状态

当前生成的配置中 Node Gateway 关闭：

```text
# CONFIG_SMART_HOME_NODE_GATEWAY is not set
# CONFIG_CAGENT_ADDONS_NODE_GATEWAY is not set
```

因此当前固件不会监听 `18790`，也不能用 Node client 完成联调。启用后需要重新生成配置
并编译，至少确认：

```text
CONFIG_SMART_HOME_NODE_GATEWAY=y
CONFIG_CAGENT_ADDONS_NODE_GATEWAY=y
CONFIG_SMART_HOME_NODE_GATEWAY_PORT=18790
```

`CONFIG_SMART_HOME_NODE_GATEWAY=y` 会选择对应的 cAGENT addon；无需单独手工设置第二项。

### 4.2 Gateway 端验证

启用固件后，在 Gateway（BOX-3）启动：

```text
nsh> smart_home
```

预期日志：

```text
Node gateway listening on 0.0.0.0:18790
```

共享凭据使用 `/data/smart_home/secrets.json` 中的
`node_gateway.shared_token`，与 `app_bridge.shared_token` 分离。

### 4.3 使用 sensor_node 接入

在第二块已烧录 `sensor_node` 的设备上创建 `/data/sensor_node.json`：

```json
{
  "version": 1,
  "endpoint": "ws://192.168.1.103:18790/",
  "auth_token": "<node_gateway.shared_token>",
  "node_id": "living_sensor",
  "display_name": "Living Sensor",
  "profile": "combined",
  "temperature_c": 29.1,
  "fault": { "command": "", "mode": "none", "delay_ms": 0 }
}
```

将 endpoint 改成 Gateway 真机的实际 IP。Node 端执行：

```text
nsh> sensor_node /data/sensor_node.json
```

Gateway 串口应出现 Node 已连接及远程工具注册日志，工具名形如：

```text
node_living_sensor_get_temperature
node_living_sensor_set_fan
```

最后在 Gateway 的 Console 提示符中请求模型调用远程工具：

```text
home> 客厅温度多少
home> 打开客厅风扇
```

这一步同时验证 WebSocket 鉴权、Node catalog、动态工具注册、LLM tool call 和回包。
若只需要验证 Node 协议接入，观察注册日志即可；模型调用还依赖设备可稳定访问 LLM 服务。

## 5. 建议的执行顺序

1. Console 模式启动后，执行 App Bridge 的 snapshot、command、history 与 WebSocket
   测试。
2. 在同一网络条件下执行 `mcp_probe`，确认设备出向 MCP 链路。
3. 启用 Node Gateway 后，先只验证 `sensor_node` 注册，再进行 LLM 远程工具调用。
4. 最后再切换 LVGL，执行 MCP Settings discover 与手机 App UI 回归。

该顺序将本地 HTTP/WS、出向 MCP、入向 Node 和 LVGL 运行时隔离，出现异常时可以快速
定位到对应的协议或运行时层。
