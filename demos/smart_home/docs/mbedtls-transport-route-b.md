# smart_home 路线 B：HTTP transport 解耦 mbedTLS 实现方案

## 1. 背景与目标

ESP32-S3-BOX-3 上启用 smart_home 在线 LLM 时，当前构建会同时牵出两套
mbedTLS：

```text
CONFIG_ESP32S3_WIFI=y
  -> nuttx/arch/xtensa/src/esp32s3/Wireless.mk
  -> ESP-IDF HAL 自带 mbedTLS library + esp_config.h

CONFIG_CAGENT_RUNTIME_OPENVELA_TLS=y
  -> CONFIG_CRYPTO_MBEDTLS=y
  -> apps/crypto/mbedtls
```

这会导致 ESP HAL 侧 mbedTLS 源文件和 apps 侧 mbedTLS 头文件、配置宏、私有字段
访问规则互相混用，典型报错包括：

```text
cipher.c: error: 'mbedtls_cipher_info_t' has no member named 'base_idx'
pk.c: error: conflicting types for 'esp_mbedtls_pk_get_bitlen'
```

路线 B 的目标是：不再让 smart_home 在线模型强依赖
`CONFIG_CAGENT_RUNTIME_OPENVELA_TLS`，而是通过 cAGENT 已有的
`agent_runtime_t.http_post` 回调，把 HTTPS/HTTP 传输实现下沉到 smart_home
应用层或板级适配层。

最终结构：

```text
smart_home agent app
  -> cAGENT OpenAI-compatible model provider
  -> agent_runtime_t.http_post
  -> smart_home transport
      -> 阶段 1：HTTP 网关
      -> 阶段 2：BOX-3 专用 HTTPS transport
```

## 2. 设计原则

| 原则 | 说明 |
|------|------|
| cAGENT core 不绑定 TLS 实现 | cAGENT 继续只依赖 `agent_runtime_t.http_post`，不直接 include ESP HAL 或 apps mbedTLS 细节 |
| smart_home 控制 transport | smart_home 根据板级能力选择 mock、HTTP 网关或 HTTPS transport |
| 第一阶段避免双 mbedTLS | BOX-3 首个在线闭环不启用 `CONFIG_CAGENT_RUNTIME_OPENVELA_TLS` |
| 保留云 LLM 语义 | OpenAI-compatible provider、tools、skills、session、ReAct 流程保持不变 |
| 后续可替换实现 | HTTP 网关只是 bring-up 手段，接口要能平滑替换为直连 HTTPS |

## 3. 当前代码基础

cAGENT 已有运行时抽象：

```c
typedef struct {
    ...
    int (*http_post)(const agent_http_request_t *request,
                     agent_http_response_t *response,
                     void *user_data);
    ...
} agent_runtime_t;
```

`agent_create()` 会先调用 `agent_runtime_fill_platform()`，再调用
`agent_runtime_fill_defaults()`，并且平台填充函数遵守“已设置的回调不覆盖”规则。

因此应用层可以这样注入自定义 HTTP transport：

```c
agent_config_t cfg = agent_config_default();

cfg.name = "smart_home_agent";
cfg.system_prompt = g_system_prompt;
cfg.runtime.http_post = smart_home_transport_http_post;
cfg.runtime.user_data = &app->transport;

app->agent = agent_create(&cfg);
```

这里的关键变化是：smart_home 不再用 `agent_create_simple()`，而是显式创建
`agent_config_t` 并注入 `http_post`。

## 4. 总体实现分阶段

### 阶段 0：配置拆分

先把 smart_home 的模型与 transport 配置拆清楚：

```text
SMART_HOME_DEMO
  -> CAGENT
  -> CAGENT_MODEL_OPENAI
  -> CAGENT_RUNTIME_OPENVELA_DEFAULTS

SMART_HOME_DEMO_TRANSPORT_GATEWAY
  -> 不 select CAGENT_RUNTIME_OPENVELA_TLS
  -> 不 select CRYPTO_MBEDTLS

SMART_HOME_DEMO_TRANSPORT_OPENVELA_TLS
  -> select CAGENT_RUNTIME_OPENVELA_TLS
  -> 用现有 cAGENT openvela TLS，主要给模拟器或非 ESP32-S3 Wi-Fi 冲突场景使用

SMART_HOME_DEMO_TRANSPORT_BOX3_HTTPS
  -> 后续实现
  -> 使用 ESP HAL 或板级可用 TLS，不拉 apps/crypto/mbedtls
```

