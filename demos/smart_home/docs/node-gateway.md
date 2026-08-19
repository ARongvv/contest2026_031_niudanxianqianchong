# smart_home Node gateway 联调

> 状态：Goldfish Hub/Node 的 WebSocket 注册链路已在双模拟器中验证。Node 经
> emulator console `redir` 连接 Hub，能够完成 `ws-up -> challenged -> registered`。
> 本文的 QEMU 流程以该联调结果为准。
>
> 设计与容量的事实来源：`docs/design/cagent-addons-design-rev2.md`；
> 库层契约：`packages/cagent_addons/include/cagent_addons/*.h`。

启用 `CONFIG_SMART_HOME_NODE_GATEWAY=y` 后，`smart_home` 是可信的 OpenClaw
Node gateway：它在 `0.0.0.0:18790` 监听 WebSocket，验证 Node 的共享 token，随后将
Node 上报的命令动态注册到本机 cAGENT。

本实现只负责 Node 接入和工具生命周期；LVGL 的 Node 状态、远程工具开关和手机端展示将在
后续阶段接入。工具仍遵循 cAGENT 的普通 registry，不引入 provider 懒发现。

## 一、构建与部署

### 1.1 重新生成配置并编译

board defconfig 已开启 `CONFIG_SMART_HOME_NODE_GATEWAY=y` 与
`CONFIG_CAGENT_MAX_TOOLS=24`。首次构建（或改过 defconfig 后）需重新生成
`nuttx/.config`：

```bash
cd /home/arongw/openvela
source build/envsetup.sh
./build.sh vendor/openvela/boards/vela/configs/goldfish-smart_home/ -j$(nproc)
```

确认新配置生效：

```bash
grep -E 'SMART_HOME_NODE_GATEWAY|CAGENT_ADDONS_NODE_GATEWAY|CAGENT_MAX_TOOLS' nuttx/.config
# 期望：CONFIG_SMART_HOME_NODE_GATEWAY=y / CONFIG_CAGENT_ADDONS_NODE_GATEWAY=y / CONFIG_CAGENT_MAX_TOOLS=24
```

> 注：`CONFIG_NETUTILS_CJSON` 会被其他选项自动拉起，`savedefconfig` 后可能从
> defconfig 消失，但 `.config` 中仍为 `y`——这是正常现象，无需手动加回。

### 1.1a sensor_node（node 侧）软链接与编译

`sensor_node` 复用同一 goldfish board，只需在 vendor 下建真实目录并软链
**defconfig 文件**（不是目录——链接整个目录会导致 `File Make.defs could not be found`）：

```bash
cd /home/arongw/openvela

# 链接 app（已做）
ln -sfnT ../../openvela_smarthome/demos/sensor_node packages/demos/sensor_node

# 链接 board defconfig 文件（关键：链文件，不链目录）
mkdir -p vendor/openvela/boards/vela/configs/goldfish-sensor_node
ln -sfn "$(pwd)/openvela_smarthome/board/goldfish-arm64/configs/sensor_node/defconfig" \
  vendor/openvela/boards/vela/configs/goldfish-sensor_node/defconfig

# 验证（应看到 defconfig 是软链、目录是真实目录）
ls -la vendor/openvela/boards/vela/configs/goldfish-sensor_node/

# 编译
./build.sh vendor/openvela/boards/vela/configs/goldfish-sensor_node -j$(nproc)
```

### 1.2 部署凭据并启动

`smart_home` 启动时强制读取 secrets 文件，**缺少它应用会直接退出**（见 2.1）。
先部署再启动：

```bash
mkdir -p cmake_out/vela_goldfish-arm64-v8a-ap
cp nuttx/.config cmake_out/vela_goldfish-arm64-v8a-ap/
cp nuttx/vela_*.bin cmake_out/vela_goldfish-arm64-v8a-ap/
ln -sf ../../nuttx/nuttx cmake_out/vela_goldfish-arm64-v8a-ap/nuttx
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap

# 另一个终端：
adb shell mkdir -p /data/smart_home
adb push secrets.json /data/smart_home/secrets.json   # 按第二节准备
adb push packages/demos/smart_home/res /data/          # 技能/资源（如未部署）
```

