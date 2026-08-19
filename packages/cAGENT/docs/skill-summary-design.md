# cAGENT Skill 渐进披露设计

## 背景

cAGENT 当前默认将所有已注册 Skill 以完整内容注入每次 ReAct 迭代的 system prompt。
当 Skill 数量较少时（2-3 个），每个 Skill 的完整说明不过 200-400 字节，额外消耗对
嵌入式为数 KB 的上下文预算尚可接受。然而当达到 8-10 个 Skill（如 smart_home 搭配
天气、安全、场景、定时器、音乐播放等策略），预注入带来的线性增长会占用过多预算。

嵌入式上下文 buffer 无法随意增大。此时 Skill 应当出现在上下文中告诉 LLM
"这里有这方面的知识"，但默认不必展开完整内容——只有需要时才付出 token 成本。

这就是渐进披露：优先展示摘要，LLM 决定需要后再主动读取完整 Skill 内容。

## 目标

在 `agent_skill_t` 中新增 `AGENT_SKILL_FLAG_SUMMARY_ONLY` 标记，让 Skill 的
context 注入行为变成两级：

```text
SUMMARY_ONLY 未设置 (默认):
  Skill 完整注入，保持现有行为不变。

SUMMARY_ONLY 已设置:
  Skill 以 "摘要" 形式出现在 system prompt 中，仅注入 name + description +
  一小段提示，告诉 LLM 如何获取完整内容。LLM 可通过 read_skill 工具按需读取。
```

非目标：

- 不自动根据 budget 动态切换摘要/全量——交由应用层显式通过 flag 控制。
- 不依赖文件系统——Skill 全部在内存中，read_skill 直接从 registry 返回。
- 不在第一版引入摘要压缩、模型摘要或过期策略。

## API 变更

### Skill 标志位（`include/cagent/skill.h`）

新增一个标志位：

```c
#define AGENT_SKILL_FLAG_ENABLED      (1u << 0) /* 已启用                */
#define AGENT_SKILL_FLAG_LLM_VISIBLE  (1u << 1) /* 对 LLM 可见           */
#define AGENT_SKILL_FLAG_SUMMARY_ONLY (1u << 2) /* 仅注入摘要，不展开全文 */
```

### 注册 Skill 时设定

Skill 注册支持两种方式——完整结构体或便捷参数式，与 Tool 的 `agent_register_tool` / `agent_register_tool_simple` 对齐。

```c
/* 完整结构体方式 — 支持全部字段（group_id / user_data 等） */
const agent_skill_t scene_skill = {
    .name = "smart_home_scenes",
    .description = "Scene mapping for the virtual smart home.",
    .context_text = "Smart home scene policy:\n"
                    "- sleep: turn off living room light, ...",
    .priority = 90u,
    .flags = AGENT_SKILL_FLAG_ENABLED | AGENT_SKILL_FLAG_LLM_VISIBLE
           | AGENT_SKILL_FLAG_SUMMARY_ONLY,
};
agent_register_skill(agent, &scene_skill);

/* 便捷参数式 — 省略 group_id / user_data（设为默认值 0/NULL） */
agent_register_skill_simple(agent,
    "smart_home_scenes",
    "Scene mapping for the virtual smart home.",
    "Smart home scene policy:\n- sleep: turn off living room light, ...",
    90u,
    AGENT_SKILL_FLAG_ENABLED | AGENT_SKILL_FLAG_LLM_VISIBLE
        | AGENT_SKILL_FLAG_SUMMARY_ONLY);
```

### 便捷函数签名

```c
int agent_register_skill_simple(agent_t *agent,
                                const char *name,
                                const char *description,
                                const char *context_text,
                                uint32_t priority,
                                uint32_t flags);
```

与 `agent_register_tool_simple` 对标——核心字段全部展开为参数，`group_id` 和 `user_data`
设为默认值（0 和 NULL），减少样板代码。需要这两个字段时仍使用完整结构体版本。

### 上下文构建行为变化（`src/skills/skill_registry.c`）

`append_skill()` 在处理 Skill 时会检查 `AGENT_SKILL_FLAG_SUMMARY_ONLY`：

