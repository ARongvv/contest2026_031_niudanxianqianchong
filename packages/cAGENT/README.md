# cAGENT

cAGENT 是一个面向嵌入式平台的 C Agent 核心库。核心提供同步
`agent_run_simple()`、模型 provider 抽象、工具注册、session 管理和平台
runtime 抽象；网络、TLS、时间、日志和锁由具体平台 runtime 提供。

## ai_agent与cAGENT比较
| 维度 | openvela `packages/ai_agent` | `openvela_smarthome/packages/cAGENT` |
|---|---|---|
| 定位 | 完整 Agent 框架/应用能力包 | 可复用的轻量 Agent 核心库 |
| 面向对象 | openvela 上的通用 AI Agent，覆盖多入口、多工具、多模态 | smart_home 这类受限嵌入式应用，把 Agent 编排能力抽成小核心 |
| 功能范围 | LLM 后端、ReAct、35+ 工具、多通道、MCP、Markdown Skills、语音/视觉/节点协作等 | ReAct loop、tool registry/schema、session/context、skills context、model provider、event callback、runtime abstraction |
| 接入通道 | CLI、飞书、微信、WebSocket、MQTT、Voice、Node/OpenClaw、MCP Client 等 | 本身不内置这些通道，交给应用层或平台适配层 |
| 工具体系 | 内置大量通用工具：文件、搜索、shell、相机、传感器、天气、MCP 等 | 只提供工具注册和 schema 生成机制；智能家居设备控制等工具由 `smart_home` 注入 |
| 资源模型 | 通过 Kconfig 开关裁剪模块，README 中给出多模块 RAM 估算，最小 CLI-only 可压缩 | 通过 Tiny/Default/ReAct profile 和细粒度 buffer/session/tool 上限控制资源 |
| 平台绑定 | 更深地面向 openvela/NuttX，包含完整 infra/channels/tools/ui/voice/node 结构 | core 尽量平台无关，openvela 只是 runtime adapter 之一 |
| 与 smart_home 的关系 | 可作为上游参考框架 | 是 smart_home 实际使用的 Agent core |
| 法务表述 | openvela 包内组件，README 说明其基于 MimiClaw 派生/扩展 | 项目内自研组件；应在 NOTICE/README 说明“参考 openvela `packages/ai_agent`”，不应列为第三方依赖 |
| 最合适的描述 | “完整 Agent 系统” | “轻量 C Agent runtime/core” |


## 最小聊天接入

下面是最小 OpenAI-compatible 聊天接入路径，适合先验证模型和平台 runtime 是否
打通。这个例子不注册工具，也不展示 ReAct tool calling。

```c
#include <agent.h>

#include <stdio.h>
#include <stdlib.h>

#define DEMO_HOST "api.deepseek.com"
#define DEMO_API_KEY "sk-REPLACE_WITH_YOUR_KEY"
#define DEMO_MODEL "deepseek-chat"

int main(int argc, char *argv[])
{
    const char *input = argc > 1 ? argv[1] : "Hello!";
    char output[4096];
    agent_t *agent;
    int ret;

    agent = agent_create_simple("demo", "You are a helpful assistant.");
    if (!agent) {
        return EXIT_FAILURE;
    }

    ret = agent_attach_openai(agent, DEMO_HOST, DEMO_API_KEY, DEMO_MODEL);
    if (ret != AGENT_OK) {
        agent_destroy(agent);
        return EXIT_FAILURE;
    }

    ret = agent_run_simple(agent, input, output, sizeof(output));
    if (ret == AGENT_OK) {
        printf("assistant: %s\n", output);
    } else {
        printf("ERROR (%d): %s\n", ret, output[0] ? output : "no detail");
    }

    agent_destroy(agent);
    return ret == AGENT_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

## openvela Demo

仓库里提供了一个同样风格的最小聊天 demo：

```text
packages/demos/cagent_demo
```

修改 `packages/demos/cagent_demo/src/cagent_demo_main.c` 顶部的后端配置：

```c
#define CAGENT_DEMO_HOST "api.deepseek.com"
#define CAGENT_DEMO_API_KEY "sk-REPLACE_WITH_YOUR_KEY"
#define CAGENT_DEMO_MODEL "deepseek-chat"
```

在 NSH 中运行：

```sh
ifup eth0
renew eth0
cagent_demo
cagent_demo "Hello"
```

## Runtime 选择

cAGENT 通过 Kconfig 选择默认平台 runtime。对 openvela demo，打开：

```text
CONFIG_CAGENT_DEMO=y
CONFIG_CAGENT_RUNTIME_OPENVELA=y
CONFIG_CAGENT_RUNTIME_OPENVELA_TLS=y
```

启用后，`agent_create_simple()` 会自动填充 openvela runtime callback，包括
mbedTLS HTTPS `http_post`。应用仍然可以通过 `agent_config_t.runtime` 手动注入
自己的 callback，已设置的 callback 不会被平台 runtime 覆盖。

## 下一步

最小聊天 demo 只验证模型接入。工具调用接入可以运行真实模型 demo：

```text
packages/demos/agent_tools_demo
```

它和 `cagent_demo` 一样接入 OpenAI-compatible API，并注册 `echo`、
`get_time`、`calc` 三个本地工具，用于展示：

```text
user input -> model tool_call -> local tool -> tool result -> final answer
```

工具接入从 `agent_register_tool_simple()` 开始。