建议 Kconfig 形态：

```kconfig
choice SMART_HOME_DEMO_TRANSPORT
    prompt "Smart home LLM transport"
    default SMART_HOME_DEMO_TRANSPORT_GATEWAY

config SMART_HOME_DEMO_TRANSPORT_GATEWAY
    bool "HTTP gateway transport"

config SMART_HOME_DEMO_TRANSPORT_OPENVELA_TLS
    bool "cAGENT openvela mbedTLS transport"
    select CAGENT_RUNTIME_OPENVELA_TLS

config SMART_HOME_DEMO_TRANSPORT_BOX3_HTTPS
    bool "ESP32-S3-BOX-3 HTTPS transport"
    depends on ESP32S3_WIFI

endchoice
```

BOX-3 的 `smart_home/defconfig` 第一阶段应使用：

```text
CONFIG_SMART_HOME_DEMO=y
CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y
CONFIG_SMART_HOME_DEMO_TRANSPORT_GATEWAY=y
CONFIG_CAGENT_MODEL_OPENAI=y
# CONFIG_CAGENT_RUNTIME_OPENVELA_TLS is not set
# CONFIG_CRYPTO_MBEDTLS is not set
```

注意：如果 Wi-Fi 的 WPA 逻辑自身需要 ESP HAL mbedTLS，它仍然会在 arch 侧编译；
关键是不要再额外启用 apps 侧 `CRYPTO_MBEDTLS`。

### 阶段 1：HTTP 网关 transport

HTTP 网关用于先跑通真实云模型和板端工具调用：

```text
ESP32-S3-BOX-3
  -> HTTP POST http://<gateway-ip>:<port>/v1/chat/completions
  -> PC / LAN gateway
  -> HTTPS POST https://api.deepseek.com/v1/chat/completions
  -> cloud LLM
```

#### 1.1 新增文件

```text
demos/smart_home/src/transport/smart_home_transport.h
demos/smart_home/src/transport/smart_home_transport_gateway.c
```

`smart_home_transport.h`：

```c
#pragma once

#include <agent.h>

typedef struct {
    char gateway_host[64];
    char gateway_port[8];
    uint32_t timeout_ms;
} smart_home_transport_t;

void smart_home_transport_init(smart_home_transport_t *transport);

int smart_home_transport_http_post(const agent_http_request_t *request,
                                   agent_http_response_t *response,
                                   void *user_data);
```

#### 1.2 请求映射

cAGENT OpenAI provider 仍然认为自己在访问云端：

```text
request->host = api.deepseek.com
request->path = /v1/chat/completions
request->port = 443
request->headers = Authorization + Content-Type
request->body = OpenAI-compatible JSON
```

gateway transport 实际发往局域网网关：

```text
POST /v1/chat/completions HTTP/1.1
Host: <gateway-ip>
X-Target-Host: api.deepseek.com
X-Target-Path: /v1/chat/completions
X-Target-Port: 443
Authorization: Bearer <key>
Content-Type: application/json
Content-Length: ...
```

网关收到后负责 HTTPS 转发。这样 provider 不需要知道网关存在，工具调用 JSON
解析也不受影响。

#### 1.3 网关 transport 行为

`smart_home_transport_gateway.c` 使用 NuttX socket 实现普通 HTTP/1.1：

1. `getaddrinfo(gateway_host, gateway_port)`。
2. `socket()` / `connect()`。
3. 设置 `SO_RCVTIMEO` / `SO_SNDTIMEO`。
4. 写 HTTP header 和 body。
5. 读取响应头，解析 status code。
6. 读取响应 body 到 `agent_http_response_t.body`。
7. 支持 `Content-Length`；第一版可不支持 chunked，网关统一返回定长 body。

返回规则：

