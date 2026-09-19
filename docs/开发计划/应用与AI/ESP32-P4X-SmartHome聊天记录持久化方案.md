# ESP32-P4X SmartHome 聊天记录持久化方案（方案 A：应用层聊天日志）

| 项 | 值 |
| --- | --- |
| 版本 | v1.0 |
| 日期 | 2026-09-19 |
| 范围 | **仅方案 A**：应用层保存"用户可见的对话文本"，重启后聊天页恢复历史显示。方案 B（cAGENT 会话快照、模型上下文恢复）不在本文档，见 §8 边界说明 |
| 权威开发树 | `openvela/contest2026_031_niudanxianqianchong` |
| 前置依赖 | **flash 写挂死问题修复**（见 §7，当前 smart_home 运行态写 `/data` 会挂死系统） |

---

## 1. 目标与验收

**目标**：用户与 Agent 的对话在设备重启后仍出现在聊天页；对话的"屏幕连续性"不因重启中断。

**DoD**：

- [ ] 聊天过程中每条用户输入与助手回复落盘 `/data/smart_home/chat_log.json`（原子写）
- [ ] 重启 `smart_home` 后聊天页自动显示最近 N 条历史（含角色区分气泡样式），并自动滚到底部
- [ ] 记录超限时淘汰最旧消息，文件体积有界（≤ ~12 KiB）
- [ ] 文件损坏/版本不符时静默丢弃并从空记录开始，不阻塞 UI 启动
- [ ] 清空对话入口（聊天页长按或设置页按钮，随本方案一并提供）同步删除文件

**非目标（明确不做）**：

- 不恢复模型上下文——重启后 Agent 仍不"记得"对话语义（方案 B 范畴）
- 不保存工具调用中间消息（role=tool 的过程气泡）——只存用户输入与助手最终回复
- 不做多会话/多用户区分

## 2. 现状与拦截点

消息流现状（`smart_home_lvgl_chat.c`）：

```text
用户输入  chat_send_event_cb / chat_input_event_cb(READY)
            → smart_home_lvgl_chat_send_text(ui, text)      ① 用户侧气泡
            → agent worker（smart_home_agent_run）
助手回复   agent 事件回调 → 聊天页追加助手气泡               ② 助手侧气泡
工具过程   tool_status_text 等过程气泡                       ③ 不持久化
```

拦截点取 ①②：两处已经在 UI 层拿到 `(role, text)` 纯文本，新增一行
`chat_log_append(role, text)` 调用即可，无需触碰 cAGENT。

## 3. 总体设计

```text
┌─ src/ui/lvgl/smart_home_lvgl_chat.c ──────────────┐
│ ①用户发送 / ②助手回复 处调用                        │
│      chat_log_append(role, text)                   │
│  聊天页构建时调用 chat_log_load() 重建历史气泡        │
└──────────────┬─────────────────────────────────────┘
               ▼
┌─ src/config/smart_home_chat_log.c [新增，~220 行] ──┐
│  内存环形缓冲（最近 N=50 条）                        │
│  append: 入缓冲 + 防抖落盘（见 §5）                  │
│  load:   读 JSON → 缓冲 + 返回数组供 UI 重建          │
│  clear:  删文件 + 清缓冲                             │
│  存储: /data/smart_home/chat_log.json（tmp+rename）  │
└─────────────────────────────────────────────────────┘
```

依赖复用：JSON 用现有 `cjson_compat`；原子写模式照抄 `smart_home_secrets.c`
（`%s.tmp` → fwrite/fflush/fclose → rename）；文件上限借用
`CONFIG_SMART_HOME_MODEL_SECRETS_MAX_FILE_SIZE` 的思路，独立定义 16 KiB 上限。

## 4. 数据结构

```json
{
  "version": 1,
  "messages": [
    { "role": "user",      "text": "打开客厅的灯", "ts": 1726700000 },
    { "role": "assistant", "text": "好的，已为你打开。", "ts": 1726700002 }
  ]
}
```

- `role`: `"user" | "assistant"` 两值；其他值读取时丢弃
- `text`: 上限 512 字节（超长截断后入库；现聊天气泡内容均远小于此）
- `ts`: 秒级 Unix 时间戳，仅排序与调试用，UI 不展示
- 单条缺失字段 → 该条丢弃；`version` 不符或 JSON 解析失败 → 整文件作废重写

## 5. 写入策略（关键决策）

