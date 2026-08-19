# ESP32-S3-BOX-3 线程与 SRAM/PSRAM 内存放置

## 1. 目的

本文记录 Smart Home 在 ESP32-S3-BOX-3 上的线程栈和动态数据实际放置情况，
说明如何区分内部 SRAM、PSRAM、CPU affinity，以及后续如何有控制地调整内存模型。

本文不将 TLS EOF/超时直接归因于内存位置。内存放置是独立的可观测项，需要通过
重复测试与网络侧日志共同分析。

## 2. 当前内存模型

当前 Smart Home 为 Flat build，关键配置如下：

```text
CONFIG_BUILD_FLAT=y
CONFIG_MM_REGIONS=2
CONFIG_ESP32S3_SPIRAM=y
CONFIG_ESP32S3_SPIRAM_MODE_OCT=y
CONFIG_ESP32S3_SPIRAM_SPEED_80M=y
CONFIG_ESP32S3_SPIRAM_COMMON_HEAP=y
```

ESP32-S3 的内部 DRAM 地址窗口为 `0x3FC88000` 至 `0x3FD00000`；PSRAM 的
可缓存数据窗口为 `0x3C000000` 至 `0x3E000000`。

`SPIRAM_COMMON_HEAP` 会将 PSRAM 作为公共堆的额外 region。普通
`malloc/calloc/realloc` 和未提供自定义栈的 `pthread_create()` 都从这组公共堆
申请，分配器决定具体落点。因此，“PSRAM 已启用”不等于“新建线程一定在 PSRAM”。

## 3. 实机地址观测

以下日志来自 Console 模式的一次成功 LLM 请求。该版本的日志函数暂时输出了
`region=unknown`，但可依据 ESP32-S3 地址窗口人工判定：

| 对象 | 地址 | 实际区域 | 结论 |
| --- | --- | --- | --- |
| Smart Home 主线程栈标记 | `0x3FCD0EC4` | 内部 SRAM | 主任务栈在 SRAM。 |
| `smart_home_agent_app_t` | `0x3FCCEB80` | 内部 SRAM | 主线程局部 app 对象在 SRAM。 |
| MCP Bridge context | `0x3FCC8478` | 内部 SRAM | 长生命周期 Bridge 对象在 SRAM。 |
| App Bridge context | `0x3FCC86A0` | 内部 SRAM | 长生命周期 Bridge 对象在 SRAM。 |
| MCP worker 栈标记 | `0x3FCDCD38` | 内部 SRAM | 16 KiB worker 栈在 SRAM。 |
| App Bridge worker 栈标记 | `0x3FCDF330` | 内部 SRAM | 4 KiB worker 栈在 SRAM。 |
| LLM 请求缓冲 | `0x3FCE33B8` | 内部 SRAM | 约 12 KiB 请求缓冲在 SRAM。 |
| LLM 响应缓冲 | `0x3FCE63C0` | 内部 SRAM | 约 8 KiB 响应缓存也在 SRAM。 |
| TLS HTTP header buffer | `0x3C221E18` | PSRAM | 4 KiB 临时缓冲在 PSRAM。 |
| TLS HTTP read buffer | `0x3C221E18` | PSRAM | 与 header buffer 为顺序复用，不是冲突。 |

TLS header buffer 在释放后，TLS read buffer 随即获得同一地址是堆分配器复用空闲块
的正常行为。两个对象没有同时存活，不能视为数据重叠或内存破坏。

## 4. 区域日志

`ov_mem_region_log()` 位于
`packages/cAGENT/src/runtime/runtime_openvela.c`，在 ESP32-S3 构建下调用
`esp32s3_ptr_extram()` 判定指针是否属于 PSRAM：

```text
[cagent_mem] bridge-worker-stack ptr=0x3fcdf330 region=SRAM
[cagent_mem] tls-read-buffer ptr=0x3c221e18 region=PSRAM
```

日志覆盖：

- 主线程、Agent 调用栈、MCP/App Bridge/Node Gateway/LVGL/App chat worker 栈标记；
- Bridge context、HTTP 请求/响应、WebSocket snapshot/frame；
- OpenAI 兼容模型请求/响应缓冲；
- TLS context、HTTP header buffer、TLS read buffer。

区域日志只回答“该地址在哪个物理内存窗口”。它不能单独证明栈高水位、越界写、
内存泄漏或两个存活对象的生命周期是否重叠。

## 5. CPU affinity 与内存位置

二者彼此独立：

| 目标 | 手段 | 作用 |
| --- | --- | --- |
| 指定线程运行 CPU | `pthread_attr_setaffinity_np()` | 仅在 SMP 下约束可调度 CPU。 |
| 指定线程栈位置 | `pthread_attr_setstack()` | 使用调用方提供的栈地址，不再自动从公共堆申请。 |
| 指定普通动态缓冲位置 | 专用内存池或链接器段 | `malloc()` 在 common heap 下不保证区域。 |

当前默认 `smart_home` 配置为单核。`smart_home_smp` 仅用于双核实验；即使线程被绑到
CPU1，其栈仍可能位于 SRAM 或 PSRAM，除非另外指定栈内存。

## 6. 手动管理线程栈

NuttX 在 `pthread_create()` 中发现 `attr->stackaddr` 非空时，会使用
`up_use_stack()` 接管调用方提供的栈，不再调用默认 `up_create_stack()`。因此可通过
`pthread_attr_setstack()` 固定线程的栈地址：

```c
static uint8_t bridge_stack[4096] __attribute__((aligned(16)));

pthread_attr_t attr;
pthread_attr_init(&attr);
pthread_attr_setstack(&attr, bridge_stack, sizeof(bridge_stack));
pthread_create(&bridge->worker, &attr, bridge_worker, bridge);
```

