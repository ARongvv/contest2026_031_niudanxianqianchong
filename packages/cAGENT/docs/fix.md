**总体判断**
从经常接入和调试 `cAGENT` 的开发者视角看，它的方向是对的：核心库很轻，边界清楚，适合嵌入式场景。它不像一个大而全的 Agent framework，而是一个同步 ReAct 推理内核：session、context、LLM、tool、runtime 五块拼出闭环。

但它现在最大的问题也来自这个“轻”：核心能力已经跑通，工程化契约还不够硬。开发者长期使用时，痛点会集中在可观测性、可扩展性、资源预算、安全策略和平台适配这几块。

**一、Public API 不够完整**
现在公开入口主要在 [agent.h](../include/agent.h#L1)，有 `agent_create()`、`agent_register_tool()`、`agent_skill_register()`、`agent_chat()`。

问题是：架构文档强调 context provider injection，但 `agent_context_register()` 目前不是正式 public API。对于真实产品，开发者很常见的需求是注入：

- 当前设备状态
- 用户偏好
- 地理位置/场景
- 电量、网络、传感器状态
- 安全策略
- 产品模式

如果只能通过 Skill 注入，语义会混乱。Skill 是知识和行为指导，设备状态是动态上下文，二者不应该混在一起。

优化方向：把 `agent_context_register()` 提升为稳定公开 API，并明确 provider 生命周期、优先级、buffer 限制和错误语义。

**二、Tool 可见性和执行策略不一致**
`agent_tool_t` 有 `AGENT_TOOL_FLAG_LLM_VISIBLE`、`SIDE_EFFECT`、`DISABLED`、`REQUIRES_CONFIRM`，这是好设计。但 [agent_schema_build](../src/tools/agent_schema.c#L22) 当前会遍历所有工具生成 schema，没有过滤 `LLM_VISIBLE` 或 `DISABLED`。

这会导致一个实际问题：模型可能看到不该看到的工具，然后调用它，最后 guard 再拒绝。对 Agent 来说，这会浪费 token、增加错误路径，也可能让模型误判系统能力。

优化方向：

- schema 构建只暴露 `LLM_VISIBLE` 且非 `DISABLED` 的工具。
- `REQUIRES_CONFIRM` 工具可以暴露，但 schema 或描述里应带确认语义。
- 增加 tool namespace/group 的过滤能力，比如当前场景只开放 `system.query`，不开放 `device.control`。
- guard 不应只是最后防线，schema 层也要做能力裁剪。

**三、同步单实例模型简单，但并发能力弱**
[agent_chat](../src/core/agent_core.c#L100) 用 `ctx->core.busy` 做全局 busy 锁。这对 MVP 很稳，但开发者实际接入时会遇到：

- 多 session 不能并发。
- 一个慢 LLM 请求会阻塞整个 agent ctx。
- 工具执行如果卡住，其他请求也进不来。
- 上层必须自己创建多个 agent ctx 或排队。

嵌入式里同步模型没问题，甚至是优点，但要把并发边界讲清楚。

优化方向：

- 保持 `agent_chat()` 同步 API。
- 增加可选的 `agent_cancel()` 或 timeout/cancel token。
- 明确一个 `agent_ctx_t` 是单请求执行器，还是多 session 容器。
- 后续如果做 async，建议放在 `agent_service`，不要污染 core。
- 如果 core 要支持并发，至少从 session-level lock 开始，不要直接引入复杂 worker。

**四、固定 buffer 策略适合嵌入式，但需要资源预算工具**
现在很多限制是编译期常量：`AGENT_CONTEXT_MAX`、`AGENT_REQUEST_MAX`、`AGENT_TOOL_OUTPUT_MAX`、`AGENT_SESSION_CONTENT_MAX` 等，定义在 [types.h](../include/agent/types.h#L1)。

这是嵌入式友好的，但开发者会经常踩这些坑：

- tool schema 稍多就超 `AGENT_TOOLS_JSON_MAX`。
- skill 内容稍长就被拒绝。
- session 历史累计后请求体超限。
- LLM 响应稍大解析失败。
- 错误只返回 `AGENT_ERROR_LIMIT`，不知道是哪一块爆了。

优化方向：

- 增加 `agent_get_stats()` 或 debug event，报告 context/tool/session/request 实际占用。
- `AGENT_ERROR_LIMIT` 附带更细错误来源，比如 `context overflow`、`tools schema overflow`。
- 提供一个 dry-run API：构建请求但不发送，用于调参。
- Kconfig 中补齐所有关键 buffer 的配置项，而不是只暴露一部分。

**五、Session 模型对 tool calling 友好，但还不够产品化**
[agent_session.c](../src/memory/agent_session.c#L1) 的优点是知道 OpenAI tool calling 的消息顺序，淘汰历史时不会轻易留下孤立 tool message。

但问题也明显：

- session 只有内存态，重启丢失。
- 没有 session reset/delete API。
- 没有按 token 或请求大小做裁剪，只按 message 数。
- `session_id` 数量固定，满了以后开发者只能收到 limit。
- assistant/tool 内容被固定长度截断风险较高。

优化方向：

- 增加 `agent_session_reset(ctx, session_id)`。
- 增加 `agent_session_delete()` / `agent_session_clear_all()`。
- 增加按请求体预算裁剪历史的逻辑。
- 后续做可选持久化，但不要放进 core 默认路径。
- session 裁剪策略最好可配置：最近 N 轮、摘要、只保留 tool-free 对话等。

**六、LLM 层目前绑定 OpenAI-compatible，抽象还浅**
[agent_llm_openai.c](../src/llm/agent_llm_openai.c#L1) 目前直接构建 OpenAI-compatible 请求。这对 MVP 很实用，但长期会遇到：

- 不同供应商 tool calling 字段略有差异。
- 本地模型可能没有 tool_calls，只能 JSON mode。
- 有的模型需要额外参数，比如 temperature、max_tokens。
- 错误响应没有很好解析，网络错误和 LLM 错误区分不够细。

优化方向：

- `agent_config_t` 增加 LLM 参数：temperature、max_tokens、top_p。
- 解析 error body，区分 HTTP/network/provider/model parse。
- 把 provider 适配变成轻量 vtable，但不要过早做复杂 router。
- 保持 OpenAI-compatible 为默认 backend。

**七、Runtime 抽象是亮点，但 openvela HTTP 实现还偏 demo**
`agent_runtime_t` 是这套架构最重要的嵌入式边界，见 [runtime.h](../include/agent/runtime.h#L1)。这个设计值得保留。

但 [agent_runtime_openvela.c](../src/runtime/agent_runtime_openvela.c#L1) 里有几个产品化风险：

- TLS 当前 `VERIFY_NONE`，安全上不能作为默认生产配置。
- HTTP request 用固定栈 buffer 拼接，body 大时容易截断。
- 没有处理 chunked response。
- 没有 HTTP status code 解析。
- debug 用 `printf`，没有统一走 runtime log。

优化方向：

- header 和 body 分段写，不要拼成一个小 request buffer。
- 加 CA/证书校验配置。
- 解析 status code，非 2xx 返回明确错误。
- 支持 chunked transfer 或要求服务端关闭 chunked 并在文档说明。
- 所有日志走 `runtime.log`。

**八、可观测性不足，调试 Agent 会痛**
Agent 最难调的是“为什么模型这么做”。现在 event 有 request、LLM start/end、tool start/end、reply/error，见 [event.h](../include/agent/event.h#L1)。这是起点，但还不够。

开发者经常需要看到：

- 最终发给 LLM 的 messages。
- tools schema。
- context prompt。
- LLM 原始响应。
- 解析出的 tool_calls。
- 每次 ReAct iteration 的编号。
- 哪个 session 被裁剪了。

优化方向：

- 增加 debug event 类型，或在 `config.debug` 下输出结构化 trace。
- 增加 `AGENT_EVENT_ITERATION_START/END`。
- 增加 `AGENT_EVENT_CONTEXT_BUILT`、`AGENT_EVENT_TOOL_SCHEMA_BUILT`，注意可配置脱敏。
- API key、用户隐私、tool output 要有脱敏策略。

**九、安全策略现在只是雏形**
guard 层在 [agent_guard.c](../src/tools/agent_guard.c#L1)，有 disabled、confirm-not-implemented、input size、side-effect rate limit。方向对，但真实设备控制场景还不够。

优化方向：

- 增加 tool permission domain，比如 read-only、device-control、network、storage。
- `REQUIRES_CONFIRM` 要形成完整状态机，而不是直接拒绝。
- 工具参数最好做 JSON schema 校验。
- side-effect 工具建议默认不允许连续多次调用。
- 增加 dry-run tool 或 preview 机制，让模型先生成计划，再确认执行。

**我会优先改的顺序**
1. 修正 tool schema 过滤：只暴露可见且可用的工具。
2. 公开 context provider API。
3. 增加 session reset/delete API。
4. 增加 debug trace 和资源占用统计。
5. 强化 openvela HTTP：status code、分段写、TLS verify。
6. 给 LLM backend 增加基础参数和错误解析。
7. 设计 confirmation flow 和 permission domain。

我的判断是：`cAGENT` 的核心分层不用大改，真正要补的是“长期被开发者使用时的契约”。也就是：哪些东西公开、哪些东西可配置、失败时怎么定位、资源不够时怎么知道、危险工具怎么被约束。把这些补起来，它会从一个能跑的 Agent core，变成一个嵌入式开发者愿意长期依赖的 Agent core。