| 决策 | 取值 | 理由 |
| --- | --- | --- |
| 触发时机 | **助手回复落定后一次性写**（一次对话 turn = user+assistant 两条一起写） | 避免用户发送瞬间写盘；对话节奏天然防抖（人不可能 1 秒内多 turn）；flash 磨损最小 |
| turn 中断处理 | 用户已发送但助手未回（出错/断网）时，用户消息保留在内存，下个 turn 一并落盘 | 保证文件内 turn 完整，简化读取端 |
| 条数上限 | 50 条（25 turn） | ~12 KiB 以内；LittleFS 磨损可忽略；屏幕回看价值以上足够 |
| 淘汰 | 写前裁掉最旧完整 turn | 与 cAGENT session 的 turn 淘汰语义对齐（虽然本方案不存 tool 消息） |
| 失败处理 | 落盘失败仅串口告警一次，不影响 UI | 持久化是增强能力，绝不阻塞聊天主流程 |

## 6. 文件清单

**新增（2）**

| 文件 | 规模 |
| --- | --- |
| `demos/smart_home/src/config/smart_home_chat_log.c/.h` | ~220 行 |
| 本方案文档 | — |

**修改（3）**

| 文件 | 改动 |
| --- | --- |
| `src/ui/lvgl/smart_home_lvgl_chat.c` | 构建时 `chat_log_load()` 重建气泡；①②拦截点 `chat_log_append()`；助手回复路径补写 |
| `src/agent/smart_home_agent.c` 或事件回调所在文件 | 若助手回复文本仅在 agent 层可见，则在回调转发处加一行 append（实现时按实际链路定，原则：拦截点唯一） |
| `demos/smart_home/Makefile` + `CMakeLists.txt` | 新源文件 |

不新增 Kconfig 开关：功能轻量、无第三方依赖，随 `SMART_HOME_DEMO` 常编。

## 7. 前置依赖：flash 写挂死（当前阻塞）

已证实的事实链（2026-09-19 真机）：

- nsh 空闲态 `echo > /data/t.txt` **正常**；
- smart_home 运行态写 `/data/smart_home/secrets.json.tmp` **必挂死系统**（LVGL 线程与 worker 线程均复现，与线程无关）；
- 栈扩容 24 KiB 无效，排除栈溢出；堆在挂点前仍健康。

本方案的每次落盘都在 smart_home 运行态发生——**必须先定位并修复该问题**（当前
排查方向：flash 擦除与 DSI 扫描/PSRAM 访问的互斥，判决实验"后台运行
smart_home 时 nsh 写文件"待执行，见对应排障记录）。

修复后本方案的写入上下文首选**同步写**（若修复后任意上下文安全）；若修复
结论限定特定上下文才安全，则落盘改走该上下文（如复用 miloco worker 模式
的独立写线程），模块接口不变。

## 8. 与方案 B 的边界及演进

方案 B（cAGENT session snapshot/restore）恢复的是**模型上下文**：需给
cAGENT 补消息导出/导入 API，且必须保证 tool calling 链
（user→assistant(tool_calls)→tool→assistant）完整 round-trip，复杂度
~1.5 天。演进路径：

1. 本方案 A 的 JSON 是 B 的子集（不含 role=tool/tool_call_id）；
2. B 落地时文件升级为 `chat_session.json`（version 2，含完整链），
   A 的加载端按 version 分支兼容；
3. UI 重建逻辑完全复用。

## 9. 测试计划

| 用例 | 步骤 | 预期 |
| --- | --- | --- |
| 基本持久化 | 对话 2 turn → 重启 → 进聊天页 | 历史气泡按角色恢复，滚动到底 |
| 条数淘汰 | 连续对话 >50 条 → 查看文件 | 最旧 turn 被裁，文件 ≤ 上限 |
| 断电安全 | 写入瞬间复位（对齐 rename 时序） | 要么旧文件要么新文件，无半文件；损坏时静默重来 |
| 损坏容错 | 手工写入非法 JSON 到 chat_log.json → 启动 | UI 正常空历史，文件被重写 |
| 清空入口 | 触发清空 → 重启 | 无历史，文件不存在 |
| 磨损估算 | 100 turn/天 | ~200 KiB/天写入，LittleFS 10 MB 分区量级无压力 |

## 10. 里程碑

| 里程碑 | 内容 | 工期 | 出口标准 |
| --- | --- | --- | --- |
| M0 | flash 写挂死修复验收（外部依赖） | — | smart_home 运行态写 /data 稳定 |
| M1 | chat_log 模块 + host 侧单测（构造/裁剪/损坏容错） | 0.5 天 | 单测过 |
| M2 | 聊天页接线 + 真机验收 | 0.5 天 | §1 DoD 全过 |

## 11. 风险

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| flash 写挂死修复周期不可控 | M2 无法验收 | M1（模块+单测）先行；拦截点已就绪，闸门开后一天内上线 |
| 助手回复拦截点在 agent 层而非 UI 层 | 接线位置变化 | 实现时先实测消息链路，原则：拦截点唯一、只取纯文本 |
| 长文本/emoji 截断 | 显示与存档不一致 | 截断发生在入库前，UI 气泡用入库后文本 |