使用自定义栈时必须满足：

1. 栈在整个线程生命周期内保持有效，线程退出前不得释放或复用。
2. 栈大小不小于 `PTHREAD_STACK_MIN`，并满足 Xtensa/TLS 对齐要求；建议 16 字节对齐。
3. 通过区域日志验证静态数组最终确实位于预期 SRAM 或 PSRAM 地址窗口。
4. 对自定义栈启用栈用量观测后，再收紧大小；不可只按配置值推断实际峰值。

静态全局数组通常可用于固定 SRAM 栈，但最终位置仍应在真机验证。DMA、ISR、
cache-disabled 路径需要的栈或数据必须保留内部 SRAM，不能仅以“PSRAM 容量更大”为由
迁移。

## 7. 可靠使用 PSRAM 的边界

`SPIRAM_COMMON_HEAP` 本身没有为应用提供“从 PSRAM 精确 malloc”的通用接口。以下做法
不应采用：

- 循环 `malloc()`，直到得到 PSRAM 地址后保留该块；这会制造碎片并让行为不可预测。
- 将所有 worker 栈迁移至 PSRAM；其中可能包含网络、DMA 或 cache 敏感调用链。
- 仅依据 `mallinfo()` 的约 16 MiB 空闲总量判断 SRAM 也充足。

本实现为 ESP32-S3 新增 `CONFIG_ESP32S3_SPIRAM_BULK_POOL_SIZE`。板级堆初始化先从
可分配 PSRAM 中保留该容量（BOX-3 Smart Home 配置为 192 KiB），再把剩余 PSRAM 加入
common heap。保留区域由独立 heap 管理，`ov_mem_bulk_alloc()` 只从其中分配，绝不回退到
内部 SRAM。其当前用途为：

- OpenAI-compatible LLM 的请求体和响应体。
- 手机 Bridge 的 HTTP 请求、网关响应、snapshot 和 WebSocket frame。
- 手机 App Chat 的 64 KiB AI worker 自定义栈。

当该池不足时，HTTP/WebSocket 建连或请求会返回既有的 `503 resource_unavailable`；设备事件
通知则丢弃本次推送。这样把低优先级的瞬时大块内存压力限制在 PSRAM 池内，而不挤占 TLS、
网络和短 worker 栈需要的内部 SRAM。

如需确定把其他大栈或大缓冲移到 PSRAM，应采用以下之一：

1. 在平台层保留一段 PSRAM，并创建只服务于 Smart Home 的专用 heap/pool。
2. 确认 NuttX 链接脚本已支持外部 BSS 段后，用经过验证的链接器 section 放置静态
   PSRAM 缓冲或自定义栈。

`CONFIG_ESP32S3_SPIRAM_USER_HEAP` 会改变内核/用户堆模型，不是“只把一个 worker
迁到 PSRAM”的轻量开关，当前 Flat build 不应为此直接切换。

## 8. 推荐放置策略

| 对象 | 推荐区域 | 原因 |
| --- | --- | --- |
| 中断相关数据、DMA 描述符和严格 DMA buffer | 内部 SRAM | 要求低延迟、DMA/缓存语义明确。 |
| Wi-Fi、网络和短 Bridge worker 栈 | 内部 SRAM | 保留确定性，避免 cache 相关风险。 |
| Node/MCP worker 栈 | 内部 SRAM | 当前由 `.dram1` 静态栈提供，避免 common heap 的不确定放置。 |
| LLM JSON 请求/响应、WebSocket frame、history/snapshot | PSRAM 优先 | 容量大、非 ISR、可容忍缓存访问延迟。 |
| LVGL framebuffer | PSRAM | 当前 LCD 驱动已在 DMA 前执行 cache writeback。 |
| App Chat 64 KiB AI worker 栈 | PSRAM | 非 ISR、非 DMA 路径；通过自定义栈显式绑定，仍需压力验证。 |
| LVGL agent 大栈 | 后续评估 | 必须确认没有 ISR/DMA/cache-disabled 访问路径。 |

当前实现将 LLM 约 12 KiB 请求和 8 KiB 响应缓冲迁入可控 PSRAM 池；4 KiB App Bridge
worker 栈、启用时的 8 KiB Node worker 栈与 16 KiB MCP worker 栈通过
`pthread_attr_setstack()` 绑定到 `.dram1` 内部 SRAM。
App Chat 的 64 KiB AI worker 栈从专用 PSRAM pool 分配并通过
`pthread_attr_setstack()` 绑定；线程退出且 `pthread_join()` 返回后才释放。LVGL agent
大栈和 framebuffer 仍保持原状，待压力测试后再决定是否调整。

## 9. 验证步骤

1. 烧录包含真实 `esp32s3_ptr_extram()` 分类的固件，确认所有日志显示 `SRAM` 或
   `PSRAM`，不再是 `unknown`。
2. 在 Console 模式执行 `hi`，记录 Agent、模型缓冲和 TLS context 的区域。
3. 用 `curl` 或手机 App 触发 REST/WebSocket，确认 Bridge 短时缓冲为 `PSRAM`，并记录
   `psram_bulk total/free/largest` 日志。
4. 在 LVGL 模式发起聊天，记录 LVGL agent worker 栈及模型缓冲区域。
5. 开启栈 coloration/usage，采集主任务、Bridge、MCP 和聊天 worker 的栈高水位。
6. 对同一负载重复请求，比较内部 SRAM 对象数量、TLS 成功率、专用 PSRAM 池剩余量和堆碎片
   变化；再通过并发连接或较大请求验证池耗尽时返回 `503`。

只有同时满足“地址区域正确、栈高水位安全、重复压力稳定”后，才将手动放置策略固化到
默认 `smart_home` 配置。
