# cAGENT 能力盘点与缺口分析

> 状态：基于 `packages/cAGENT/` 当前源码（2026-08-04）核实的快照；
> 2026-08-05 按 `cagent-addons-design-rev2.md` 再次对齐：任务 B/C 的 P1-P3
> 不需要修改 cAGENT core；工具枚举/schema/按名执行 API 改列为未来候选能力。
> 事实盘点（二、三的现状描述）未变。
> 文档目的：作为任务 B（多设备协作）与任务 C（MCP 协议扩展）开发的依据，
> 明确"内核已有能力""内核缺口""协议层该放哪"。
>
> 维护原则：`include/`、`src/`、`Kconfig` 为事实来源；本文件的"已具备"
> 与"缺口"描述均可在源码中验证，不把 architecture.md 中"目标职责"当作已实现。
>
> 配套文档：`openvela_smarthome/docs/design/cagent-addons-design-rev2.md`（B/C 当前实施方案，
> 本文件中的"addons 包"即该方案定义的 `packages/cagent_addons/`）。

## 一、结论摘要

- **cAGENT 的推理内核骨架是完整且清晰的**：同步 ReAct loop、工具/Skill/context/session
  注册、模型 provider 抽象、runtime 抽象、事件/策略回调、资源上限与超时控制均已落地。
- **任务 B/C 的当前路径不需要内核改动**：按 `cagent-addons-design-rev2.md`，
  sensor_node 是 cAGENT-less 的（自带命令表分发 invoke），gateway 侧使用已有的
  `agent_register_tool()` / `agent_unregister_tool()` 全量动态注册远程工具。mutation 由
  smart_home agent worker 串行化，远程工具 UI 目录由 glue catalog 提供。
- **工具枚举/schema/按名执行仍是有价值的未来公共 API（3.1）**，适用于 MCP exporter、
  通用 registry UI 或 agent-on-node，但不是任务 B/C 的前置条件，不在 P1-P3 修改。
- **运行时服务能力（按设计应放在内核之外）**：异步/步进 loop、长期记忆、持久化、
  工具并行、WebSocket transport、协议接入（node/mcp）。
  这些是"接入层/生态层"的职责，放入内核会违反 architecture.md 边界。
- **任务 B/C 的正确落点**：协议层（WS / OpenClaw node / MCP RPC）与远程工具 catalog
  放内核之外的 `cagent_addons` 包，依赖 cAGENT 现有公共 API；本期内核零改动，
  内核 WS 钩子已否决（见 3.7）。

## 二、已具备的功能

### 2.1 生命周期与运行（agent.h）

| 能力    | API                                                      | 备注                                                                                         |
| ----- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| 创建/销毁 | `agent_create` / `agent_create_simple` / `agent_destroy` | `_simple` 便捷入口；config 可 NULL                                                               |
| 同步推理  | `agent_run` / `agent_run_simple`                         | ReAct loop；busy 防重入；limits 可临时覆盖                                                           |
| 取消/重置 | `agent_cancel` / `agent_reset`                           | 协作式取消；reset 不清工具/skill/model                                                               |
| 运行限制  | `agent_set_limits` / `agent_limits_t`                    | max\_steps / 整体 timeout / 模型 timeout / 工具 timeout / max\_tool\_calls / max\_output\_tokens |
| 统计    | `agent_get_stats` / `agent_stats_t`                      | runs / iterations / arena 峰值等                                                              |

### 2.2 配置（config.h）

- `agent_config_default()` / `agent_config_tiny()`：两级预设。
- `agent_config_load(path, env_prefix, cb, ud)`：key=value 文件 + 环境变量，回调式解析。
- 编译期宏三级映射：直接定义 `CAGENT_*` > Kconfig `CONFIG_CAGENT_*` > 代码默认值。

### 2.3 工具（tools.h）

- `agent_register_tool` / `_simple` / `_unregister` / `_set_enabled` / `_is_enabled`。
- 6 个 flag：LLM 可见 / 只读 / 副作用 / 需确认 / 禁用 / 并行安全。
- schema 由 `tool_schema.c` 自动构建（内部函数，无公共枚举 API —— 见 3.1）。

### 2.4 Skill 与 Context（skill.h / context.h）