```c
static int append_skill(agent_skill_entry_t *entry,
                        char *buffer, size_t buffer_size, size_t *used)
{
    /* name + description 注入不变 */

    uint32_t remaining = (uint32_t)(buffer_size - *used);
    if (entry->def.flags & AGENT_SKILL_FLAG_SUMMARY_ONLY) {
        /* 只注入 name + description + 读取提示 */
        int n = snprintf(buffer + *used, remaining,
            "\n(Full content: call read_skill \"%s\")\n",
            entry->def.name);
        if (n < 0 || (uint32_t)n >= remaining) {
            return AGENT_ERROR_CONTEXT_OVERFLOW;
        }
        *used += (size_t)n;
        return AGENT_OK;
    }

    /* 否则注入 name + description + 完整 Instructions */
    return append_text(buffer, buffer_size, used, entry->def.context_text);
}
```

LLM 在 system prompt 中看到的摘要形式为：

```text
Skill: smart_home_scenes
Description: Scene mapping for the virtual smart home.
(Full content: call read_skill "smart_home_scenes")
```

## SMART_HOME 集成示例

### 区分哪些 Skill 适合摘要

| Skill | SUMMARY_ONLY | 理由 |
|-------|-------------|------|
| `smart_home_safety` | 否 | 安全规则是合规要求，必须每次都完整出现在 prompt 中，不能依赖 LLM 主动读取 |
| `smart_home_scenes` | 是 | 场景映射仅在用户请求场景时需要使用，日常控灯/控空调不涉及 |
| `smart_home_weather` | 是 | 天气策略仅在用户询问天气时使用，绝大多数对话不触发 |

### Skills 注册（`src/skills/smart_home_skills.c`）

```c
/* safety — 全量注入，安全规则始终直接可见 */
skill.flags = AGENT_SKILL_FLAG_ENABLED | AGENT_SKILL_FLAG_LLM_VISIBLE;

/* scenes — 仅摘要 */
skill.flags = AGENT_SKILL_FLAG_ENABLED | AGENT_SKILL_FLAG_LLM_VISIBLE
            | AGENT_SKILL_FLAG_SUMMARY_ONLY;

/* weather — 仅摘要 */
skill.flags = AGENT_SKILL_FLAG_ENABLED | AGENT_SKILL_FLAG_LLM_VISIBLE
            | AGENT_SKILL_FLAG_SUMMARY_ONLY;
```

### `read_skill` 工具注册（`src/tools/smart_home_tools.c`）

LLM 通过调用 `read_skill` 获取摘要标记的 Skill 的完整说明文本：

```c
static int read_skill_execute(const agent_tool_call_t *call,
                              agent_tool_result_t *result,
                              void *user_data)
{
    agent_t *agent = (agent_t *)user_data;
    char name[64];

    parse_skill_name(call->arguments_json, name, sizeof(name));

    const agent_skill_entry_t *entry =
        agent_skill_registry_find_const(agent, name);
    if (!entry) {
        set_tool_error(result, "skill not found");
        return AGENT_ERROR_NOTFOUND;
    }

    result->status = AGENT_OK;
    result->content_json = entry->def.context_text;  /* 返回完整 Skill 文本 */
    return AGENT_OK;
}
```

```c
/* 将 read_skill 注册为 LLM-visible + 只读工具 */
agent_register_tool_simple(agent,
    "read_skill",
    "Read the full instructions of a named skill.",
    "{\"type\":\"object\",\"properties\":{"
    "\"name\":{\"type\":\"string\"}},"
    "\"required\":[\"name\"]}",
    read_skill_execute,
    agent,
    AGENT_TOOL_FLAG_LLM_VISIBLE | AGENT_TOOL_FLAG_READ_ONLY);
```

### 运行时流程

```text
用户: 我要睡觉了

迭代 1:
  model sees:
    safety (full)
    scenes: (Full content: call read_skill)
    weather: (Full content: call read_skill)
  model calls: read_skill("smart_home_scenes")
    → returns full scene policy

迭代 2:
  model now has full scene knowledge
  model calls: run_scene("sleep")
    → device updated

迭代 3:
  model: "已进入睡眠模式，卧室灯调暗至 10%，空调设为 26°C。"
```

一次场景请求额外消耗 1 轮 LLM 迭代，但日常开灯/关灯/调空调不消耗场景和天气的
token 预算。对于 session 有限的嵌入式场景，总体 token 消耗改善约 20-40%。

## 对比分析