NSH 中验证网络后启动：

```text
nsh> ifup eth0
nsh> renew eth0
nsh> smart_home "打开客厅灯"        # 带参 console 模式，先验证本地闭环
```

## 二、运行时凭据

在目标设备创建 `/data/smart_home/secrets.json`。共享 token 不能放入 defconfig、日志或
仓库；Goldfish 演示可使用随机长字符串，真机应优先替换为安全存储后端。
`secrets.json` 结构由 `demos/smart_home/config/secrets.example.json` 定义，
**普通 `smart_home_config_store` 不加载/回退/写回 secrets**（config/README 约定），
由 node gateway 专用 loader 消费：

```json
{
  "version": 1,
  "model": { "api_key": "" },
  "node_gateway": { "shared_token": "replace-with-a-random-demo-token" },
  "mcp_bridge": { "service_token": "" }
}
```

启动时 gateway 只读取 `version` 与 `node_gateway.shared_token`。读取后内存中的
token 副本会被覆写清零，不会出现在日志中。

### 2.1 ⚠ 无有效凭据时整个应用拒绝启动

文件不存在、格式不正确、版本不是 `1` 或 token 为空时，
`smart_home_agent_app_init()` 返回失败，**整个 smart_home（包括本地灯光/空调控制）
都不会启动**。这是避免无凭据监听的有意行为，但对演示环境是硬约束：

- 部署新固件后第一件事就是 push secrets.json；
- 启动即退出时先查 syslog 中的 `Node gateway secrets unavailable`；
- 后续版本计划降级为"gateway 不可用、本地功能保留"（行为变更待确认，
  见 `smart_home_agent.c` 中 `smart_home_node_gateway_start` 的失败路径）。

## 三、两节点演示

复用同一份 `sensor_node` 固件，准备两份运行时配置，二者的 `endpoint` 与
`auth_token` 必须指向同一个 smart_home gateway。

```json
// living_sensor.json
{
  "version": 1,
  "endpoint": "ws://10.0.2.2:18790/",
  "auth_token": "replace-with-a-random-demo-token",
  "node_id": "living_sensor",
  "display_name": "Living Room Sensor",
  "profile": "temperature",
  "temperature_c": 29.1
}
```

```json
// living_fan.json
{
  "version": 1,
  "endpoint": "ws://10.0.2.2:18790/",
  "auth_token": "replace-with-a-random-demo-token",
  "node_id": "living_fan",
  "display_name": "Living Room Fan",
  "profile": "fan"
}
```

### 3.1 QEMU 端口转发（已验证）

gateway 位于 Hub Goldfish guest 内，sensor_node guest 的 `10.0.2.2` 指向宿主机。
因此宿主机需使用 Hub 的 emulator console `redir`，把宿主机 TCP `18790` 转发至 Hub
guest TCP `18790`：

```bash
HUB_CONSOLE_PORT=5554
{
  printf 'auth %s\n' "$(cat "$HOME/.emulator_console_auth_token")"
  printf 'redir add tcp:18790:18790\n'
  printf 'redir list\n'
  printf 'quit\n'
} | nc 127.0.0.1 "$HUB_CONSOLE_PORT"
```

预期输出含 `tcp:18790 => 18790`。其中 Hub 的 console 端口可通过 `adb devices`
确认；若已有残留规则，先以同一控制台连接发送 `redir del tcp:18790`。

`adb forward tcp:18790 tcp:18790` 不适用于这条 Node-to-Hub 链路：虽然宿主机端口可
连接，但数据不会正确到达 Goldfish guest 的 Node gateway。真机部署不需要转发，Node
endpoint 直接填写 smart_home 的局域网 IP 即可。

### 3.2 双 QEMU 实例注意事项