- Skill 注册/注销/便捷注册；`SUMMARY_ONLY` flag；上下文注入。
- Context provider 注册/注销；CRITICAL / NORMAL / OPTIONAL 三级；溢出不静默截断。

### 2.5 Session（session.h）

- 清除指定/全部 session；查询 session 数。
- 内部按完整 turn 淘汰，保证 tool calling 消息链完整（user → assistant(tool\_calls) → tool → assistant）。

### 2.6 模型 provider（model.h）

- provider 抽象（complete / cancel / destroy），`agent_set_model` / `_owned`。
- **脚本式 mock model**（`agent_model_mock_create`）——离线测试利器。
- OpenAI-compatible provider（`model_openai.h`）。

### 2.7 可观测与安全（event.h / policy.h）

- 10 种事件类型，同步回调；event callback 是纯通知，不能改 loop 行为。
- policy 回调：ALLOW / DENY / REQUIRE\_CONFIRM（MVP 等同 DENY，见 3.2）。
- 错误码 16 种，覆盖参数/内存/busy/limit/timeout/cancel/网络/解析等。

### 2.8 平台抽象（runtime.h）

- 回调集：malloc / free / now\_ms / sleep / log / http\_post / mutex / 临界区。
- openvela runtime 已实现 mbedTLS HTTPS（`runtime_openvela.c`），http\_post 支持任意 method。
- ESP-IDF / STM32 runtime 适配边界已保留。

### 2.9 资源约束

- 编译期上限全部有默认值：tools（12）/ skills（8）/ context providers（8）/ sessions（4）/ session messages（24）/ 各 buffer 大小。
- Kconfig profile：TINY / DEFAULT / REACT / CUSTOM。

## 三、核心推理能力缺口

### 3.1 工具"按名执行"与"枚举/schema"没有公共 API

- **现状**：工具注册后，执行入口被锁死在 ReAct loop 内部（`agent_loop.c` → `tool_registry.c` 内部函数）。
  `tool_schema.c` 的 `agent_tool_schema_build` 是内部函数；`include/` 无任何枚举/按名执行 API。
- **对任务 B/C 最小路径不是硬前提**：sensor_node（node 角色）按设计不链接
  cAGENT，用自带命令表（函数指针）分发 `node.invoke`；gateway 侧用已有的
  注册/注销 API 动态挂载远程工具。两个新 API 在 B/C 最小路径上均不出现。
- **未来需求来源**：
  1. **MCP exporter/server 模式**：
     暴露 `tools/list` 需要枚举 + schema；执行外部 `tools/call` 需要按名执行。
  2. **通用 registry 设置页 UI**：若未来不想由 glue catalog 维护远程工具目录，
     可以通过公共枚举 API 统一查看全部工具；当前 smart_home 直接合并本地静态表与
     glue catalog，任务 B/C 不依赖该 API。
  3. 未来 node 设备也运行 cAGENT 的拓扑（收到 invoke 后按名执行本机注册工具）。
- **未来补法（单独设计，不纳入任务 B/C）**：新增公共 API，放 `tools.h`：
  ```c
  /* 管理视角：枚举全部工具（含禁用），供 UI/审计 */
  int agent_tool_count(const agent_t *agent);
  int agent_tool_get_info(const agent_t *agent, int index,
                          agent_tool_info_t *info); /* name/flags/enabled */

  /* 模型视角：导出 LLM 可见且启用工具的 schema JSON
     （core 每次请求模型时本已生成，导出近零成本） */
  int agent_tool_schema_export(agent_t *agent, char *buf, size_t buf_size);

  /* 按名执行：供 MCP server 模式 / agent-on-node 使用 */
  int agent_tool_execute_by_name(agent_t *agent,
                                 const char *name,
                                 const char *arguments_json,
                                 const char *trace_id,
                                 char *output, size_t output_size);
  ```
  内部复用 `tool_registry.c` / `tool_schema.c` 现有逻辑即可。
- **`agent_tool_execute_by_name` 的安全条件（接受它的前提）**：
  1. 必须走与 ReAct loop 内调用**相同的完整执行链**
     （registry lookup → policy → guard → handler），不得直接调 handler，
     否则它成为绕过安全链的后门；
  2. 必须尊重 `AGENT_TOOL_FLAG_DISABLED`，已禁用工具返回结构化拒绝；
  3. 必须触发 TOOL 事件回调，保证 UI 事件时间线与审计不断链；
  4. 外部调用无 session：`session_id` 语义固定为 `"external"`（或 NULL 的
     明确定义），`trace_id` 由调用方传入，写入 policy 请求与审计。