| 场景 | 返回 |
|------|------|
| socket/connect 失败 | `AGENT_ERROR_NETWORK` |
| header/body 超过 buffer | `AGENT_ERROR_LIMIT` |
| HTTP 状态码非 2xx | `AGENT_OK`，但 `response->status_code` 保留真实状态码，由 provider 处理 |
| 解析 HTTP 状态失败 | `AGENT_ERROR_PARSE` |

#### 1.4 网关服务约定

PC 侧网关建议先用 Python/FastAPI 或 Node.js 实现，接口保持非常薄：

```text
POST /v1/chat/completions
headers:
  X-Target-Host: api.deepseek.com
  X-Target-Path: /v1/chat/completions
  X-Target-Port: 443
  Authorization: Bearer ...
body:
  OpenAI-compatible request JSON
```

网关只做三件事：

1. 组装 `https://X-Target-Host:X-Target-Port + X-Target-Path`。
2. 转发 `Authorization`、`Content-Type` 和 body。
3. 将云端响应 body 原样返回给板端，并设置 `Content-Length`。

安全要求：

- 网关只监听局域网或 localhost 反向代理，不暴露公网。
- 不打印完整 `Authorization`。
- 只允许白名单 target host，例如 `api.deepseek.com`、`api.openai.com`、
  `api.moonshot.cn`、`dashscope.aliyuncs.com`。
- 返回体不要改写，避免破坏 provider JSON 解析。

### 阶段 2：smart_home 注入 runtime

`smart_home_agent_app_t` 增加 transport 状态：

```c
typedef struct {
    agent_t *agent;
    smart_home_state_t device_state;
    smart_home_skill_store_t skill_store;
    smart_home_model_config_t model_config;
    smart_home_transport_t transport;
    uint64_t run_start_ms;
} smart_home_agent_app_t;
```

`smart_home_agent_app_init()` 改为：

```c
agent_config_t cfg;

...
smart_home_transport_init(&app->transport);

cfg = agent_config_default();
cfg.name = "smart_home_agent";
cfg.system_prompt = g_system_prompt;

#ifdef CONFIG_SMART_HOME_DEMO_TRANSPORT_GATEWAY
cfg.runtime.http_post = smart_home_transport_http_post;
cfg.runtime.user_data = &app->transport;
#endif

app->agent = agent_create(&cfg);
```

注意：

- `agent_runtime_fill_platform()` 仍会补 openvela 的 malloc、log、mutex 等回调。
- 因为 `http_post` 已经由 smart_home 设置，openvela runtime 不会覆盖它。
- 如果未启用 gateway transport，则走平台默认 transport 或 fallback。

### 阶段 3：配置入口

新增 Kconfig：

```kconfig
config SMART_HOME_DEMO_GATEWAY_HOST
    string "Smart home HTTP gateway host"
    default "192.168.1.100"
    depends on SMART_HOME_DEMO_TRANSPORT_GATEWAY

config SMART_HOME_DEMO_GATEWAY_PORT
    string "Smart home HTTP gateway port"
    default "8080"
    depends on SMART_HOME_DEMO_TRANSPORT_GATEWAY
```

`smart_home_transport_init()` 使用 Kconfig 默认值：

```c
void smart_home_transport_init(smart_home_transport_t *transport)
{
    memset(transport, 0, sizeof(*transport));
    strncpy(transport->gateway_host,
            CONFIG_SMART_HOME_DEMO_GATEWAY_HOST,
            sizeof(transport->gateway_host) - 1);
    strncpy(transport->gateway_port,
            CONFIG_SMART_HOME_DEMO_GATEWAY_PORT,
            sizeof(transport->gateway_port) - 1);
    transport->timeout_ms = 30000;
}
```

后续如果接入 `/data/res/config.json`，运行时配置优先级建议为：

```text
Kconfig 默认值
  < /data/res/config.json
  < UI 设置页临时修改
```

## 5. Make/CMake 接入

Makefile：

```make
ifneq ($(CONFIG_SMART_HOME_DEMO_TRANSPORT_GATEWAY),)
  CSRCS += src/transport/smart_home_transport_gateway.c
endif

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/src/transport
```

CMakeLists.txt：

```cmake
if(CONFIG_SMART_HOME_DEMO_TRANSPORT_GATEWAY)
  list(APPEND SMART_HOME_SRCS
    src/transport/smart_home_transport_gateway.c
  )
endif()
```

