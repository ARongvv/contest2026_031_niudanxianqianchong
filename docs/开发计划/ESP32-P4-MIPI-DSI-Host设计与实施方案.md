# ESP32-P4 MIPI-DSI Host 设计与实施方案

## 1. 目的与边界

本文定义 ESP32-P4 的 MIPI-DSI Host 适配边界、接口和实施验收门，服务于
`ESP32-P4X-Function-EV-Board` 的 EK79007 面板显示。它是
[ESP32-P4X-LVGL 显示与触摸适配计划](ESP32-P4X-LVGL显示与触摸适配计划.md)的芯片层专项设计，
不替代该计划中的板级装配和 LVGL 验收。

首期将 Host 分为两个独立里程碑：

```text
M1：命令 Host
  D-PHY 供电、PLL/PHY、MIPI DSI DCS short/long packet、错误恢复

M2：视频 Host
  DPI 视频时序、framebuffer、DMA 描述符、cache 同步、vsync/error 中断
```

M1 的目标是可靠地初始化面板和发送 DCS 命令；它**不**承诺已经具备持续像素
扫描输出能力。M2 完成前不得宣称已支持 framebuffer 或 LVGL 显示。

本文不包含 EK79007 寄存器表、GPIO27 复位、GPIO26 背光、GT911 触摸和业务
UI；这些分别属于通用面板驱动、P4X 板级层和输入层。

## 2. 已验证的硬件与软件约束

| 项目 | 约束 | 设计含义 |
| --- | --- | --- |
| DSI Host 数量 | ESP32-P4 仅有 1 个 DSI Host | Host API 的 bus 参数首期只能接受 bus 0，仍保留字段以避免接口重写。 |
| 数据 lane | P4 Host 最多 2 条 data lane | 板级配置必须确认面板工作在 1/2 lane 模式；不得按 FPC 引脚数量假设 4 lane。 |
| D-PHY 电源 | D-PHY 需要独立、稳定的 2.5 V 电源 | 使用 P4 可用 LDO 通道供电；具体 VO 通道、上电时序以 P4X 原理图为准。 |
| 面板 | AML070JGI50-07403L，1024 x 600，EK79007AD + EK73217BCGA | 分辨率与 video timing 是面板/板级参数，不能硬编码为通用芯片配置。 |
| 通用框架 | NuttX 已提供 `mipi_dsi_host`、DCS 与 packet 抽象 | Host 必须实现并注册 `mipi_dsi_host_ops`，不复制通用 packet 编解码。 |
| Vendor HAL | P4 HAL 已提供 DSI 寄存器定义和部分 HAL | 仅在芯片层封装；上层不得包含 ESP-IDF 私有类型或头文件。 |

硬件资料参考：[ESP32-P4 Function EV Board 用户指南](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)、[ESP-IDF MIPI-DSI LCD 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html)。开始 M1 前应将面板的 lane 数、lane bit rate、像素格式、水平/垂直 porch、同步极性和实际 LDO 通道记录到板级硬件连接表。

## 3. 分层与责任边界

```text
NuttX MIPI-DSI 通用 API
  mipi_dsi_host_register() / packet / DCS
              │
ESP32-P4 芯片层
  esp_mipi_dsi.c              命令 Host、PHY、传输、故障状态
  esp_mipi_dsi_video.c        DPI、framebuffer、DMA、vsync（M2）
  esp_ldo.c                   LDO vendor API 到 errno 风格的薄封装
              │
ESP HAL / 寄存器层
  mipi_dsi_hal.c / mipi_dsi_periph.c / DSI LL / GDMA HAL
              │
P4X 板级层
  2.5V D-PHY 电源、reset、背光、面板时序、实例装配
```

芯片层只理解 P4 的硬件能力，不应包含面板初始化命令、面板分辨率或 GPIO 编号。
板级层选择面板参数并持有设备实例；通用 EK79007 驱动通过 DCS API 控制面板。

## 4. Host 接口与状态机

芯片私有头文件应提供不暴露 ESP-IDF 类型的接口。首期建议的接口形状如下，具体
symbol 命名在实现时遵循 NuttX 现有风格：