| 属性 | 全量预注入（当前） | 摘要式（SUMMARY_ONLY） | 纯懒加载（文件式） |
|------|-------------------|----------------------|-------------------|
| Skill 存储 | 内存 registry | 内存 registry | 文件系统 |
| SMUNAR content 每次注入 | 全部 Skill 完整内容 | 仅 name+description | 仅路径/名称 |
| 额外 LLM 调用 | 无 | 每个摘要 Skill +1 iter | 每个 Skill +1 iter |
| Skill 按需读取方式 | 不需要 | read_skill 工具 | read_file 工具 |
| 适用 Skill 特征 | 少、关键 | 多、领域性、场景性 | 多、文件式、独立加载 |
| 代码改动量 | 基准 | +3 行 flag + ~60 行 read_skill 工具 | 重写技能加载器 |

摘要式是预注入和纯懒加载之间的折中——Skills 继续以内存形式注册在 `agent->skills[]`
中，由 Registry 管理生命周期，不需要文件系统；但注入行为从 "必须全量" 变为 "摘要 +
按需"。安全性或合规性 Skill 可以保持全量，domain-specific Skill 改用摘要。

## 替代方案与取舍

### 自动切换摘要/全量（未采纳）

可以在 `agent_skill_context_build` 中根据总 buffer 剩余空间自动选择哪些 Skill
使用摘要、哪些全量。不采纳的原因：

- 行为难以预测：同一个 Skill 在不同请求中可能有不同的注入行为（取决于前面的
  provider/skill 消耗了多少 buffer），调试困难。
- 优先级语义混淆：priority 控制 Skill 出现顺序（大 priority 先出现），用空间
  阈值再控制时优先级不再是纯顺序。
- 更简单的做法：显式 flag 由应用层在注册时决定，框架不猜测应用意图。

### 按需调用后缓存 Skill 内容（未采纳）

LLM 调用 `read_skill` 后拿到完整 Skill 内容，后续迭代不再需要重复调用。实现上
可以将已读取的 Skill 内容注入当前 session 作为上一个 assistant 消息的 tool
result 文本，LLM 在下一个迭代自然看到。不额外做框架层缓存。

### 支持 Markdown 文件式 Skill（未采纳）

ai_agent 的 `skill_loader.c` 支持从 `/data/agent/skills/*.md` 读取 Skill，
提供热重载。这个特性对嵌入式生产环境有价值，但对 cAGENT MVP 来说：
- 文件式 Skill 可以与内存注册的 Skill 并行存在——两者不冲突。
- 当前 demo 已有内存注册 Skill，后续 loader 作为上层模块时再把文件解析后的
  Skill 通过 `agent_register_skill` 注册——接口不变。

## 代码改动范围

### cAGENT 侧

| 文件 | 改动 |
|------|------|
| `include/cagent/skill.h` | 新增 `AGENT_SKILL_FLAG_SUMMARY_ONLY` 宏 + `agent_register_skill_simple` 函数声明 |
| `src/skills/skill_registry.c` | `append_skill` 中检查 flag 分流 path + `agent_register_skill_simple` 实现 |

无需修改 context builder、无需修改 public API 签名。`agent_register_skill` 保持原样。
便捷函数 `agent_register_skill_simple` 对标 `agent_register_tool_simple`，降低应用代码
的样板行数。

### smart_home 侧

| 文件 | 改动 |
|------|------|
| `src/skills/smart_home_skills.c` | 在 scenes 和 weather 注册标记中加了 `SUMMARY_ONLY` |
| `src/tools/smart_home_tools.c` | 新增 `read_skill` 工具注册 |

无需修改 device 层、UI 层、agent 层。

## 验收标准

- `AGENT_SKILL_FLAG_SUMMARY_ONLY` 标记已声明且值为 1u<<2。
- 未设置此标记的 Skill 仍然完整注入，行为不变。
- 设置此标记的 Skill 仅注入 name + description + 提示文本"(Full content: call read_skill \"name\")"，不注入 `context_text`。
- Skill 数量为 0 时构建仍返回 AGENT_OK。
- `agent_register_skill_simple` 创建 Skill 并以默认 group_id(0) / user_data(NULL) 注册，功能等价于构造完整结构体后调用 `agent_register_skill`。
- smart_home 的 `smart_home_safety` 保持全量注入；
  `smart_home_scenes` 和 `smart_home_weather` 为摘要式。
- 用户说 "我要睡觉了" 后，LLM 能调用 `read_skill` 获取完整
  scenes 信息，再调用 `run_scene`。
- 用户说 "打开客厅灯"（不涉及 scenes 或 weather）时，LLM 不调用
  `read_skill`，额外上下文预算接近零。
