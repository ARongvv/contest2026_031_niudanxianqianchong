# BOX-3 Bridge 内存与 TLS 排障记录

## 1. 范围与结论

本文记录 ESP32-S3-BOX-3 上 Smart Home 手机 App Bridge 与 LLM TLS
请求的排障过程，重点区分两个相关但不能混为一谈的问题：

1. Bridge HTTP/WebSocket worker 的任务栈安全。
2. Console 模式下访问 DeepSeek 时偶发的 TLS EOF 或超时。

当前结论如下：

- 旧 Bridge 的 HTTP 请求、API 响应和 WebSocket 帧缓冲位于 worker 函数栈，
  在 4 KiB worker 栈下存在明确的栈溢出风险，必须重构。
- 已将这些大缓冲改为按连接的短时堆分配；内存不足时返回 HTTP 503，避免继续
  挤占任务栈。
- LLM 的 TLS EOF/超时发生在 DNS 和 TCP 成功之后、TLS 握手阶段。现有 A/B
  和 `mallinfo()` 数据不能证明它由 Bridge worker 栈或全局堆总量不足直接造成。

## 2. 现象

设备已正常接入 WLAN，网络初始化日志显示 IP、网关、DNS 均有效，例如：

```text
[smart_home_net] init-begin if=wlan0 ip=192.168.1.103 gateway=192.168.1.1 ...
[smart_home_net] init-end if=wlan0 ip=192.168.1.103 gateway=192.168.1.1 ... online=1
```

手机侧可访问 Bridge 的 REST 接口：

```text
GET  /v1/home/snapshot                 -> 200
POST /v1/commands                      -> 202
GET  /v1/history                       -> 200
```

但 Console 发送 LLM 请求时，曾出现以下两类失败：

```text
phase=tcp connected host=api.deepseek.com port=443
ssl_handshake ret=-0x7280: SSL - The connection indicated an EOF
```

或：

```text
phase=tls handshake_wait timeout
```

成功样本同样会经过 DNS、TCP、TLS 和 HTTP 200，因此失败点在 ClientHello 后的
服务端响应或本机接收路径，尚不能据此断言是 App Bridge 引起。

## 3. A/B 对照

| 场景 | Console LLM 结果 | 说明 |
| --- | --- | --- |
| 关闭 `CONFIG_SMART_HOME_APP_BRIDGE` | 曾成功 | Bridge 不参与时链路可成功。 |
| 创建监听 socket，但不创建 worker | 曾成功 | 监听 socket 本身未复现问题。 |
| 创建 4 KiB worker，关闭 `accept()` | 曾成功，也曾 TLS EOF | 同一配置存在成功和失败样本。 |
| 创建 8 KiB worker，关闭 `accept()` | 曾 TLS 超时 | 增大 worker 栈没有稳定改善。 |

因此，worker 栈大小与 TLS 成败曾有表面相关性，但目前缺少可重复的因果证据。
网络侧的 CDN 节点、WAN 抖动、ESP Wi-Fi/TCP 接收路径或内部专用资源仍需继续
排除。

## 4. 运行时堆预算证据

在以下节点打印了 `mallinfo().fordblks`、`mxordblk` 和 `uordblks`：

- Bridge worker 创建前后。
- LLM 请求体、响应体分配前后。
- TLS 建连、握手前后。
- 一次模型请求结束后。

一组 LLM 成功样本：

```text
bridge-worker-before-create fordblks=16138672 mxordblk=16068008 uordblks=891144
bridge-worker-after-create  fordblks=16134328 mxordblk=16068008 uordblks=895488

model-request-before fordblks=16117936 mxordblk=16068008 uordblks=911880
model-request-after  fordblks=16105640 mxordblk=16068008 uordblks=924176
model-response-after fordblks=16097440 mxordblk=16068008 uordblks=932376
tls-setup-after      fordblks=16061032 mxordblk=16051232 uordblks=968784
tls-handshake-after  fordblks=16052152 mxordblk=16048616 uordblks=977664
model-request-finished fordblks=16109608 mxordblk=16068008 uordblks=920208
```

说明：

- 创建 4 KiB Bridge worker 后，堆占用约增加 4344 字节，符合任务栈及任务控制块
  的量级。
- LLM 请求体约增加 12 KiB，响应缓存约增加 8 KiB；TLS 建连和握手峰值额外约
  使用 45 KiB。
- 请求结束后仍保留约 8 KiB，是 OpenAI 兼容 provider 缓存最近响应的设计，不是
  本轮请求泄漏。
- 空闲堆约 16 MiB，最大连续块约 15 MiB。当前配置使用两个 heap region，并将
  PSRAM 加入 common heap，因此 `CONFIG_RAM_SIZE=114688` 不代表运行时可用总堆。

这组数据排除了“聚合全局堆已经耗尽”的判断，但 `mallinfo()` 是聚合指标，不能
证明 Wi-Fi、网络协议栈或 DMA 等内部专用资源一定可用。