```c
struct esp_mipi_dsi_host_config_s
{
  uint8_t lane_num;            /* 1 或 2，由板级传入 */
  uint32_t lane_bit_rate_mbps; /* 面板资料确认后的速率 */
  uint32_t timeout_ms;
};

int esp_mipi_dsi_host_initialize(
  FAR const struct esp_mipi_dsi_host_config_s *config,
  FAR struct mipi_dsi_host **host);
int esp_mipi_dsi_host_uninitialize(FAR struct mipi_dsi_host *host);
```

返回值统一转换为 NuttX errno 负值；vendor `esp_err_t`、寄存器地址和 HAL 私有
对象必须停留在 `.c` 文件内部。M2 在接口稳定后另行增加 video 配置结构，避免
在 M1 将尚未验证的 DMA/framebuffer 设计固定成 ABI。

Host 生命周期必须是可逆的：

```text
OFF
  -> LDO_READY
  -> PHY_READY
  -> COMMAND_READY
  -> VIDEO_CONFIGURED       （M2）
  -> VIDEO_RUNNING          （M2）
  -> FAULT

stop/uninitialize：按相反方向停止 DMA、关闭 PHY、释放 LDO。
```

只有 `COMMAND_READY` 及后续状态允许 DCS transfer。错误中断、传输超时、PHY
锁定失败进入 `FAULT` 并保留首个错误码；恢复必须先完整 stop，再重新初始化，
不得在未知硬件状态上继续写寄存器。

## 5. M1：命令 Host 实施方案

### 5.1 初始化顺序

1. 校验 `lane_num` 为 1 或 2，校验 bit rate 和超时非零。
2. 取得并启用为 D-PHY 配置的 2.5 V LDO 通道。
3. 配置 DSI Host 时钟、PHY PLL、lane 映射和 D-PHY 时序，等待 PHY ready。
4. 初始化 command transport，安装必要错误中断。
5. 建立 `mipi_dsi_host_ops`，调用 `mipi_dsi_host_register()`。
6. 通过独立 `dsi_probe` 发送 DCS short/long packet，验证可观测的成功和失败路径。

`attach`/`detach` 负责 DSI device 的 VC、lane、format 约束；`transfer` 负责
packet 生命周期和超时。一次 transfer 的 buffer 在函数返回前必须完成使用，或由
Host 明确复制，禁止异步持有调用方临时内存。

### 5.2 并发与中断

- Host 配置、attach/detach 和 transfer 共用互斥锁；配置切换期间不得并发发送 DCS。
- ISR 只确认硬件状态、记录错误、释放 semaphore 或投递 work；不得分配内存、打印
  大量日志、等待锁或调用面板代码。
- 线程上下文完成超时判定、错误转换与恢复。DCS 命令默认串行，首期不追求多请求吞吐。
- `uninitialize` 必须停止新请求、等待在途 transfer 结束或超时，再释放 IRQ/PHY/LDO。

## 6. M2：视频输出、DMA 与内存

DSI DBI/DCS 命令只用于控制面板；1024 x 600 的持续像素输出需要 DPI video
pipeline。M2 必须单独实现并验收：

1. 由板级传入完整 video timing（active、front porch、sync、back porch、极性、
   pixel clock、format），不在芯片层写死 EK79007 数值。
2. 初始化 DPI video、framebuffer 取数和 GDMA/DW-GDMA 资源；先确认 vendor HAL
   所需源码已纳入 P4 HAL 构建，再接入芯片层 CMake 与 Make.defs。
3. DMA 描述符、IRQ 控制块放在 DMA 可访问的内部 RAM；framebuffer 是否可位于
   PSRAM 必须通过实测确认。
4. CPU 写 framebuffer 后做 clean；DMA 写回或读取状态时按方向做 invalidate；
   所有 buffer 满足 cache line 与 DMA 对齐要求。
5. 首期仅 RGB565 单缓冲，显示稳定后才评估双缓冲、局部刷新或 RGB888。

1024 x 600 RGB565 单缓冲为 1,228,800 B（约 1.17 MiB），双缓冲约 2.34 MiB。
这是一项显示流水线预算，不能从任务栈或普通 small-heap 中零散分配。

## 7. Kconfig 与构建接入

Kconfig 应表达硬件能力，而不是把某个面板参数提升为全芯片默认值。建议分层：