### 3.2 REQUIRE\_CONFIRM 确认协议未实现（policy.h 明示）

- **现状**：`AGENT_POLICY_REQUIRE_CONFIRM` 在 policy.h 注释明确"MVP 等同 DENY"。
  loop 只识别 DENY 分支，无任何确认回调/异步确认机制。
- **影响**：危险工具（门锁、燃气、支付）无法"先确认再执行"，只能拒绝。
- **裁决**：defer。任务 B/C 演示用工具禁用 + policy DENY 足够；
  确认协议依赖异步/步进 loop（3.3），如做复赛任务 E（安全增强）再一并评估。

### 3.3 异步/流式推理未实现（无 agent\_begin / agent\_step）

- **现状**：`architecture.md` 规划的 `agent_begin` / `agent_step` 未实现（grep 无结果）；
  只有同步 `agent_run`。
- **影响**：无法流式输出模型回复；无法在推理中途接管。对 LVGL UI 体验有影响。
- **裁决**：defer。smart_home 已用 pthread worker + `lv_async_call` 在应用层
  桥接异步（`smart_home_lvgl_agent.c`），不阻塞任务 B/C。

### 3.4 长期记忆 / 持久化缺失（memory\_store.c 为占位）

- **现状**：`memory_store.c` 仅 46 行，snapshot/restore 全部返回 `AGENT_ERROR_NOTSUP`；
  session 纯内存，重启即失。README 也已承认"暂无持久化"。
- **对任务 B/C 无实际影响**（设计已免疫）：
  - node 远程工具在 node 重连握手时**自动重新注册**，无需持久化注册表；
  - MCP 工具清单在每次启动时向 gateway **重新拉取**；
  - 真正受影响的是定时器/设备状态等业务数据，README 已列为后续优化项。
- **裁决**：defer。flash-backed snapshot 为后续可选能力。

### 3.5 工具并行执行未实现（flag 已定义，loop 顺序执行）

- **现状**：`AGENT_TOOL_FLAG_PARALLEL_SAFE` 已定义，但 loop 明确顺序执行，
  无并行调度器、buffer pool、join 超时。
- **影响**：协议扩展后，多远端工具调用（多个 node / 多个 MCP server）也无法并行。
- **裁决**：defer。cagent_addons 的 gateway pending 表已按并发 invoke 设计
  （`CAGENT_ADDONS_MAX_PENDING`），core 将来支持并行时 addon 无需返工。

### 3.6 无测试 / 示例目录（tests/ examples/ 不存在）

- **现状**：仓库无 `tests/`、`examples/`；mock model 存在但无 host 回归测试。
- **影响**：协议层（WS/node/mcp）是新增网络代码，无测试将难以保证正确性。
- **裁决**：测试作为 **cagent_addons 的交付物**补齐，core 不动。
  cagent_addons 协议层按设计不依赖 NuttX/cAGENT 头文件，可用主机 gcc 直接构建：
  ws 帧编解码、握手状态机、invoke 超时、掉线唤醒 pending 等单元/集成测试
  （golden frames + mock gateway）。glue 层则链接 cAGENT standalone 静态库，
  使用真实 registry/guard/schema 和 mock model 测 catalog mutation、inflight 引用与
  浅拷贝生命周期。反过来，"能在主机上跑测试"也是分层边界的检验标准。

### 3.7 Runtime 无 WebSocket / 流式 transport【任务 B/C 直接相关】

- **现状**：`agent_runtime_t` 只有一次性 `http_post`（支持任意 method，
  但为"请求-响应"模型）；无 WS 握手/帧、无 SSE 流式读取。
- **关键事实：设备端不存在 SSE/流式需求**。MCP 按 sidecar 架构
  （`cagent-addons-design-rev2.md` §四），Streamable HTTP / SSE 止于主机 gateway，
  设备只看到 plain HTTP POST 的请求-响应。本缺口因此收缩为"WS 长连接"一项。
- **裁决**：WS 帧层落在 cagent_addons 协议层（自带 socket 实现），
  **内核 WS 钩子已否决**——内核保持零 transport 绑定。