虽然 BOX-3 当前优先走 Make 路径，CMakeLists 仍同步维护，避免 demo 在模拟器
或其他板级上回退。

## 6. defconfig 策略

建议保留两个配置目标：

```text
configs/smart_home/
  默认 BOX-3 bring-up 配置
  使用 HTTP gateway transport
  不启用 CAGENT_RUNTIME_OPENVELA_TLS

configs/smart_home_tls/
  实验配置
  启用 CAGENT_RUNTIME_OPENVELA_TLS
  用于模拟器或非 ESP32-S3 Wi-Fi 冲突环境
```

BOX-3 第一阶段验证命令：

```bash
cd openvela
source build/envsetup.sh

./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)
```

配置检查：

```bash
rg -n "SMART_HOME_DEMO|CAGENT_RUNTIME_OPENVELA_TLS|CRYPTO_MBEDTLS|ESP32S3_WIFI" \
  nuttx/.config
```

期望：

```text
CONFIG_SMART_HOME_DEMO=y
CONFIG_SMART_HOME_DEMO_TRANSPORT_GATEWAY=y
CONFIG_ESP32S3_WIFI=y
# CONFIG_CAGENT_RUNTIME_OPENVELA_TLS is not set
# CONFIG_CRYPTO_MBEDTLS is not set
```

## 7. 板端运行流程

1. 烧录 BOX-3 固件。
2. 配置 Wi-Fi：

```nsh
wapi psk wlan0 <ssid> 3 <password>
wapi essid wlan0 <ssid> 1
wapi ifup wlan0
renew wlan0
```

3. 确认能访问网关：

```nsh
ping <gateway-ip>
```

4. 启动 smart_home：

```nsh
smart_home "把客厅灯打开，并告诉我当前状态"
```

5. 期望行为：

```text
smart_home
  -> OpenAI-compatible provider 构造请求
  -> gateway transport 发 HTTP 到 PC 网关
  -> 网关 HTTPS 转发到云 LLM
  -> 模型返回 tool_calls
  -> cAGENT 执行 smart_home tools
  -> 模型给出最终回复
  -> Console 输出结果
```

## 8. 阶段 2：BOX-3 直连 HTTPS transport

HTTP 网关跑通后，再实现：

```text
src/transport/smart_home_transport_box3_https.c
```

目标是不启用 apps 侧 `CONFIG_CRYPTO_MBEDTLS`，而是复用 ESP32-S3 Wi-Fi/HAL
已经需要的 TLS/crypto 能力。

这个阶段要先确认可用 API 形态：

| 方向 | 需要确认 |
|------|----------|
| ESP HAL mbedTLS 符号 | 是否可以在应用层稳定 include ESP HAL mbedTLS 头文件 |
| 符号前缀 | `mbedtls_*` 与 `esp_mbedtls_*` 的映射关系 |
| include 顺序 | 应用层是否能避免包含 apps/crypto/mbedtls 头文件 |
| CA 校验 | 是否有证书 bundle、设备时间、SNI 支持 |
| 链接边界 | 是否会和 arch 侧静态库重复定义 |

如果这些条件不稳定，不要强行把 ESP HAL mbedTLS 暴露给应用层。可以考虑把
直连 HTTPS transport 放到 board/arch 侧，给 smart_home 只暴露纯 C 回调。

推荐边界：

```text
smart_home_transport_box3_https.c
  -> 只 include smart_home_transport.h 和一个板级 wrapper 头

board/esp32s3-box-3/src/box3_https_client.c
  -> include ESP HAL / mbedTLS 细节
  -> 提供 box3_https_post(...)
```

这样即便后续 ESP HAL 更新，影响也限制在板级 wrapper。

## 9. 错误处理与日志

transport 层日志要脱敏：

| 内容 | 日志策略 |
|------|----------|
| host/path/port | 可以打印 |
| HTTP status | 可以打印 |
| API key | 不打印；最多打印是否存在 |
| request body | 默认不打印；debug 时截断并过滤 Authorization |
| response body | 非 2xx 时最多打印前 160 字节 |

错误码建议：

