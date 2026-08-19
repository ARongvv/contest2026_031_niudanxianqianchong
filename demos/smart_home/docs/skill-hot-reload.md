# smart_home Skill 热加载与设备端扩展方案

> 状态：设计待实施（2026-08-05 已按当前 loader/registry API 复核）。
> 范围：smart_home 应用层技能（Skill）的运行时管理。与复赛任务 B/C 无耦合，
> 服务于任务 A（技能文件编写）的完整叙事："无需修改 C 代码、无需重启即可
> 扩展 Agent 行为"。

## 1. 背景与目标

smart_home 的技能机制是 cAGENT 的 Markdown Skill：每个 `.md` 文件经 front
matter（name/description）+ 正文描述一组领域策略（设备控制、安全规则、
场景、定时器、天气感知）。当前 5 个技能文件位于 `res/skills/`。

目标：

1. **热加载**：不重启设备，使新增/修改的 skill 文件生效；
2. **宿主编写闭环**：主机编辑 → 推送设备 → 热加载，分钟级迭代；
3. 明确设备端编辑与 LLM 自写 skill 的边界（本期不做，见 §6/§7）。

## 2. 现状分析

### 2.1 加载链路（已核实）

```text
agent 装配（smart_home_agent.c:120）
  → smart_home_skills_register()
  → load_skills_from_default_paths()        (skills/smart_home_skills.c:15)
  → smart_home_skill_loader_load_dir()      (skills/smart_home_skill_loader.c:339)
      扫描 /data/res/skills/*.md（fallback: /data/res/res/skills）
      解析 front matter → agent_register_skill()
```

加载仅在装配时执行一次；代码中不存在第二次调用 `load_dir` 的路径，
新 skill 当前必须重启生效。

### 2.2 已具备的运行时能力

- cAGENT 公共 API：`agent_register_skill()` / `agent_unregister_skill()`
  （`packages/cAGENT/include/cagent/skill.h:47,74`），运行时可动态增删；
- `SUMMARY_ONLY` flag + `read_skill` 工具：摘要常驻 context、全文按需读取，
  热加 skill 天然遵循同一预算模型；
- `/data` 为 LittleFS 可写分区，skill 文件是纯数据，增改不涉及重新编译。

### 2.3 文件进设备的现有路径

| 平台 | 路径 |
|------|------|
| QEMU goldfish | `adb push <file>.md /data/res/skills/` |
| ESP32-S3-BOX-3 | 主机打包 LittleFS 镜像烧录 0xE00000（全量更新，不适合迭代） |

### 2.4 缺口

1. 无热加载触发器（NSH 命令 / LVGL 按钮 / 文件监视均不存在）；
2. BOX-3 缺少单文件上传通道（无 ADB/FTP，整区烧录迭代成本高）；
3. 设备端无文本编辑能力（NSH 无编辑器）；
4. LVGL 设置页无技能管理视图（仅有工具开关）。

## 3. 能力分层结论

| 层次 | 能力 | 本期裁决 |
|------|------|----------|
| L1 | 热加载（reload 触发器） | **实现**，需要 staging + 串行提交 |
| L2 | 宿主编写 + push + reload 闭环 | **实现**（依赖 L1，QEMU 路径已通） |
| L3 | 设备端直接编辑 skill | **不做**（无编辑器/上传通道，投入产出比低） |
| L4 | LLM 自写新 skill（write_skill 工具） | **不做**，安全敏感，见 §6 |

## 4. 热加载设计

### 4.1 触发器

主触发器为 NSH 子命令（Console 与脚本/演示均可驱动）：

```text
nsh> smart_home skill reload     # 重新扫描并注册全部 skill
nsh> smart_home skill list       # 列出已加载 skill（名称/摘要长度/来源路径）
```

LVGL 设置页后续可加"重载技能"按钮，调用同一函数（非本期必须）。

### 4.2 reload 流程

```text
smart_home skill reload
  1. 将 reload 任务投递到与 agent_run 相同的 smart_home 单 worker
  2. 扫描目录，只解析到 staging store，不修改 cAGENT registry
  3. 预检查 name 唯一性、CAGENT_MAX_SKILLS 和 system-context 预算
  4. 保留 old store，注销旧 skill，再注册 staging 中的有效 skill
  5. 提交成功后 swap store 并释放 old store；失败则回滚注册旧 store
  6. 汇报结果：加载 N 个 skill，跳过 M 个（文件名 + 原因）
```

约束与边界：

- **并发**：cAGENT 只保证同一 agent 不可重入，但没有公共 busy 查询
  API。reload 不从 NSH/LVGL 线程直接动 registry，而是与 ReAct 一起排入
  smart_home worker；当前 run 完成后再执行，不强制打断；
