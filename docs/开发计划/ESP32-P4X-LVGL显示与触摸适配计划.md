# ESP32-P4X Function EV Board：LVGL 显示与触摸适配计划

## 1. 目标与边界

本计划为 `ESP32-P4X-Function-EV-Board` 增加可验证的本机图形显示和触摸
能力，最终使 LVGL 应用能够在官方 7 英寸屏上显示，并接收触摸坐标。

目标链路：

```text
ESP32-P4 MIPI-DSI host
  -> EK79007 面板驱动
  -> framebuffer / 显示设备
  -> LVGL display port

I2C master
  -> GT911 触摸驱动
  -> /dev/inputX
  -> LVGL input device
```

本计划不包含业务 UI、摄像头、音频或网络应用集成。它们必须在显示、触摸的
最小独立验证完成后再进入下一阶段。

## 2. 已确认的硬件基线

| 项目 | 结论 |
| --- | --- |
| 开发板 | ESP32-P4X-Function-EV-Board |
| 芯片修订版 | ESP32-P4 revision v3.1 及以上；当前实板识别为 v3.2 |
| 当前配置 | `CONFIG_ESP32P4_REV_MIN_301=y`，满足 P4X 修订版要求 |
| LCD 模组 | AML070JGI50-07403L，7 英寸，1024 x 600 |
| LCD 控制器 | EK79007AD + EK73217BCGA |
| 显示接口 | MIPI-DSI |
| 触摸控制器 | GT911，I2C 接口 |
| 面板复位 | 主板 GPIO27 -> LCD Adapter `RST_LCD` |
| 背光 PWM | 主板 GPIO26 -> LCD Adapter `PWM` |

LCD 是配套可选组件。开始软件适配前，必须确认 LCD adapter 已使用反向 FPC
线缆接至主板 MIPI-DSI 接口，并完成 GPIO27、GPIO26、5V 与 GND 接线。P4X
板级 LDO_VO3/LDO_VO4 的电压与使能状态也必须按参考设计配置，否则面板可能
保持黑屏。

## 3. 当前代码状态与缺口

当前仓库已有 `drivers/video/mipidsi/` 通用 MIPI-DSI 协议框架，包括 host 和
device 的抽象、DCS 命令封装以及设备注册接口。

当前仓库尚未实现下列关键组件：

| 缺口 | 影响 |
| --- | --- |
| ESP32-P4 MIPI-DSI host | 无法操作 P4 的 MIPI-DSI 控制器、DMA 或中断 |
| EK79007 通用面板驱动 | 无法下发面板初始化序列，也无法建立 1024 x 600 视频时序 |
| GT911 通用触摸驱动 | 无法经 I2C 读取坐标并上报输入事件 |
| P4X 板级显示装配 | 未配置 LDO、复位、背光、DSI host 和面板实例 |
| P4X 板级触摸装配 | 未初始化指定 I2C 总线、地址、复位与输入注册 |
| `lvgl` defconfig | 没有可复现的显示、触摸与 LVGL 配置组合 |

现有 `esp32p4_buttons.c` 中的 `CONFIG_ESPRESSIF_TOUCH` 是芯片内部触摸
传感器（touch-pad）支持，**不是** LCD 上 GT911 电容触摸屏驱动。

## 4. 分层设计与文件归属

### 4.1 归属原则

通用协议不得复制到单一板级目录。EK79007 和 GT911 未来可能被其他板卡复用，
应以 NuttX 通用驱动形式实现；板级代码只描述 P4X 的电源、引脚、总线和设备
装配关系。

```text
应用 / LVGL
    │
    ├─ /dev/fb0（或等价显示设备）
    └─ /dev/inputX
             │
NuttX 通用驱动层
    ├─ EK79007 MIPI-DSI 面板
    └─ GT911 I2C 触摸
             │
ESP32-P4 芯片层
    └─ MIPI-DSI host、I2C、DMA、中断
             │
P4X 板级装配层
    ├─ LDO、电源、GPIO27 Reset、GPIO26 PWM
    └─ I2C bus、设备地址、屏幕方向
```

### 4.2 计划修改的文件