| 错误 | 返回 |
|------|------|
| DNS 失败 | `AGENT_ERROR_NETWORK` |
| connect 超时 | `AGENT_ERROR_NETWORK` |
| 写 socket 失败 | `AGENT_ERROR_NETWORK` |
| 响应头过大 | `AGENT_ERROR_LIMIT` |
| body buffer 不足 | `AGENT_ERROR_LIMIT` |
| HTTP 格式错误 | `AGENT_ERROR_PARSE` |
| 未配置 gateway | `AGENT_ERROR_INVALID` |

## 10. 验证矩阵

| 场景 | 期望 |
|------|------|
| 模拟器 + mock | smart_home 骨架继续可运行 |
| 模拟器 + openvela TLS | 作为回归场景，确认原 cAGENT TLS 未被破坏 |
| BOX-3 + gateway transport | 编译不拉 apps/crypto/mbedtls，能访问云 LLM |
| BOX-3 + gateway 断开 | smart_home 返回网络错误，不崩溃 |
| BOX-3 + 错误 API key | 返回云端 401/403 摘要 |
| BOX-3 + tool call | 能执行设备状态查询和开关控制 |
| BOX-3 + 多轮会话 | session 里保留上下文，不重复初始化 transport |

构建检查：

```bash
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)
```

符号/配置检查：

```bash
rg -n "CRYPTO_MBEDTLS|CAGENT_RUNTIME_OPENVELA_TLS|SMART_HOME_DEMO_TRANSPORT" nuttx/.config
```

运行检查：

```nsh
smart_home "查询所有设备状态"
smart_home "打开客厅灯"
smart_home "把空调调到 26 度"
```

## 11. 推荐提交顺序

1. **Kconfig 拆分**
   - 新增 `SMART_HOME_DEMO_TRANSPORT_*` choice。
   - gateway 为 BOX-3 默认 transport。
   - `OPENVELA_TLS` 分支才 select `CAGENT_RUNTIME_OPENVELA_TLS`。

2. **runtime 注入**
   - `smart_home_agent_app_t` 增加 `smart_home_transport_t`。
   - `smart_home_agent_app_init()` 改为显式 `agent_config_t`。
   - 保持 OpenAI-compatible provider 不变。

3. **gateway transport**
   - 新增 `src/transport/`。
   - 实现 HTTP/1.1 POST、Content-Length 响应读取、错误码映射。
   - Makefile/CMakeLists 接入。

4. **defconfig 更新**
   - BOX-3 `smart_home/defconfig` 切到 gateway。
   - 确认不启用 apps `CRYPTO_MBEDTLS`。

5. **PC 网关脚本**
   - 放在 `tools/` 或 `docs/` 示例区。
   - 支持白名单 host、Authorization 转发、Content-Length 返回。

6. **板端验证记录**
   - 更新 `docs/esp32s3-box.md`。
   - 记录编译命令、Wi-Fi 命令、smart_home 测试样例和已知限制。

## 12. 风险与后续工作

| 风险 | 处理 |
|------|------|
| HTTP 网关不是最终形态 | 文档和 Kconfig 标为 bring-up transport，后续替换为 BOX-3 HTTPS |
| 局域网明文传输 API key | 仅开发阶段使用；网关和设备在同一可信网络；最终直连 HTTPS |
| 云端响应过大 | 限制 `max_output_tokens`，gateway 不改写 body，板端 response buffer 明确报错 |
| BOX-3 直连 HTTPS 仍有 HAL 符号问题 | 将 HTTPS wrapper 放在 board 层，不污染 smart_home/cAGENT |
| 证书和时间问题 | 直连 HTTPS 阶段补 SNTP、CA bundle 或证书 pinning |

## 13. 结论

路线 B 的核心不是绕过云 LLM，而是把“Agent 语义”和“板级网络安全传输”分层。
smart_home 继续使用 OpenAI-compatible provider、tools、skills 和 ReAct loop；
HTTP/HTTPS 只通过 `agent_runtime_t.http_post` 注入。这样可以先在 BOX-3 上完成
真实云模型闭环，又避免同时编译 apps mbedTLS 与 ESP HAL mbedTLS 导致的冲突。