- 每个实例使用独立的 `cmake_out` 目录（实例锁 `multiinstance.lock` 按目录隔离）；
- sensor_node 固件用 `board/goldfish-arm64/configs/sensor_node/` 配置单独编译；
- 演示顺序：先启动 Hub 并确认 `gateway listening` → 设置 `redir` → 启动 Node；
- 两个实例都可能显示 `eth0=10.0.2.15`，这是独立 QEMU NAT 的正常现象；
- 同一个 `node_id` 只允许一个活跃 Node。Hub 会关闭同 ID 的旧连接；若两个
  `living_fan` 实例同时运行，就会互相触发重连。Node 端用 `ps`、`kill <PID>` 清理
  多余实例，或重启 Node 模拟器后只执行一次 `sensor_node`；
- 若要同时演示两个 Node，分别使用 `living_sensor`、`living_fan` 等不同的 `node_id`
  和不同配置文件启动。

## 四、验收清单

两节点上线后，按以下顺序确认（当前验证手段只有 console 与 syslog，见 7.1）：

| # | 检查项 | 预期 |
|---|--------|------|
| 1 | smart_home 启动 | syslog 出现 `Node gateway listening on 0.0.0.0:18790` |
| 2 | sensor_node 上线 | smart_home syslog 出现 `Node tool registered: node_living_sensor_get_temperature` |
| 3 | 第二个节点上线 | syslog 出现 `Node tool registered: node_living_fan_set_fan` |
| 4 | 查询类调用 | `smart_home "客厅现在温度多少"` 触发 `node_living_sensor_get_temperature` |
| 5 | 控制类调用 | `smart_home "打开客厅风扇"` 触发 `node_living_fan_set_fan` |
| 6 | 节点掉线 | 停止一个 sensor_node，对应工具排队注销（时机见 6.1） |

## 五、工具注册结果

两节点演示配置上线后，cAGENT registry 应新增以下两个 Node 工具，`group_id=10`：

| Node | 工具 |
|---|---|
| `living_sensor` | `node_living_sensor_get_temperature` |
| `living_fan` | `node_living_fan_set_fan` |

若接入第三个只读环境 Node，按同一模式生成第三个工具；工具名由
`node_<node_id>_<command>` 稳定生成。Goldfish smart_home defconfig 将
`CAGENT_MAX_TOOLS` 设为 `24`，预算为 9 个本地工具 + 最多 8 个 Node 工具 +
最多 4 个 MCP 工具。

## 六、生命周期与并发

WebSocket reader 线程只处理协议、更新远程 catalog，并通过信号量唤醒 smart_home 的
catalog worker。后者与 `agent_run()` 共用一把 application mutex，顺序执行
`agent_register_tool()` / `agent_unregister_tool()`；因此 Node 可以在没有用户对话时
立即完成注册，同时不会与模型读取工具表并发冲突。

Node 断线或同 ID 重连时，gateway 按 `connection_gen` 标记旧工具离线并排队注销。
停机顺序为：停止 gateway、排空注销 mutation、销毁 gateway/transport/catalog、
最后销毁 cAGENT。

### 6.1 对话进行中的节点变更会延迟生效

`agent_run()` 在一次 AI 对话期间全程持有 application mutex（一次 run 可能数十秒）。
因此**对话进行中节点上线/掉线，注册表变更会排队到本轮对话结束才生效**。设计演示
脚本时注意：不要在对话输出进行中拔节点并立刻追问——工具列表的更新会延迟出现，
这是预期行为而非故障。

## 七、当前边界与未实现项

### 7.1 验证手段仅 console

- LVGL Settings 的 Tool Directory 会在进入 Settings 时刷新当前 local、`node_*` 与
  `mcp_*` 工具。本地工具可通过开关启用或禁用；Node 与 MCP 工具仅展示其当前状态，
  生命周期仍由各自的连接管理；
- Console 的 `node status` 会列出已完成注册的 `node_*` 工具及启用状态；工具名中
  包含 Node ID，因此它可作为 Node 联调的只读快照；
- Node gateway 的目录同步、发送、WebSocket accept 和每个 Node reader 使用由
  smart_home 提供的 PSRAM 栈。按四个 Node 连接计算，七个 8 KiB 栈约占 56 KiB；
  Box3 的 bulk pool 因此配置为 256 KiB，栈申请失败时对应服务或连接会直接失败，
  不会改用 SRAM；
- LVGL 无 Node 在线状态展示；
- 联调验证请使用带参 console 模式（`smart_home "…"`）和 syslog。