- **容量**：`CAGENT_MAX_SKILLS=8`（ReAct 档），当前用 5，余量 3。
  超出上限的文件跳过并逐个日志告警，不静默丢弃；
- **context 预算**：新增 skill 的摘要注入 system context，
  遵循既有 `SUMMARY_ONLY` + `read_skill` 按需读全文模型，
  不允许摘要总长挤爆 `CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE`；
- **提交原子性**：现有 `smart_home_skill_loader_load_dir(agent, store, path)`
  边解析边注册，不能直接用于安全 reload。实施时必须拆为“解析到
  staging”和“注册 staging”两步。单文件解析失败只会从新候选集跳过；
  registry 提交失败则恢复整个 old store，不留半套新注册表。由于提交期间
  worker 不运行 ReAct，外部不会观察到逐项 unregister/register 的中间状态。

### 4.3 skill 文件格式约定（新增 skill 必须遵守）

```markdown
---
name: my_new_skill
description: 一句话说明，会进入系统上下文，须精简
---

正文：策略与规则，Markdown 自由格式。
通过 read_skill 工具按需读取，不写进常驻上下文。
```

- `name` 唯一，重复时后加载者拒绝并告警；
- `description` 建议 ≤ 80 字符（常驻 context 成本）；
- 正文只陈述策略与知识，**不能授予或绕过权限**（见 §6）。

## 5. 宿主编写闭环（L2）

QEMU 开发迭代路径（reload 命令落地后即可用）：

```bash
# 1. 主机编辑或新建 res/skills/my_skill.md
adb push my_skill.md /data/res/skills/
# 2. NSH 执行 smart_home skill reload，立即生效
# 3. 对话验证新行为；不满意回到 1
```

BOX-3 迭代路径（本期可接受的降级方案）：开发期改用 QEMU 验证 skill，
定稿后随 LittleFS 镜像整体烧录。单文件上传通道（如 NSH 下 `get`/
最小 TFTP）列为可选后续项，不阻塞本期。

## 6. 安全边界

1. skill 正文对模型是指令性文本，但对系统是**数据**：skill 不能修改
   工具开关、policy、allowlist、模型配置或系统提示词的强制规则。
   真实权限仍在 cAGENT guard + smart_home policy 闭合（与
   `mcp-extension-plan.md` §8 信任边界一致）；
2. **L4（LLM 自写 skill）本期不做**：`write_skill` 类工具意味着模型输出
   可转化为长期行为指令，prompt injection 可借此改写 agent 后续行为。
   若未来引入，必须挂在安全链上：`AGENT_TOOL_FLAG_REQUIRES_CONFIRM`
   + policy 白名单 + LVGL 用户确认，缺一不可；
3. reload 不重载工具，不影响 tool guard 状态；skill 文件解析失败
   （缺 front matter、name 缺失）按数据错误处理，不中断系统。

## 7. 非目标

1. 不做设备端文本编辑器、不做 LVGL skill 编辑界面；
2. 不做 `write_skill` / `delete_skill` 等 LLM 可写的管理工具；
3. 不做 skill 的远程分发/版本管理（OTA 通道属于产品化议题）；
4. 不改动 cAGENT core（全部使用现有公共 API）。

## 8. 验收用例

| 用例 | 预期 |
|------|------|
| 新增 skill + reload | 不重启，`skill list` 出现新条目，对话行为体现新策略 |
| 修改已有 skill + reload | 新正文生效（通过 read_skill 验证全文已更新） |
| skill 超过 8 个 | 超出部分跳过并告警，前 8 个正常 |
| 文件缺 front matter | 该文件报错跳过，其余正常加载 |
| ReAct 进行中触发 reload | 请求排队，当前对话完成后再 reload，不打断对话 |
| registry 提交中一项失败 | 恢复 old store，无新旧 skill 混合状态 |
| name 重复 | 后者拒绝并告警，前者不受影响 |

## 9. 实施步骤

1. `smart_home_main.c`：NSH 子命令解析（`skill reload` / `skill list`）；
2. `skills/smart_home_skill_loader.c/.h`：拆分 parse/stage 与 register，使文件
   解析期间不修改 agent registry；
3. `skills/smart_home_skills.c/.h`：实现 `smart_home_skills_reload()`，包含
   预检查、串行提交、store swap 和失败回滚，供 NSH 与未来 LVGL 复用；
4. 在 smart_home worker 增加 reload job，与 `agent_run()` 串行化；
5. QEMU 闭环验证（§5 流程）+ §8 验收用例；
6. README/复赛增量文档补"L2 宿主编写闭环"操作说明。

预计实现量：约 200–300 行 C（不含测试与文档）；主要成本在 staging、
registry 回滚和 worker 串行化，不应再按“几十行 reload 命令”估算。