```text
CONFIG_ESPRESSIF_LDO                 # P4 LDO 薄封装/依赖
CONFIG_ESPRESSIF_MIPI_DSI            # M1：DSI command Host
CONFIG_ESPRESSIF_MIPI_DSI_VIDEO      # M2：DPI + framebuffer，依赖 Host
CONFIG_LCD_EK79007                   # 通用面板
CONFIG_INPUT_GT911                   # 通用触摸
```

`lane_num`、lane bit rate、video timing、framebuffer 数量与格式属于 P4X 板级
Kconfig 或板级静态配置。所有新增 C 源必须同时更新对应 `Kconfig`、`Make.defs`
和 `CMakeLists.txt`；P4 vendor HAL 增量源也必须在 `hal_esp32p4.mk` 与
`hal_esp32p4.cmake` 保持一致。

建议的首批文件边界：

| 文件 | M1/M2 | 职责 |
| --- | --- | --- |
| `chips/esp32p4/common/espressif/esp_ldo.c/.h` | M1 | LDO 生命周期、errno 转换 |
| `chips/esp32p4/common/espressif/esp_mipi_dsi.c/.h` | M1 | Host、PHY、DCS transfer |
| `chips/esp32p4/common/espressif/esp_mipi_dsi_video.c/.h` | M2 | DPI/video/DMA/vsync |
| `chips/esp32p4/common/espressif/{Kconfig,Make.defs,CMakeLists.txt}` | M1/M2 | 芯片层开关和构建 |
| `chips/esp32p4/hal_esp32p4.{mk,cmake}` | M1/M2 | 条件纳入 vendor DSI/GDMA HAL 源 |
| `board/.../src/esp32p4_lcd.c` | M1 后 | P4X 电源、GPIO、面板实例 |
| `app/dsi_probe/`、`configs/dsi_probe/defconfig` | M1 | 与 LVGL 解耦的验证入口 |

## 8. 验收矩阵

| 阶段 | 最小测试 | 通过标准 |
| --- | --- | --- |
| M1 编译 | `dsi_probe` 构建，Make/CMake 两入口 | 无链接遗漏、无 warning、Kconfig 依赖可复现 |
| M1 启动 | USB console 打印 LDO、PHY、Host 状态 | 失败能定位到 LDO/PLL/transfer 并返回 errno |
| M1 DCS | 发 short/long packet、面板 reset 后读/写可观测命令 | 不死锁、不在 ISR 阻塞，重复十次初始化/释放稳定 |
| M2 显示 | RGB565 色条/纯色 | 1024 x 600 稳定，无撕裂、花屏或 DMA abort |
| M2 压力 | 背光、sleep/wake、重启、连续刷新 | 无资源泄漏，异常后可从 `FAULT` 完整恢复 |

每次验收至少留存构建命令、`git diff --check`、USB console 日志和屏幕照片/视频。

## 9. 实施顺序与提交粒度

```text
feat(esp32p4): 增加 LDO 与 MIPI-DSI 命令 Host 基础
build(esp32p4): 接入 MIPI-DSI vendor HAL 与双构建入口
test(esp32p4x): 新增 dsi_probe 配置与命令 Host 实板证据
feat(esp32p4): 增加 MIPI-DSI 视频输出与 framebuffer 管理
feat(lcd): 接入 EK79007 面板与 P4X 显示装配
config(esp32p4x): 新增 LVGL 显示与触摸验证配置
```

每个提交只跨越一个层次。未完成 M1 时不提交声称可显示的 LVGL 配置；未确认
DMA 内存属性时不将 PSRAM framebuffer 作为默认事实。

## 10. 风险清单

| 风险 | 控制措施 |
| --- | --- |
| 面板/adapter 并非 1/2 lane 兼容 | P0 核对面板资料和原理图；P4 Host 不支持 4 lane 时不靠软件绕过。 |
| D-PHY 供电通道选择错误 | 用原理图确认实际 LDO 通道和电压；启动日志记录 LDO acquire/enable 结果。 |
| Vendor HAL 源未纳入构建 | 先做 M1 最小链接验证；mk/cmake 双入口同时检查。 |
| 将 DCS 控制误认为视频显示 | M1/M2 分离验收，M2 前不注册 `/dev/fb0`。 |
| DMA/PSRAM 不一致或 cache 未同步 | 单缓冲色条验证起步，记录内存区域、对齐和 cache 操作。 |
| DSI 错误 ISR 与面板线程竞态 | ISR 最小化，状态机和互斥锁只在任务上下文完成恢复。 |