| 层级 | 计划文件 | 职责 |
| --- | --- | --- |
| P4 芯片层 | `chips/esp32p4/common/espressif/esp_mipi_dsi.c` | 实现 P4 MIPI-DSI host ops、时钟、DMA 和中断 |
| P4 芯片层 | `chips/esp32p4/common/espressif/esp_mipi_dsi.h` | host 初始化与板级调用接口 |
| P4 芯片层 | `chips/esp32p4/common/espressif/Kconfig`、`Make.defs`、`CMakeLists.txt` | 建立 MIPI-DSI host 配置与构建入口 |
| 竞赛驱动覆盖层 | `drivers/nuttx/drivers/lcd/{ek79007.c,ek79007.h}` | EK79007 面板 DCS 初始化、视频模式、休眠与恢复 |
| 竞赛驱动覆盖层 | `drivers/nuttx/drivers/input/{gt911.c,gt911.h}` | GT911 I2C 寄存器访问、触点解析、输入事件上报 |
| NuttX 工作树映射 | `nuttx/drivers/{lcd,input}/` | 由 `scripts/link_nuttx_display_drivers.sh` 创建相对软链接，供 NuttX 正常构建 |
| NuttX 构建项 | 对应 `drivers/*/{Kconfig,Make.defs,CMakeLists.txt}` | 注册通用面板和输入驱动 |
| P4X 板级层 | `board/esp32p4/esp32p4-function-ev-board/src/esp32p4_lcd.c` | LDO、reset、背光、DSI 与面板装配 |
| P4X 板级层 | `board/esp32p4/esp32p4-function-ev-board/src/esp32p4_touch.c` | I2C 获取、GT911 复位与注册 |
| P4X 板级层 | `src/esp32p4-function-ev-board.h` | 板级初始化接口、GPIO 常量 |
| P4X 板级层 | `src/esp32p4_bringup.c` | 按 Kconfig 调用显示和触摸初始化 |
| P4X 板级层 | `src/{Make.defs,CMakeLists.txt}`、`Kconfig` | 加入板级源文件和开关 |
| P4X 配置 | `configs/lvgl/defconfig`（新增） | 固化 USB console、DSI、LCD、GT911、LVGL 配置 |

> 注：EK79007 与 GT911 的规范源码当前保存在竞赛目录的 `drivers/nuttx/`，并以
> 相对软链接映射至 NuttX 工作树。该脚本不修改 Kconfig、Make.defs、CMakeLists；
> 构建入口将在驱动 API 稳定后单独接入。若仅为短期原型，也不得在 board `src/`
> 中复制一套无法复用的 GT911/EK79007 协议实现。

## 5. 实施阶段与验收门

### P0：硬件和资料冻结

1. 确认屏幕模组标签为 AML070JGI50-07403L，拍照存档。
2. 核对 LCD adapter 与 P4X 的反向 FPC、GPIO27、GPIO26、5V、GND 接线。
3. 从 P4X 参考设计确认 GT911 的 I2C 控制器、SCL/SDA 引脚、I2C 地址、reset
   和 interrupt 引脚连接；不得凭 ESP-IDF 示例猜测这些参数。
4. 核对 LDO_VO3/LDO_VO4 所需电压和上电顺序。

通过标准：硬件连接表与可引用的原理图页码齐全；屏幕供电、复位和背光线路
可用。

### P1：ESP32-P4 MIPI-DSI host 最小验证

1. 在 P4 芯片层实现 host 初始化、时钟、PHY、DMA 和必要中断。
2. 接入 `drivers/video/mipidsi/` 的 `mipi_dsi_host_register()` 接口。
3. 提供只含 DSI host 的独立 defconfig 或测试命令，不引入 LVGL。
4. 验证 host 能创建 DSI device，发送 DCS short/long packet 并获得明确日志。

通过标准：DSI host 注册成功；失败路径可返回具体 errno；无 DMA 对齐、时钟或
中断异常。

### P2：EK79007 面板最小显示

1. 新增通用 EK79007 面板驱动，使用 P4X 确认过的初始化序列和 1024 x 600
   时序；寄存器常量和时序必须以芯片资料为准。
2. 在 `esp32p4_lcd.c` 中完成 LDO、GPIO27 复位、GPIO26 PWM 背光及 panel
   实例装配。
3. 建立 framebuffer 或等价的 NuttX 显示设备注册路径。
4. 先显示纯色、色条或静态测试图，不引入字体、复杂布局或网络。

通过标准：屏幕完成 reset、背光可控，稳定显示 1024 x 600 测试画面；连续重启
十次不出现黑屏、花屏或内存泄漏。

### P3：GT911 触摸最小验证

1. 先评估 NuttX 现有 `gt9xx` 通用驱动；确有能力缺口时，再完善当前 GT911 I2C
   驱动，读取设备 ID、状态和触点坐标。
2. `esp32p4_touch.c` 仅提供 P4X 的 I2C bus、地址、复位和可选中断配置。
3. 初版可采用轮询；确认 INT 引脚可用后再增加中断路径。
4. 在 NSH 或独立测试程序持续输出单点与多点坐标，完成边界与坐标方向校验。

通过标准：`/dev/inputX` 注册成功；单指、多指、抬起事件可重复读取；坐标范围
与 1024 x 600 面板一致。

### P4：LVGL 最小应用

1. 新增 `configs/lvgl/defconfig`，基于已验证的 `usbconsole`，保留
   `/dev/ttyACM0` 作为故障诊断通道。
2. 启用 framebuffer、输入、LVGL 及最小 LVGL 示例。
3. 先显示标签、按钮和触摸坐标；验证触摸点击可改变标签或背景色。
4. 将 LVGL 绘制和输入处理置于独立任务，避免在中断上下文调用 LVGL。

通过标准：冷启动进入 LVGL 画面；按钮可被触摸点击；串口日志可报告显示/触摸
初始化状态；故障时仍可从 USB console 进入 NSH。

### P5：压力、恢复与上游准备