- **配套缺口（cagent_addons 侧）**：WS client 需要熵源生成帧 mask key，
  cAGENT runtime 无 random 回调（ai_agent 使用其私有 `agent_secure_random`）。
  cagent_addons 自带熵源方案（NuttX `/dev/urandom` 或 getrandom），
  不要求内核新增回调。

### 3.8 JSON 能力缺失（core 为手写解析，无 cJSON 依赖）

- **现状**：cAGENT core 用 `strstr` 式手写解析（`llm_parse.c`）；
  项目 defconfig 已开 `CONFIG_NETUTILS_CJSON=y`，但 cAGENT 未使用。
- **影响**：协议层（node/mcp）需频繁构造/解析 JSON（connect/invoke/result、
  tools/list/tools/call），手写解析脆弱且易错。
- **裁决**：cagent_addons 直接依赖 cJSON（`apps/netutils/cjson`）；
  core 的手写解析只服务于 OpenAI 响应的窄场景，**不重构、不引入依赖**。

## 四、缺口与任务 B/C 的映射

| 缺口                  | 任务 B（OpenClaw Node）            | 任务 C（MCP 扩展）             | 落点                      |
| ------------------- | ------------------------------ | ------------------------ | ----------------------- |
| 3.1 按名执行 API        | 当前不需要；agent-on-node 拓扑才需要   | 当前不需要；未来 MCP exporter 执行 tools/call | **未来内核候选**（不进入 P1-P3）      |
| 3.1 枚举/schema API   | 当前不需要                       | 当前由 glue catalog 枚举；未来 exporter 可使用 | **未来内核候选**（不进入 P1-P3） |
| 3.7 WS transport    | WS 长连接 + 帧 + 心跳                | 设备端无需求（SSE 止于 sidecar）   | **内核外 cagent_addons**（内核钩子已否决） |
| 3.8 JSON 能力         | 协议消息编解码                        | 协议消息编解码                  | **内核外 cagent_addons**（用 cJSON） |
| 3.6 测试              | 协议层回归保障                        | 同左                       | **内核外 cagent_addons**（host 侧测试） |
| 3.5 并行              | 多节点工具可并行（可选）                   | 多 server 工具可并行（可选）       | 后续演进；addon 已按并发设计       |
| 3.2/3.3/3.4         | 确认/体验/持久化，B/C 演示不受影响           | 同左                       | 内核后续演进                  |

**结论**：任务 B/C 的协议层、远程工具 catalog 和 UI 枚举全部落在内核之外的
`cagent_addons`/smart_home glue；P1-P3 不修改 cAGENT。未来若实现 3.1 的公共 API，
`execute_by_name` 仍必须复用完整 policy → guard → handler 执行链，并以独立设计、测试和
提交推进，不能反向阻塞当前 Node/MCP 交付。

## 五、未来内核候选 API（任务 B/C 当前不需要）

以下 API 不进入 cagent_addons P1-P3，只在 MCP exporter、通用 registry UI 或
agent-on-node 的真实需求出现后重新评审：

1. `include/cagent/tools.h` + `src/tools/tool_registry.c`：
   管理视角枚举 `agent_tool_count()` / `agent_tool_get_info()`
   （供 smart_home 设置页与 hub 广播使用）。
2. `include/cagent/tools.h` + `src/tools/tool_schema.c`：
   模型视角 `agent_tool_schema_export()`（导出 LLM 可见且启用工具的 schema，
   供 MCP server 模式的 tools/list 使用）。
3. `include/cagent/tools.h` + `src/tools/tool_registry.c`：
   `agent_tool_execute_by_name()`，**必须**复用完整执行链
   （policy → guard → handler）、尊重 DISABLED、触发 TOOL 事件、
   明确 external session 语义（供 MCP server 模式的 tools/call、
   未来 agent-on-node 使用）。
4. ~~`runtime.h` 增加 WS 回调钩子~~：**已否决**。WS/熵源由 cagent_addons
   协议层自带（独立 socket + `/dev/urandom`），内核保持零 transport 绑定。

> 说明：1-3 是未来可能的“把内部注册表/schema/执行逻辑安全地暴露为公共 API”，
> 不是当前实施清单。若启动，需同步更新 `docs/api_reference.md`、本文件状态并单独测试
> 禁用拒绝、policy、生存期、事件和并发条件。
