# cagent_addons

cagent_addons 是 cAGENT 嵌入式 Agent 核心库的**可选扩展包**，提供两类
"agent 与外部能力的连接"能力：

- **OpenClaw Node 协议**（复赛任务 B，多设备协作）：WebSocket 上的
  node/gateway 帧协议，gateway 把远程节点的工具以 `node_<id>_<command>`
  命名空间注册进 cAGENT，实现跨设备工具能力共享。
- **MCP 桥接**（复赛任务 C）：设备端的薄 HTTP RPC client，经主机侧
  MCP gateway（`scripts/mcp_gateway.py`）接入第三方 MCP 服务（如高德地图），
  外部工具以 `mcp_<server>_<tool>` 命名空间注册进 cAGENT。

设计方案见 `openvela_smarthome/docs/cagent-addons-design-rev2.md`；
内核缺口论证见 `packages/cAGENT/docs/capability-gap.md`。

## 分层与依赖纪律

```text
src/node/   协议层：ws 帧 + OpenClaw 帧编解码 + 连接管理
            不依赖 cAGENT，可独立编译（sensor_node 等无 LLM 设备只链接本层）
src/glue/   粘合层：远程工具动态注册/注销/分发/结果归一化
            只使用 cAGENT public API（depend CAGENT）
src/mcp/    MCP RPC client（depend CAGENT）
```

- cAGENT core 不被修改，也不被本包反向依赖内部实现。
- JSON 编解码使用 openvela 已有的 cJSON（`apps/netutils/cjson`）。
- 熵源由本包自带（`/dev/urandom` / getrandom），用于 WS 帧 mask key。

## 目录结构

```text
packages/cagent_addons/
├── Kconfig / Make.defs / Makefile / CMakeLists.txt
├── include/cagent_addons/
│   ├── errors.h / limits.h
│   ├── cjson_compat.h    # host/openvela cJSON include 路径适配
│   ├── transport.h / ws_transport.h / ws_frame.h
│   ├── node_proto.h      # 帧编解码（纯协议，无平台依赖）
│   ├── node_client.h     # node 角色：连接 gateway、响应 invoke
│   ├── node_gateway.h    # gateway 角色（depend CAGENT）
│   └── mcp_bridge.h      # MCP RPC client（depend CAGENT）
└── src/
    ├── node/             # 协议层（不依赖 CAGENT）
    │   ├── ws_frame.c
    │   ├── node_proto.c
    │   ├── node_client.c
    │   └── node_gateway.c
    ├── transport/
    │   └── ws_transport.c
    ├── glue/
    │   └── remote_tool.c
    └── mcp/
        └── mcp_bridge.c
```

## Kconfig 选项

| 选项 | 依赖 | 用途 |
|------|------|------|
| `CAGENT_ADDONS` | — | 包总开关 |
| `CAGENT_ADDONS_NODE` | CAGENT_ADDONS | 协议层（不依赖 CAGENT） |
| `CAGENT_ADDONS_NODE_CLIENT` | NODE | node 角色（sensor_node 使用） |
| `CAGENT_ADDONS_NODE_GATEWAY` | NODE + CAGENT | gateway 角色（smart_home 使用） |
| `CAGENT_ADDONS_MCP` | CAGENT | MCP 桥接 |

容量选项（`CAGENT_ADDONS_MAX_NODES` 等）见 Kconfig "Capacity limits" 菜单。

## 构建接入

照 cAGENT 的软链接模式接入 openvela 工作区：

```bash
cd openvela
ln -sfnT ../openvela_smarthome/packages/cagent_addons packages/cagent_addons
```

随后在两套 defconfig 中打开所需选项（见设计文档 §4）。

主机侧独立构建（协议层 + 单元测试，无需 NuttX）：

```bash
cmake -B build-host -DCADDONS_BUILD_TESTS=ON packages/cagent_addons
cmake --build build-host
ctest --test-dir build-host
```

P2 gateway/glue 测试需要链接真实 cAGENT 静态库：

```bash
cmake -B build-p2-host -DCADDONS_BUILD_TESTS=ON \
  -DCADDONS_NODE=ON -DCADDONS_GATEWAY=ON packages/cagent_addons
cmake --build build-p2-host
ctest --test-dir build-p2-host
```

## P2 线程与生命周期契约

- transport reader 回调只更新 Node/catalog 状态并入队，不直接修改 cAGENT registry，
  也不执行慢设备动作；
- `agent_run()`、catalog mutation、工具启停由应用的单一 agent worker 串行执行；
- `caddons_node_gateway_stop()` 会使 pending invoke 失败并排队注销在线节点工具；应用必须
  应用完注销 mutation，确认调用已排空后，才能 destroy gateway/catalog；
- 远程工具结果缓冲由 catalog 持有，只保证有效到下一次远程工具执行。当前 smart_home
  单 worker 模型同一时刻只允许一个远程工具执行。

## 状态

实施已于 2026-08-05 开始，按
`cagent-addons-design-rev2.md` §12 分期进行：

- 步骤 0：公共错误码、资源上限、transport ownership、Node API 和
  ai_agent protocol-3 golden frames 已冻结；
- 步骤 1：有界 WS frame/upgrade、真实 plain-TCP WebSocket client/server 与 Node
  evt/req/res/connect codec 已实现；支持 partial I/O、client mask、ping/pong、close、
  每 peer send mutex、I/O deadline 和 stop/join；
- 步骤 2（client 部分）：Node client 的 challenge/connect、精确 `toolMeta`、有界命令
  worker、结果回送、keepalive 和带 jitter 指数退避重连已实现；
- P2 核心：Node gateway 鉴权/节点表/pending invoke/真实 deadline、远程 catalog、动态
  registry mutation、引用排空和结果归一化已实现；使用 mock transport + 真实 cAGENT
  host 测试通过；
- host 已通过真实回环链路验证 Node client → WS gateway → 动态注册 → cAGENT
  `agent_run()` → Node command → result，并覆盖 gateway 重启后的注销、自动重连和重注册；
- `demos/sensor_node` 与 Goldfish 最小 defconfig 已实现：运行时 JSON 配置、非空共享
  token、`temperature/fan/combined` profile、精确 schema，以及 `delay/error` 故障注入；
  固件已在 `-Werror` 下完整编译、链接成功，并确认未启用 cAGENT/LVGL；
- 仍待实现：官方语义 Python mock gateway 和 smart_home adapter。因此 Node 角色已有
  openvela 应用级样例，但任务 B 的双端应用交付尚未完成；MCP bridge 仍待 P3。

- P1 协议层 + node demo（`src/node/` + `demos/sensor_node`）
- P2 gateway + glue（任务 B 完成）
- P3 MCP 桥（任务 C 完成）
- P4 真机与打磨