## 5. 原 Bridge 的栈风险

旧实现将大数组放在 Bridge worker 的调用链中，典型包括：

- HTTP 请求缓冲约 6 KiB。
- 网关 API 响应对象约 4 KiB。
- WebSocket 初始化快照约 4 KiB，以及对应帧缓冲约 4 KiB。

这些局部对象的总量远大于 4 KiB worker 栈，即使它们并非每次同时达到峰值，也会
使 HTTP/WebSocket 请求处理带来栈破坏风险。该问题与 TLS EOF 是否同源无关，
仍应优先修复。

## 6. 已实施的 Bridge 内存模型

Bridge 保持 4 KiB worker 栈，并采用以下原则：

1. HTTP 请求缓冲、网关 API 响应、WebSocket 快照和帧缓冲改为堆上的短时对象。
2. 每次连接结束后释放请求和响应对象；WebSocket 初始帧发送完成后释放快照和帧。
3. 分配失败时向该连接返回 `503 resource_unavailable`，不使用超大栈数组兜底。
4. 设备状态和 Agent 事件分配失败时允许丢弃推送；客户端依据 revision 重新拉取
   `/v1/home/snapshot`，保证最终状态一致。
5. 使用 `agent_mutex` 的 `trylock` 串行化 Bridge 和本地 Agent 的大内存活动。
   Agent 正在执行时，Bridge 返回 `503 agent_busy`，避免与 TLS 建连争抢临时资源。
6. 当前只维护一个 WebSocket peer，避免无界连接和帧缓冲占用。

该重构的目标是栈安全和明确的资源退化策略，不应被视为 TLS EOF 的直接修复。

## 7. 当前配置

```text
CONFIG_SMART_HOME_APP_BRIDGE=y
CONFIG_SMART_HOME_APP_BRIDGE_STACKSIZE=4096
CONFIG_SMART_HOME_APP_BRIDGE_WORKER=y
CONFIG_SMART_HOME_APP_BRIDGE_WORKER_ACCEPT=y
# CONFIG_SMART_HOME_APP_BRIDGE_CHAT is not set
```

聊天运行服务仍关闭。后续应将 run service 改为按需创建、可回收的模型，再开启
`SMART_HOME_APP_BRIDGE_CHAT`，避免在启动期常驻占用大栈。

## 8. 重构后的实机验证

在保持 Bridge worker 已创建、4 KiB 栈、正常 `accept()` 配置的实机上，Console
输入 `hi` 后完成了一次 DeepSeek 请求：

```text
bridge-worker-before-create fordblks=16138688 mxordblk=16068008 uordblks=891128
bridge-worker-after-create  fordblks=16134344 mxordblk=16068008 uordblks=895472

phase=tcp connected host=api.deepseek.com port=443 fd=4
phase=tls handshake_ok version=TLSv1.2 cipher=TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384
HTTP status 200
http_post done status=200 body_len=727
2690ms | MODEL_RESP | iter=1 | err=0 | ok
```

本次验证说明重构后的 Bridge worker 可以与 Console LLM 请求共存，且没有出现
栈破坏、TLS 卡死或全局堆不足。其内存变化与第 4 节成功样本一致：TLS 握手峰值时
仍有约 16 MiB 空闲堆和约 15 MiB 最大连续块。

这只是一个成功观测，不代表此前 TLS EOF/超时已经修复；仍需按下一节的重复与对照
测试确认失败率是否发生变化。

## 9. 建议验证矩阵

1. 启动 Console 模式，连续发起多次短 LLM 请求，保留 TLS 阶段和内存日志。
2. 手机侧验证 snapshot、command、history、catalog 路由，以及 WebSocket 首帧和
   `device_*` 事件。
3. 在 Console LLM 执行期间发送 REST 请求，预期快速返回 `503 agent_busy`，而非
   阻塞、崩溃或影响 TLS。
4. 在 REST/WebSocket 压力下重复 Console LLM 请求，比较失败率与空闲时的差异。
5. 建立一个局域网内稳定的 TLS 测试服务。若本地 TLS 同样失败，重点检查 ESP32-S3
   Wi-Fi/TCP 接收路径；若只有公网 DeepSeek 节点失败，优先检查 WAN/CDN 差异。
6. 后续启用 `CONFIG_STACK_COLORATION` 或等价栈用量统计，实测 Bridge worker、
   Console 主任务、MCP discovery 和聊天 run worker 的高水位。

## 10. 后续处理原则

- 保留当前内存诊断日志直至得到可重复的 TLS 结论，之后单独清理调试输出。
- 不通过盲目增大 worker 栈来掩盖局部大缓冲；新接口必须审查调用链的最大栈帧。
- 在没有本地 TLS 对照测试前，不把 DeepSeek TLS EOF 归因为 Bridge、LVGL 或堆总量。