### 7.2 准入控制：token 即准入（与 rev2 的已知偏差）

rev2/HANDOVER 曾规划"allowlist/priority 来自 `/data/cagent_addons.json`，超配额按
priority 降序选取"。**该机制尚未实现**：当前任何持有有效 shared_token 的 Node 均可
注册，达到 `CAGENT_ADDONS_MAX_NODES` 后新连接被拒绝；`CAGENT_ADDONS_MAX_NODE_TOOLS`
限制单个 Node 注册进 LLM 的工具数。竞赛 MVP 规模（2-3 个节点）下 token 准入足够；
若后续补实现，需同步更新本文与 rev2。

### 7.3 明文 WebSocket

Node 链路使用 `ws://` 明文：shared_token 验证了 Node 身份，但流量不加密。
这是竞赛 LAN 演示的 demo-only 选择，不得沿用到手机 App 或公网部署。

### 7.4 容量与超时（默认值，Kconfig 可调）

| 项 | 值 | 含义 |
|---|---|---|
| `CAGENT_ADDONS_MAX_NODES` | 4（上限 8） | 同时在线 Node 数，第 5 个连接被拒绝 |
| `CAGENT_ADDONS_MAX_NODE_COMMANDS` | 8 | 单个 Node 上报命令数上限 |
| `CAGENT_ADDONS_MAX_NODE_TOOLS` | 8 | Node 工具注册进 LLM 的上限 |
| `CAGENT_ADDONS_MAX_PENDING` | 4 | gateway 并发 invoke 上限 |
| `CAGENT_ADDONS_INVOKE_TIMEOUT_MS` | 10000 | invoke 单调时钟 deadline（真正中断等待） |
| `CAGENT_ADDONS_RECV_BUF_SIZE` | 4096 | 单帧接收缓冲 |
| `CAGENT_ADDONS_MAX_RESULT_SIZE` | 4096 | 单次 invoke 结果上限 |
| transport / gateway / worker 栈 | 8192 | 各内部线程栈 |

## 八、故障排查

| 现象 | 排查 |
|---|---|
| smart_home 启动即退出 | 第一嫌疑是 secrets.json：路径、version=1、token 非空；查 syslog `Node gateway secrets unavailable` |
| sensor_node 反复重连、停在认证阶段 | token 不一致；比对两侧文件（不要从日志找 token，它不会被打印） |
| Node 显示已连接但工具没出现 | 查 smart_home syslog 是否有 `Node tool registered`；无则查 catalog worker 是否存活、单 Node 命令数是否超 `MAX_NODE_COMMANDS` |
| 对话中拔节点后工具仍在 | 预期行为，等本轮 run 结束（见 6.1）；run 结束后仍存在再查 unregister mutation |
| 第 5 个 Node 连不上 | 达到 `MAX_NODES=4`，拒绝是设计行为 |
| invoke 返回超时错误 | 节点端命令执行超过 10 s deadline；可用 sensor_node 的 `fault.mode=delay` 复现验证 |

## 九、回归测试

修改 `packages/cagent_addons` 或 gateway 装配代码后，板级联调前先跑 host 测试：

```bash
ctest --test-dir packages/cagent_addons/build/ws-next --output-on-failure   # 6 个用例
ctest --test-dir demos/sensor_node/tests/build --output-on-failure          # 2 个用例

# smart_home config_store（backends/settings/state 持久化）host 测试
cmake -S demos/smart_home/tests -B build/tests && cmake --build build/tests
ctest --test-dir build/tests --output-on-failure                            # 13 个用例
```

build 目录不存在时按 `packages/cagent_addons/CMakeLists.txt` 与
`demos/sensor_node/README.md` 的说明重新配置（`-DCADDONS_BUILD_TESTS=ON`）。

## 十、真机部署（P4，未实施）

ESP32-S3 BOX-3 上 gateway 监听逻辑不变：secrets.json 放 LittleFS 同路径，
sensor_node 的 endpoint 改为 BOX-3 的局域网 IP。Wi-Fi 凭据仍由 `/data/wapi.conf`
独立管理。真机联调属于 P4 阶段，本文步骤以 Goldfish 为准。