1. 验证背光开关、面板休眠/唤醒、连续重启和异常恢复。
2. 检查 DMA buffer 对齐、PSRAM 可访问性和 framebuffer 生命周期。
3. 对新增 C 文件执行 `nuttx/tools/checkpatch.sh -f`，完成 Kconfig、Make 和
   CMake 双构建入口检查。
4. 将通用 DSI host、EK79007、GT911 和 P4X board glue 拆分为独立提交，为
   后续上游贡献保留清晰历史。

通过标准：无编译警告；`git diff --check` 通过；实板有启动、显示、触摸、重启
的完整证据。

## 6. 配置与内存预算

最低建议使用 RGB565。1024 x 600 的 framebuffer 占用如下：

| 格式 | 单缓冲 | 双缓冲 | 建议 |
| --- | ---: | ---: | --- |
| RGB565 | 1,228,800 B（约 1.17 MiB） | 2,457,600 B（约 2.34 MiB） | 首期默认选择 |
| RGB888 | 1,843,200 B（约 1.76 MiB） | 3,686,400 B（约 3.52 MiB） | 仅在带宽和 PSRAM 验证后启用 |

首期采用 RGB565 单缓冲，优先将 framebuffer 放入满足 P4 DSI DMA 约束的内存
区域。是否可直接使用 PSRAM 必须以 P4 DSI DMA 实测为准；若 DMA 不支持或存在
对齐限制，应使用内部 SRAM 描述符/行缓冲加 PSRAM 图像缓冲的分层方案，而不是
假设 PSRAM 一定可直接扫描输出。

建议的关键 Kconfig 类别：

```text
CONFIG_MIPI_DSI=y
CONFIG_ESPRESSIF_MIPI_DSI=y             # 计划新增
CONFIG_LCD_EK79007=y                    # 计划新增
CONFIG_INPUT_GT911=y                    # 计划新增
CONFIG_I2C=y
CONFIG_FB=y 或等价显示接口
CONFIG_LVGL=y
CONFIG_ESPRESSIF_LEDC=y                 # GPIO26 背光 PWM
CONFIG_ESPRESSIF_USBSERIAL=y            # 保留 USB console
```

实际 symbol 命名必须遵循当前 NuttX 对应子系统的既有 Kconfig 风格，不以本节
示例为最终接口定义。

## 7. 风险与控制措施

| 风险 | 控制措施 |
| --- | --- |
| 将 P4X 当作旧 P4 板处理 | 固定 revision >= 3.1；每次构建检查 revision Kconfig |
| LCD 未供电或未接 GPIO27/GPIO26 | 在软件排障前先完成硬件接线检查表 |
| 无 P4 DSI host 却直接写面板代码 | P1 必须先独立通过，P2 不得跳过 |
| ESP-IDF 代码与 NuttX 模型混用 | ESP-IDF 仅用于寄存器/时序参考；NuttX 使用其 MIPI、LCD、input 框架 |
| framebuffer 超出 SRAM 或 DMA 不可访问 | 首期 RGB565 单缓冲；记录 DMA 可访问内存和对齐要求 |
| 触摸坐标方向错误 | P3 独立打印坐标并完成旋转/镜像校准后再接 LVGL |
| LVGL 在 ISR 或 bring-up 中阻塞 | 触摸 ISR 只采样/唤醒，LVGL 仅在任务上下文运行 |

## 8. 提交与测试策略

建议按以下顺序提交，避免将仍不可显示的 UI 变更和底层驱动混在一起：

```text
feat(esp32p4): 新增 MIPI-DSI host 支持
feat(lcd): 新增 EK79007 MIPI-DSI 面板驱动
feat(input): 新增 GT911 I2C 触摸驱动
feat(esp32p4x): 装配 LCD 与触摸设备
config(esp32p4x): 新增 LVGL 显示验证配置
test(esp32p4x): 补充显示与触摸实板测试证据
```

每个阶段至少保留以下证据：构建命令与成功末尾、`git diff --check`、USB console
日志、对应硬件现象或截图。P4X 的 Simple Boot 镜像仍应按已验证规则写入
`0x2000`，直到构建系统的正式 flash offset 修复完成。

## 9. 驱动覆盖层与软链接约定

竞赛目录是显示、触摸驱动的唯一规范源码位置：

```text
drivers/nuttx/drivers/lcd/ek79007.c
drivers/nuttx/drivers/lcd/ek79007.h
drivers/nuttx/drivers/input/gt911.c
drivers/nuttx/drivers/input/gt911.h
```

执行以下命令创建或修复 NuttX 工作树链接：

```bash
contest2026_031_niudanxianqianchong/scripts/link_nuttx_display_drivers.sh
contest2026_031_niudanxianqianchong/scripts/link_nuttx_display_drivers.sh --check
```

脚本仅删除它自身旧版本创建、且目标完全匹配的两条 EK79007 悬空链接；遇到其他
普通文件或指向未知位置的软链接会拒绝覆盖。这样可以在目录重构后保持安全，且
避免把 NuttX 构建规则悄然改成不可追踪状态。
