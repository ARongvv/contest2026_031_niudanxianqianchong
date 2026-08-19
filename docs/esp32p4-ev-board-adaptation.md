# ESP32-P4 Function EV Board 适配 openvela 技术文档

> **文档版本**: v1.1
> **创建日期**: 2026-06-30
> **最近修订**: 2026-08-19
> **适用项目**: contest2026_031_niudanxianqianchong / openvela
> **目标硬件**: ESP32-P4 Function EV Board (乐鑫官方开发板)

---

## 一、概述

### 1.1 适配目标

当前目标是将 **ESP32-P4 Function EV Board** 的芯片层和板级层适配到
openvela，并建立可重复的构建、烧录和外设验证路径。第一阶段验收是
最小 NSH 可在实板启动，不绑定 SmartHome、audio_event 或其他应用。

SmartHome、audio_event 等均属于后续应用迁移或外设验收载体。本文件中
较早章节出现的 SmartHome UI 示例仅作应用阶段参考，不能替代板级适配
的验收标准。

### 1.2 ESP32-P4 vs ESP32-S3 关键差异

| 特性 | ESP32-S3 (当前) | ESP32-P4 (目标) |
|------|-----------------|-----------------|
| **CPU 架构** | Xtensa LX7 双核 240MHz | RISC-V HP 双核 400MHz + LP 单核 40MHz |
| **FPU** | 单精度 | 单精度 + AI 指令扩展 |
| **内部 SRAM** | 512 KB | 768 KB (HP) + 32 KB (LP) |
| **PSRAM** | 最大 8MB (Octal SPI) | 支持 (通过 OSPI) |
| **Flash** | 最大 16MB | 支持 (通过 OSPI) |
| **WiFi/BLE** | 内置 | SoC 无内置；本板通过板载 ESP32-C6 提供无线协处理能力 |
| **显示接口** | SPI LCD (320×240) | **MIPI-DSI** (最高 1080p) |
| **摄像头** | 无原生接口 | **MIPI-CSI** (最高 1080p) |
| **USB** | USB-OTG 1.1 | **USB-OTG 2.0 HS** |
| **以太网** | 无 | **内置 EMAC** |
| **GPIO** | 45 | 55 |
| **DMA** | GDMA | GDMA + 2D-DMA |
| **视频编解码** | 无 | H.264 编码 (1080p@30fps) |
| **图像加速** | 无 | PPA (Pixel Processing Accelerator) |
| **NuttX arch** | `nuttx/boards/xtensa/esp32s3/` | `nuttx/boards/risc-v/esp32p4/` |
| **工具链** | `xtensa-esp32s3-elf-gcc` | `riscv32-esp-elf-gcc` 或 `riscv-none-elf-gcc` |

### 1.3 适配工作量评估

| 层次 | 工作量 | 说明 |
|------|--------|------|
| 工具链 | ★☆☆☆☆ | 使用现有 RISC-V 工具链或获取乐鑫专用工具链 |
| Arch 层 | ★★★☆☆ | 需从上游 NuttX 合入 ESP32-P4 支持 |
| Board 层 | ★★☆☆☆ | 上游已有 `esp32p4-function-ev-board`，需适配 LCD/触摸 |
| WiFi 层 | ★★★★☆ | 需实现 ESP-Hosted 伴侣芯片方案，工作量最大 |
| 应用层 | ★★☆☆☆ | SmartHome UI 需适配新分辨率，cAGENT 框架基本无需修改 |
| 总计 | ~3-4 周 | 假设 1-2 名工程师全职投入 |

---

## 二、上游 NuttX ESP32-P4 支持状态

### 2.1 Apache NuttX 上游现状

截至 2026 年 6 月，Apache NuttX 主线（`master` 分支）**已包含** ESP32-P4 完整支持：

```
nuttx/
├── arch/risc-v/src/esp32p4/            # 芯片层（薄封装，依赖 common/espressif）
│   ├── Kconfig                          # 芯片配置（版本选择、Cache 配置）
│   ├── esp_chip_rev.c                   # 芯片版本检测
│   ├── hal_esp32p4.cmake                # HAL 集成（拉取乐鑫组件）
│   └── hal_esp32p4.mk
│
├── arch/risc-v/src/common/espressif/    # 乐鑫 RISC-V 芯片共享驱动层
│   ├── Kconfig                          # 统一外设配置（GPIO/SPI/I2C/UART/WiFi...）
│   ├── esp_gpio.c                       # GPIO 驱动
│   ├── esp_spi.c                        # SPI 驱动
│   ├── esp_i2c.c                        # I2C 驱动
│   ├── esp_wlan.c                       # WiFi (ESP-Hosted) 驱动
│   ├── esp_emac.c                       # 以太网 EMAC 驱动
│   ├── esp_mipi.c                       # MIPI-DSI/CSI 驱动
│   └── ...                              # 其他共享驱动
│
├── boards/risc-v/esp32p4/
│   ├── common/                          # 板级共享代码
│   │   ├── include/                     # 共享头文件
│   │   ├── scripts/                     # 链接脚本
│   │   └── src/                         # 共享初始化代码
│   │
│   ├── esp32p4-function-ev-board/       # ★ 目标板
│   │   ├── Kconfig                      # 板级配置
│   │   ├── src/                         # 板级初始化源码
│   │   ├── include/                     # board.h 等
│   │   └── configs/                     # 35 个 defconfig 配置
│   │       ├── nsh/                     # 最小 NSH shell
│   │       ├── spi/                     # SPI 外设测试
│   │       ├── ethernet/                # 以太网配置
│   │       ├── webpanel/                # Web 面板 (含网络/Python/Flash)
│   │       ├── i2c/                     # I2C 外设测试
│   │       ├── psram_usrheap/           # PSRAM 测试
│   │       ├── usbconsole/              # USB 串口控制台
│   │       └── ...                      # 共 35 个配置
│   │
│   ├── esp32p4-pico-wifi-wareshare/     # Pico WiFi 变体
│   └── esp32p4-tab5/                    # Tab5 平板变体
```

### 2.2 OpenVela Fork 现状

当前 OpenVela 的 NuttX 分支 (`dev-ai-contest-2026`) **尚未合入** ESP32-P4 支持：

```
nuttx/arch/risc-v/src/       # 无 esp32p4/ 目录
nuttx/boards/risc-v/         # 无 esp32p4/ 目录
```

**这意味着需要从上游 NuttX 合入相关代码。**

### 2.3 上游已支持的 EV Board 外设

根据上游 `esp32p4-function-ev-board` 的 35 个 defconfig，已验证支持的外设：

| 外设 | 配置名 | 状态 |
|------|--------|------|
| UART (控制台) | nsh | ✅ 已支持 |
| GPIO | gpio | ✅ 已支持 |
| SPI | spi | ✅ 已支持 |
| I2C | i2c | ✅ 已支持 |
| I2S (音频) | i2schar | ✅ 已支持 |
| PWM/LEDC | pwm | ✅ 已支持 |
| ADC | adc | ✅ 已支持 |
| 以太网 EMAC | ethernet, webpanel | ✅ 已支持 |
| SPI Flash (SmartFS) | spiflash, webpanel | ✅ 已支持 |
| PSRAM | psram_usrheap | ✅ 已支持 |
| USB 串口 | usbconsole | ✅ 已支持 |
| 看门狗 | watchdog | ✅ 已支持 |
| RTC | rtc | ✅ 已支持 |
| TWAI (CAN) | twai | ✅ 已支持 |
| RMT (红外/LED) | rmt | ✅ 已支持 |
| 温度传感器 | temperature_sensor | ✅ 已支持 |
| 电机控制 | motor | ✅ 已支持 |
| SPI Slave | spislv | ✅ 已支持 |
| eFuse | efuse | ✅ 已支持 |
| 加密引擎 | crypto | ✅ 已支持 |
| ULP 低功耗协处理器 | ulp | ✅ 已支持 |
| Python (MicroPython) | python | ✅ 已支持 |
| WiFi (ESP-Hosted) | (需额外配置) | ⚠️ 需伴侣芯片 |
| LCD/MIPI-DSI | (不在上游配置中) | ❌ 需自行适配 |
| 触摸屏 | (不在上游配置中) | ❌ 需自行适配 |

---

## 三、系统架构设计

### 3.1 总体架构

```
┌──────────────────────────────────────────────────────────────┐
│                    SmartHome AI Agent 应用                    │
│  ┌─────────────┐  ┌──────────┐  ┌─────────────────────────┐ │
│  │ Console UI  │  │ LVGL UI  │  │ cAGENT Framework        │ │
│  │ (REPL)      │  │ (1024×   │  │ (ReAct Loop + Tools)    │ │
│  │             │  │  600)    │  │                         │ │
│  └──────┬──────┘  └────┬─────┘  └─────────┬───────────────┘ │
├─────────┴──────────────┴──────────────────┴─────────────────┤
│                    openvela (NuttX RTOS)                      │
├──────────────────────────────────────────────────────────────┤
│                    板级支持 (BSP)                              │
│  ┌────────────┐  ┌───────────┐  ┌──────────────────────────┐│
│  │ ESP32-P4   │  │ MIPI-DSI  │  │ ESP-Hosted WiFi         ││
│  │ EV Board   │  │ LCD Panel │  │ (ESP32-C6 伴侣)         ││
│  │ Board Init │  │ + 触摸屏  │  │                         ││
│  └────────────┘  └───────────┘  └──────────────────────────┘│
├──────────────────────────────────────────────────────────────┤
│                    NuttX Arch 层                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ arch/risc-v/src/common/espressif/ (共享驱动)            │ │
│  │ GPIO │ SPI │ I2C │ UART │ EMAC │ WiFi │ DMA │ Timer   │ │
│  └─────────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ arch/risc-v/src/esp32p4/ (芯片特定)                    │ │
│  │ 启动 │ 中断 │ 时钟 │ Cache │ 版本检测                   │ │
│  └─────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│                    硬件层                                     │
│  ┌──────────────┐  ┌──────────┐  ┌────────────────────────┐ │
│  │ ESP32-P4     │  │ 7" MIPI  │  │ ESP32-C6               │ │
│  │ HP 双核 400M │  │ 1024×600 │  │ WiFi 6 + BLE 5 伴侣   │ │
│  │ LP 核 40M    │  │ + 触摸   │  │ (SPI/SDIO 连接)       │ │
│  └──────────────┘  └──────────┘  └────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 无线网络方案

ESP32-P4 SoC **无内置 Wi-Fi/BLE**。但 ESP32-P4 Function EV Board 板载
ESP32-C6 作为无线协处理器，P4 板级移植时应先确认已有硬件连接和所需
固件，再决定是否接入 ESP-Hosted；不应把“外接 C6 模组”当成该板的默认
前提。

#### 方案 A：ESP-Hosted + ESP32-C6（推荐）

```
ESP32-P4 (Host)                ESP32-C6 (Companion)
┌──────────┐    SPI/SDIO      ┌──────────────┐
│ NuttX    │◄────────────────►│ ESP-AT /     │
│ WiFi     │    数据通道       │ ESP-Hosted   │
│ Driver   │                  │ Slave FW     │
│ (esp_    │    ┌────────┐    │              │
│  hosted) │◄──►│ GPIO   │───►│ WiFi 6 +    │
│          │    │ IRQ    │    │ BLE 5       │
└──────────┘    └────────┘    └──────────────┘
```

- **优点**: NuttX 上游 `common/espressif` 已有 `esp_wlan.c` 驱动支持 ESP-Hosted
- **连接方式**: SPI（简单）或 SDIO（高吞吐）
- **EV Board 实现**: 优先验证板载 ESP32-C6 与 P4 的既有连接；如采用
  ESP-Hosted，还需确认协处理器固件、传输接口和 NuttX 驱动兼容性

#### 方案 B：SPI WiFi 外接模块

外接通用 SPI WiFi 模块（如 ESP32-C3 模组），使用 AT 指令集。实现较简单但性能受限。

#### 方案 C：仅使用以太网（开发阶段）

EV Board 自带 RJ45 以太网口，上游 `webpanel` defconfig 已验证 EMAC 驱动可用。
开发阶段可先用以太网调试 WiFi 无关功能，降低初期适配风险。

**建议路径**: Phase 1 用以太网 → Phase 2 接入 ESP-Hosted WiFi。

### 3.3 板载音频方案

Function EV Board 带板载麦克风、ES8311 音频编解码器和功放/扬声器链路。
因此音频不是“需外接 codec”的功能，而是一个独立的板级 bring-up 项：

1. 验证 I2C 控制总线和 ES8311 响应；
2. 按实际原理图/BSP 配置 I2S 的 BCLK、MCLK、LRCK、DOUT 和 DIN；
3. 使用 NuttX 已有 ES8311 lower-half 注册播放和录音设备；
4. 用固定采样率、声道数和位宽完成录放音测试后，才允许应用使用音频设备。

当前项目中的通用 I2S `pcm_in0` 注册只能证明 I2S 总线适配路径，不能
替代 codec 初始化或板载麦克风验证。具体引脚和 I2C 地址必须以目标板
版本的原理图或乐鑫 BSP 为准。

### 3.4 LCD/触摸屏方案

EV Board 的 MIPI-DSI 显示屏需自行适配驱动：

| 组件 | 方案 |
|------|------|
| **显示接口** | MIPI-DSI (4-lane)，通过 `common/espressif/esp_mipi.c` 驱动 |
| **LCD 面板** | 根据 EV Board 实际面板型号配置初始化序列（ILI/LG/BOE 等） |
| **触摸屏** | I2C 电容触摸（常见 GT911/FT5x06 系列），通过 `esp_i2c.c` 驱动 |
| **LVGL 渲染** | 使用 `CONFIG_LV_USE_NUTTX_LCD` + `CONFIG_LV_USE_NUTTX_TOUCHSCREEN` |
| **分辨率适配** | SmartHome UI 从 320×240 → 1024×600，需重新设计布局 |
| **DMA 刷新** | 使用 GDMA 或 2D-DMA 加速帧传输 |

---

## 四、适配步骤详解

### Phase 1：工具链与构建系统

#### 4.1.1 工具链准备

ESP32-P4 使用 RISC-V 架构（RV32IMAFC），有以下工具链选择：

| 工具链 | 来源 | 路径 |
|--------|------|------|
| `riscv-none-elf-gcc` | OpenVela 预编译 | `prebuilts/gcc/linux-x86_64/riscv-none-elf/` |
| `riscv32-esp-elf-gcc` | 乐鑫官方 | 需自行下载或通过 ESP-IDF 安装 |

**推荐**: 优先使用 OpenVela 已有的 `riscv-none-elf` 工具链。如果遇到 ABI 或指令集兼容性问题，再切换到乐鑫官方工具链。

验证工具链可用性：
```bash
# 检查现有 RISC-V 工具链
ls prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-gcc --version

# 如果需要乐鑫工具链，从以下地址获取:
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/
# 或通过: idf_tools.py install riscv32-esp-elf
```

#### 4.1.2 从上游 NuttX 合入 ESP32-P4 支持

需要从 Apache NuttX `master` 分支合入以下目录/文件：

```bash
# 1. 芯片层 (arch)
git checkout upstream/master -- arch/risc-v/src/esp32p4/
git checkout upstream/master -- arch/risc-v/src/common/espressif/

# 2. 板级层 (boards)
git checkout upstream/master -- boards/risc-v/esp32p4/

# 3. 工具链配置
git checkout upstream/master -- tools/esp32p4/          # 如果存在
git checkout upstream/master -- arch/risc-v/src/esp32p4/hal_esp32p4.cmake

# 4. 处理冲突
# openvela 的 common/espressif 可能有自己的修改，需手动合并
```

**注意**: OpenVela 的 `common/espressif/` 可能包含乐鑫厂商的定制修改（如 BLE 协议栈、私有 WiFi 驱动等），合入时需仔细处理冲突。

#### 4.1.3 添加 ESP32-P4 工具链到 manifest

在 `contest2026_031_niudanxianqianchong.xml` 中添加工具链声明（如需乐鑫专用工具链）：

```xml
<!-- ESP32-P4 RISC-V 工具链（如果使用乐鑫专用版本） -->
<project path="prebuilts/gcc/linux-x86_64/riscv32-esp-elf"
         name="openvela-toolchain-external/prebuilts_gcc_linux-x86_64_riscv32-esp-elf"
         remote="git" revision="main" clone-depth="1"
         groups="notdefault,platform-linux"/>
```

#### 4.1.4 添加板级 linkfile 到 manifest

在 `contest2026_031_niudanxianqianchong.xml` 中添加 EV Board 的 linkfile：

```xml
<!-- ESP32-P4 EV Board 板级适配 -->
<linkfile src="board/esp32p4/esp32p4-function-ev-board" dest="vendor/espressif/boards/esp32p4/esp32p4-function-ev-board"/>
```

这会将本项目的 `board/esp32p4/esp32p4-function-ev-board/` 映射到构建系统的 vendor 板级目录。

---

### Phase 2：Board 层适配

#### 4.2.1 使用 Custom Board 模式

由于上游已有 `esp32p4-function-ev-board`，采用 `CONFIG_ARCH_BOARD_CUSTOM=y` 模式在本项目中覆盖：

**目录结构**:

```
board/esp32p4/esp32p4-function-ev-board/
├── Kconfig                              # 板级配置选项
├── CMakeLists.txt                       # CMake 构建规则
├── include/
│   └── board.h                          # 板级硬件定义
├── src/
│   ├── CMakeLists.txt                   # 源文件列表
│   ├── Make.defs                        # Make 构建规则
│   ├── esp32p4_boot.c                   # 板级启动初始化
│   ├── esp32p4_bringup.c                # 外设 bringup
│   ├── esp32p4_board_mipi_lcd.c         # MIPI-DSI LCD 驱动
│   ├── esp32p4_board_touch_gt911.c      # GT911 触摸屏驱动
│   └── esp32p4_appinit.c                # 应用初始化
├── scripts/
│   └── Make.defs                        # 工具链 & 链接脚本
└── configs/
    ├── nsh/
    │   └── defconfig                    # 最小 NSH 配置
    └── smart_home/
        └── defconfig                    # ★ SmartHome 应用配置
```

#### 4.2.2 Kconfig — 板级配置

```kconfig
# board/esp32p4/esp32p4-function-ev-board/Kconfig

if ARCH_BOARD_ESP32P4_FUNCTION_EV_BOARD

config ESP32P4_EV_MIPI_LCD
    bool "Enable MIPI-DSI LCD"
    default y
    depends on ESPRESSIF_LCD_MIPI
    ---help---
        Enable the MIPI-DSI LCD display on the ESP32-P4 EV Board.

config ESP32P4_EV_LCD_HRES
    int "LCD horizontal resolution"
    default 1024
    depends on ESP32P4_EV_MIPI_LCD

config ESP32P4_EV_LCD_VRES
    int "LCD vertical resolution"
    default 600
    depends on ESP32P4_EV_MIPI_LCD

config ESP32P4_EV_TOUCHSCREEN
    bool "Enable capacitive touchscreen"
    default y
    depends on ESPRESSIF_I2C
    ---help---
        Enable the I2C capacitive touchscreen on the EV Board.

config ESP32P4_EV_TOUCH_I2C_ADDR
    hex "Touch controller I2C address"
    default 0x5D
    depends on ESP32P4_EV_TOUCHSCREEN

endif # ARCH_BOARD_ESP32P4_FUNCTION_EV_BOARD
```

#### 4.2.3 defconfig — SmartHome 应用配置

以下为 SmartHome 应用的完整 defconfig，基于上游 `webpanel` 配置扩展：

```ini
# board/esp32p4/esp32p4-function-ev-board/configs/smart_home/defconfig

# ========== 架构 ==========
CONFIG_ARCH="risc-v"
CONFIG_ARCH_BOARD="esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_COMMON=y
CONFIG_ARCH_BOARD_ESP32P4_FUNCTION_EV_BOARD=y
CONFIG_ARCH_BOARD_CUSTOM=y
CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/espressif/boards/esp32p4/esp32p4-function-ev-board"
CONFIG_ARCH_CHIP="esp32p4"
CONFIG_ARCH_CHIP_ESP32P4=y
CONFIG_ARCH_RISCV=y
CONFIG_ARCH_INTERRUPTSTACK=2048
CONFIG_ARCH_IRQ_TO_NDX=y
CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMIC=y
CONFIG_ARCH_NUSER_INTERRUPTS=17
CONFIG_BOARD_LOOPSPERMSEC=15000

# ========== 调试 ==========
CONFIG_DEBUG_ASSERTIONS=y
CONFIG_DEBUG_ASSERTIONS_EXPRESSION=y
CONFIG_DEBUG_FEATURES=y
CONFIG_DEBUG_FULLOPT=y
CONFIG_DEBUG_SYMBOLS=y
CONFIG_SCHED_BACKTRACE=y

# ========== 调度 ==========
CONFIG_RR_INTERVAL=200
CONFIG_SCHED_WAITPID=y
CONFIG_IDLETHREAD_STACKSIZE=4096
CONFIG_DEFAULT_TASK_STACKSIZE=4096
CONFIG_INIT_ENTRYPOINT="nsh_main"
CONFIG_INIT_STACKSIZE=8192
CONFIG_PREALLOC_TIMERS=4

# ========== 内存 ==========
CONFIG_MM_REGIONS=2
CONFIG_ESPRESSIF_SPIRAM=y
CONFIG_ESPRESSIF_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
CONFIG_ESPRESSIF_FLASH_16M=y
CONFIG_ESPRESSIF_FLASH_MODE_QIO=y

# ========== ESP32-P4 缓存 ==========
CONFIG_ESPRESSIF_CACHE_L2_CACHE_256KB=y

# ========== GPIO & 外设 ==========
CONFIG_ESPRESSIF_GPIO_IRQ=y
CONFIG_ESPRESSIF_LEDC=y
CONFIG_ESPRESSIF_LEDC_CHANNEL0_PIN=6
CONFIG_ESPRESSIF_LEDC_TIMER0=y
CONFIG_ESPRESSIF_MERGE_BINS=y

# ========== SPI Flash 文件系统 ==========
CONFIG_ESPRESSIF_SPIFLASH=y
CONFIG_ESPRESSIF_SPIFLASH_FS_MOUNT_PT="/mnt"
CONFIG_ESPRESSIF_SPIFLASH_SMARTFS=y
CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET=0xFCA000
CONFIG_ESPRESSIF_STORAGE_MTD_SIZE=0x36000

# ========== LCD (MIPI-DSI) ==========
CONFIG_ESPRESSIF_LCD_MIPI=y
CONFIG_ESP32P4_EV_MIPI_LCD=y
CONFIG_ESP32P4_EV_LCD_HRES=1024
CONFIG_ESP32P4_EV_LCD_VRES=600

# ========== 触摸屏 (I2C) ==========
CONFIG_ESPRESSIF_I2C=y
CONFIG_ESP32P4_EV_TOUCHSCREEN=y
CONFIG_ESP32P4_EV_TOUCH_I2C_ADDR=0x5D

# ========== 网络 (以太网 — Phase 1) ==========
CONFIG_ESPRESSIF_EMAC=y
CONFIG_ESPRESSIF_ETH_DMA_BUFFER_SIZE=512
CONFIG_NET_BROADCAST=y
CONFIG_NET_ETH_PKTSIZE=1514
CONFIG_NET_ICMP_SOCKET=y
CONFIG_NET_IGMP=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_TCP_WRITE_BUFFERS=y
CONFIG_NET_TCP_DELAYED_ACK=y
CONFIG_NET_TCP_SELECTIVE_ACK=y
CONFIG_NET_UDP_WRITE_BUFFERS=y
CONFIG_NET_LOCAL=y
CONFIG_NET_LOCAL_SCM=y
CONFIG_NET_LOOPBACK=y
CONFIG_NET_NETLINK=y
CONFIG_NETLINK_ROUTE=y
CONFIG_NETDB_DNSCLIENT=y
CONFIG_NETDB_DNSCLIENT_RECV_TIMEOUT=2
CONFIG_NETDB_DNSCLIENT_RETRIES=8
CONFIG_NETDEV_LATEINIT=y
CONFIG_NETDEV_PHY_IOCTL=y
CONFIG_NETDEV_STATISTICS=y
CONFIG_NETINIT_DHCPC=y
CONFIG_NETINIT_THREAD=y

# ========== TLS/HTTPS ==========
CONFIG_TLS_NELEM=4
CONFIG_TLS_TASK_NELEM=4

# ========== LVGL 图形界面 ==========
CONFIG_GRAPHICS_LVGL=y
CONFIG_LV_USE_NUTTX=y
CONFIG_LV_USE_NUTTX_LCD=y
CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_USE_CLIB_SPRINTF=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_FONT_MONTSERRAT_20=y
# 如需中文支持:
# CONFIG_LV_USE_FREETYPE=y

# ========== 工具库 ==========
CONFIG_LIBUV=y
CONFIG_NETUTILS_CJSON=y
CONFIG_NETUTILS_CODECS=y
CONFIG_NETUTILS_IPERF=y
CONFIG_NETUTILS_MQTTC=y
CONFIG_NETUTILS_WEBCLIENT=y
CONFIG_SYSTEM_FLATBUFFERS=y
CONFIG_LIBCXX=y
CONFIG_CXX_STANDARD="gnu++17"

# ========== 文件系统 ==========
CONFIG_FS_PROCFS=y
CONFIG_FS_LARGEFILE=y
CONFIG_NAME_MAX=48
CONFIG_LINE_MAX=64

# ========== Builtin Apps ==========
CONFIG_BUILTIN=y
CONFIG_NSH_BUILTIN_APPS=y
CONFIG_NSH_READLINE=y
CONFIG_NSH_FILEIOSIZE=512
CONFIG_SYSTEM_NSH=y
CONFIG_SYSTEM_PING=y
CONFIG_SYSTEM_PING_STACKSIZE=3072
CONFIG_SYSTEM_DHCPC_RENEW=y

# ========== SmartHome App ==========
CONFIG_DEMOS_SMART_HOME=y

# ========== 其他 ==========
CONFIG_BOARDCTL_RESET=y
CONFIG_INTELHEX_BINARY=y
CONFIG_START_DAY=29
CONFIG_START_MONTH=11
CONFIG_START_YEAR=2019
CONFIG_IOB_NBUFFERS=124
CONFIG_IOB_THROTTLE=24

# 禁用项
# CONFIG_NSH_ARGCAT is not set
# CONFIG_NSH_CMDOPT_HEXDUMP is not set
```

#### 4.2.4 板级源码 — esp32p4_boot.c

```c
/****************************************************************************
 * board/esp32p4/esp32p4-function-ev-board/src/esp32p4_boot.c
 *
 * ESP32-P4 Function EV Board 板级启动初始化
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <arch/board/board.h>

#include "esp32p4_ev_board.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   板级晚期初始化，在 OS 初始化完成后调用。
 *   此处进行外设 bringup。
 *
 ****************************************************************************/

int board_late_initialize(void)
{
#ifdef CONFIG_BOARD_LATE_INITIALIZE
  /* Bring up board peripherals */

  esp32p4_bringup();
#endif

  return OK;
}
```

#### 4.2.5 板级源码 — esp32p4_bringup.c

```c
/****************************************************************************
 * board/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c
 *
 * ESP32-P4 EV Board 外设 bringup
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <syslog.h>

#ifdef CONFIG_ESP32P4_EV_MIPI_LCD
#  include "esp32p4_board_mipi_lcd.h"
#endif

#ifdef CONFIG_ESP32P4_EV_TOUCHSCREEN
#  include "esp32p4_board_touch_gt911.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp32p4_bringup(void)
{
  int ret = OK;

#ifdef CONFIG_ESP32P4_EV_MIPI_LCD
  /* 初始化 MIPI-DSI LCD 显示屏 */

  ret = esp32p4_mipi_lcd_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize MIPI LCD: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESP32P4_EV_TOUCHSCREEN
  /* 初始化 I2C 触摸屏 */

  ret = esp32p4_touch_gt911_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize touchscreen: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_SPIFLASH
  /* 挂载 SPI Flash 文件系统 */

  ret = esp_spiflash_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init SPI flash: %d\n", ret);
    }
#endif

  return ret;
}
```

#### 4.2.6 LCD 驱动 — esp32p4_board_mipi_lcd.c

```c
/****************************************************************************
 * board/esp32p4/esp32p4-function-ev-board/src/esp32p4_board_mipi_lcd.c
 *
 * MIPI-DSI LCD 驱动适配
 * 需根据 EV Board 实际面板型号调整初始化序列
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/mipi_dsi.h>
#include <syslog.h>

#include "hardware/esp32p4_mipi_dsi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LCD 面板参数 — 根据 EV Board 实际屏幕调整 */

#define LCD_HRES           CONFIG_ESP32P4_EV_LCD_HRES  /* 1024 */
#define LCD_VRES           CONFIG_ESP32P4_EV_LCD_VRES  /* 600  */
#define LCD_LANE_MBPS      1000   /* DSI 数据速率 Mbps */
#define LCD_LANE_COUNT      4     /* DSI 通道数 */

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* MIPI-DSI 初始化命令序列
 * 需根据实际 LCD 面板 IC 型号填充
 * 示例为通用 ILI 系列面板，实际使用时替换
 */

static const struct mipi_dsi_lcd_cmd_s g_lcd_init_cmds[] =
{
  /* 此处省略具体面板初始化序列
   * 需从面板 datasheet 或厂商提供的初始化代码中获取
   * 格式: { command_type, data_or_reg, delay_ms }
   */
  { 0, 0, 0 }  /* Terminator */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp32p4_mipi_lcd_initialize(void)
{
  struct mipi_dsi_device_s *dsi;
  struct lcd_dev_s *lcd;
  int ret;

  syslog(LOG_INFO, "Initializing MIPI-DSI LCD: %dx%d\n", LCD_HRES, LCD_VRES);

  /* 1. 创建 MIPI-DSI 设备 */

  dsi = esp_mipi_dsi_initialize(0 /* DSI port */);
  if (dsi == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to init MIPI-DSI\n");
      return -ENODEV;
    }

  /* 2. 发送面板初始化命令 */

  ret = mipi_dsi_lcd_send_cmds(dsi, g_lcd_init_cmds,
                                sizeof(g_lcd_init_cmds) /
                                sizeof(g_lcd_init_cmds[0]));
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to send LCD init cmds: %d\n", ret);
      return ret;
    }

  /* 3. 注册 LCD 设备到 NuttX */

  lcd = mipi_dsi_lcd_getdev(dsi);
  if (lcd == NULL)
    {
      syslog(LOG_ERR, "ERROR: Failed to get LCD device\n");
      return -ENODEV;
    }

  /* 注册为 /dev/lcd0 */

  ret = lcd_register(0, lcd);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: lcd_register failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "MIPI-DSI LCD registered as /dev/lcd0\n");
  return OK;
}
```

---

### Phase 3：WiFi 适配

#### 4.3.1 ESP-Hosted 伴侣芯片接线

EV Board 通过 SPI 或 SDIO 连接 ESP32-C6 模组：

```
ESP32-P4 EV Board                    ESP32-C6 Module
┌──────────────┐                    ┌──────────────┐
│ SPI2_CLK  ───┼────────────────────┼─ SPI_CLK     │
│ SPI2_MOSI ───┼────────────────────┼─ SPI_MOSI    │
│ SPI2_MISO ───┼────────────────────┼─ SPI_MISO    │
│ GPIO_CS   ───┼────────────────────┼─ SPI_CS      │
│ GPIO_IRQ  ───┼────────────────────┼─ GPIO_READY  │
│ GPIO_RST  ───┼────────────────────┼─ CHIP_PU     │
│ 3.3V      ───┼────────────────────┼─ 3V3         │
│ GND       ───┼────────────────────┼─ GND         │
└──────────────┘                    └──────────────┘
```

#### 4.3.2 伴侣芯片固件

ESP32-C6 需刷入 ESP-Hosted Slave 固件：

```bash
# 获取 ESP-Hosted 固件
git clone https://github.com/espressif/esp-hosted.git
cd esp-hosted/esp_hosted_fg/esp/esp_driver/network_adapter

# 编译 ESP32-C6 Slave 固件
idf.py set-target esp32c6
idf.py build

# 烧录到 ESP32-C6
esptool.py --chip esp32c6 --port /dev/ttyUSB1 \
  write_flash 0x0 build/esp_hosted_network_adapter.bin
```

#### 4.3.3 NuttX WiFi 驱动配置

在 defconfig 中启用 WiFi：

```ini
# ========== WiFi (ESP-Hosted, Phase 2) ==========
CONFIG_ESPRESSIF_WIFI=y
CONFIG_ESPRESSIF_WIFI_SPI=y
CONFIG_ESP_HOSTED=y
CONFIG_ESP_HOSTED_SPI_FREQ=20000000

# 使用 ESP32-C6 的 WiFi 引脚定义
CONFIG_ESP_HOSTED_SPI_CS_PIN=<GPIO_CS_PIN>
CONFIG_ESP_HOSTED_SPI_HANDSHAKE_PIN=<GPIO_IRQ_PIN>
CONFIG_ESP_HOSTED_SPI_DATA_READY_PIN=<GPIO_READY_PIN>

# WiFi 网络栈
CONFIG_DRIVERS_IEEE80211=y
CONFIG_DRIVERS_WIRELESS=y
CONFIG_WIRELESS=y
CONFIG_WIRELESS_WAPI=y
CONFIG_WIRELESS_WAPI_CMDTOOL=y
CONFIG_WIRELESS_WAPI_INITCONF=y
CONFIG_WIRELESS_WAPI_STACKSIZE=8192
CONFIG_NETUTILS_DHCPD=y
```

#### 4.3.4 SmartHome WiFi 适配

修改 `smart_home_wifi.c`，增加 ESP32-P4 平台适配：

```c
/* demos/smart_home/src/net/smart_home_wifi.c */

#ifdef CONFIG_ARCH_CHIP_ESP32P4
  /* ESP32-P4 使用 ESP-Hosted 连接伴侣芯片的 WiFi */

  /* 步骤 1: 确认 ESP-Hosted 连接就绪 */
  /* 步骤 2: 使用标准 WAPI 接口连接 AP */
  /* 步骤 3: DHCP 获取 IP */

  /* WiFi 驱动设备路径可能与 ESP32-S3 不同:
   * ESP32-S3: wlan0
   * ESP32-P4 (ESP-Hosted): espsta0 或 wlan0
   * 需要确认实际设备路径
   */
#endif
```

---

### Phase 4：SmartHome 应用适配

#### 4.4.1 LVGL UI 分辨率适配

从 320×240 → 1024×600，需要重新设计布局参数：

```c
/* demos/smart_home/src/ui/lvgl/smart_home_lvgl.c */

#ifdef CONFIG_ESP32P4_EV_MIPI_LCD
#  define SCREEN_WIDTH   1024
#  define SCREEN_HEIGHT  600
#  define FONT_SIZE       24     /* 更大字体 */
#  define ICON_SIZE       48     /* 更大图标 */
#  define CARD_COLS        3     /* 3 列卡片（原 2 列） */
#  define CARD_ROWS        3     /* 3 行卡片（原 2 行） */
#else
#  define SCREEN_WIDTH    320
#  define SCREEN_HEIGHT   240
#  define FONT_SIZE        20
#  define ICON_SIZE        32
#  define CARD_COLS         2
#  define CARD_ROWS         2
#endif
```

#### 4.4.2 中文字体适配

1024×600 分辨率下可使用更大的中文字体，提升可读性：

```c
/* 在 defconfig 中启用 FreeType 支持更大的中文渲染 */
CONFIG_LV_USE_FREETYPE=y
CONFIG_LV_FONT_MONTSERRAT_28=y
```

#### 4.4.3 cAGENT 框架适配

cAGENT 框架与硬件平台**基本解耦**，仅需关注：

1. **栈大小**: ESP32-P4 内存更充裕，可适当增大 Agent 任务栈：
   ```c
   #define CAGENT_AGENT_TASK_STACKSIZE  16384  /* 原 8192 */
   ```

2. **JSON 缓冲区**: 可增大 LLM 响应缓冲区：
   ```c
   #define CAGENT_LLM_RESPONSE_BUF_SIZE  8192  /* 原 4096 */
   ```

3. **无其他修改**: HTTP/TLS 通信、JSON 解析、工具/技能注册等逻辑与平台无关。

#### 4.4.4 LittleFS 数据镜像制作

参考 ESP32-S3 的 `make_box3_littlefs_data_image.sh`，创建 EV Board 版本：

```bash
# scripts/make_evboard_littlefs_data_image.sh

#!/bin/bash
# 制作 EV Board 的 LittleFS 数据镜像
# 包含: 字体、图标、技能文件

OUTPUT=data_evboard.img
MOUNT=/tmp/littlefs_mount
SIZE=0x400000  # 4MB (EV Board 有更大 Flash)

# 创建文件系统镜像
mkdir -p ${MOUNT}
cp -r demos/smart_home/res/fonts/* ${MOUNT}/
cp -r demos/smart_home/res/icons/* ${MOUNT}/
cp -r demos/smart_home/res/skills/* ${MOUNT}/

# 使用 mklittlefs 制作镜像
mklittlefs -c ${MOUNT} -s ${SIZE} ${OUTPUT}
```

---

### Phase 5：构建与烧录

#### 4.5.1 构建命令

```bash
# 1. 初始化环境
cd /path/to/openvela
source build/envsetup.sh

# 2. 选择目标 (方式一: 直接指定路径)
lunch ../vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/smart_home

# 2. 选择目标 (方式二: 使用 build.sh)
# ./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/smart_home -j8

# 3. 编译
m -j$(nproc)

# 4. 编译产物
# out/espressif_esp32p4-function-ev-board_smart_home/nuttx.bin
```

#### 4.5.2 烧录到 EV Board

```bash
# ESP32-P4 使用 esptool.py 烧录
# 确保 esptool.py >= 4.8 (支持 ESP32-P4)

# 方法一: USB 串口烧录
esptool.py --chip esp32p4 --port /dev/ttyUSB0 \
  --baud 921600 \
  write_flash 0x0 out/.../nuttx.bin

# 方法二: USB-JTAG 烧录 (EV Board 板载 JTAG)
esptool.py --chip esp32p4 --port /dev/ttyACM0 \
  write_flash 0x0 out/.../nuttx.bin

# 如果使用 MERGE_BINS, 烧录合并后的单一文件:
# out/.../nuttx.merged.bin
```

#### 4.5.3 验证流程

```bash
# 1. 连接串口控制台 (115200 baud)
minicom -D /dev/ttyUSB0 -b 115200

# 2. 验证 NuttX 启动
NuttShell (NSH) NuttX-12.x.x
nsh>

# 3. 验证基本外设
nsh> ls /dev
nsh> free          # 检查内存 (应看到 PSRAM)
nsh> ifconfig      # 检查以太网
nsh> ping 8.8.8.8  # 验证网络连通

# 4. 验证 LCD
nsh> fb           # 帧缓冲测试 (如果支持)

# 5. 运行 SmartHome
nsh> smart_home   # 进入交互模式
```

---

## 五、目录结构映射

### 5.1 项目文件布局

```
contest2026_031_niudanxianqianchong/
├── board/
│   └── esp32p4/
│       ├── common/                         # ESP32-P4 多板共享层
│       └── esp32p4-function-ev-board/      # ★ EV Board 板级适配
│           ├── Kconfig
│           ├── CMakeLists.txt
│           ├── include/
│           │   └── board.h
│           ├── src/
│           │   ├── esp32p4_boot.c
│           │   ├── esp32p4_bringup.c
│           │   ├── esp32p4_board_mipi_lcd.c
│           │   ├── esp32p4_board_touch_gt911.c
│           │   └── esp32p4_appinit.c
│           ├── scripts/
│           │   └── Make.defs
│           └── configs/
│               ├── nsh/defconfig
│               └── smart_home/defconfig    # ★ SmartHome 专用配置
│   │
│   ├── esp32s3-box-3/                      # ESP32-S3-BOX-3 (已有)
│   ├── esp32s3-box/                        # ESP32-S3-BOX (已有)
│   └── goldfish-arm64/                     # QEMU 模拟器 (已有)
│
├── scripts/
│   ├── make_evboard_littlefs_data_image.sh # ★ EV Board 数据镜像脚本
│   ├── fix_box3_mbedtls_*.sh               # (已有)
│   └── subset_font.sh                      # (已有)
│
├── docs/
│   ├── esp32p4-ev-board-adaptation.md      # ★ 本文档
│   ├── deliverable_preliminary.md          # (已有)
│   └── ESP32S3-BOX3-SCH.MD                # (已有)
│
├── demos/smart_home/                       # SmartHome 应用
│   └── src/
│       ├── net/smart_home_wifi.c           # 需增加 ESP32-P4 适配
│       └── ui/lvgl/smart_home_lvgl.c       # 需增加分辨率适配
│
└── contest2026_031_niudanxianqianchong.xml  # 需增加 linkfile
```

### 5.2 构建系统映射关系

```
board/esp32p4/esp32p4-function-ev-board/                           ← 项目源码
        │ (manifest linkfile)
        ▼
vendor/espressif/boards/esp32p4/           ← 构建系统可见
  esp32p4-function-ev-board/
        │ (CONFIG_ARCH_BOARD_CUSTOM_DIR)
        ▼
NuttX CMake 构建 → out/.../nuttx.bin       ← 编译产物
```

---

## 六、关键技术要点

### 6.1 ESP32-P4 芯片版本

ESP32-P4 存在重大硬件版本差异：

| 版本范围 | 说明 |
|----------|------|
| v0.0 ~ v1.x | 早期版本（<v3.0），与 v3.x 有"巨大硬件差异" |
| v3.0 ~ v3.1 | 当前生产版本（默认支持） |

在 Kconfig 中通过 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 和 `CONFIG_ESP32P4_REV_MIN` 选择。
**默认配置 v3.1**，如使用早期工程样片需手动切换。

### 6.2 L2 缓存配置

ESP32-P4 支持可配置的 L2 缓存大小：

| 选项 | 大小 | 行大小 | 适用场景 |
|------|------|--------|----------|
| `ESPRESSIF_CACHE_L2_CACHE_128KB` | 128KB | 64B | 低内存场景 |
| `ESPRESSIF_CACHE_L2_CACHE_256KB` (默认) | 256KB | 64B | 通用场景 |
| `ESPRESSIF_CACHE_L2_CACHE_512KB` | 512KB | 128B | 高性能场景 |

SmartHome 应用推荐使用 **256KB**（默认）或 **512KB**（如启用 LVGL 大分辨率渲染）。

### 6.3 双核利用策略

ESP32-P4 的 HP 双核可按以下方式分配：

```
Core 0 (HP):  NuttX 内核 + 应用任务
              ├─ SmartHome Agent (cAGENT ReAct Loop)
              ├─ LVGL UI 渲染
              └─ 网络栈 (WiFi/以太网)

Core 1 (HP):  可选的计算密集型任务
              ├─ JPEG 解码 (硬件加速)
              └─ PPA 图像处理

LP Core:      低功耗后台任务
              ├─ 环境传感器轮询
              └─ 定时器管理
```

在 defconfig 中启用 SMP：
```ini
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2
```

### 6.4 PSRAM 使用策略

ESP32-P4 的 768KB 内部 SRAM 需合理分配：

| 区域 | 用途 | 建议大小 |
|------|------|----------|
| IRAM | 代码执行 + 中断栈 | ~384KB |
| DRAM | 内核堆 + 栈 | ~256KB |
| Cache | L2 缓存 | 256KB (由硬件占用) |

PSRAM 用于：
- LVGL 帧缓冲 (1024×600×16bit = 1.2MB)
- 字体数据
- JSON 缓冲区
- 大型数组

defconfig 中通过 `CONFIG_ESPRESSIF_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`
允许 BSS 段自动放入 PSRAM。

### 6.5 Flash 布局

EV Board 使用 16MB Flash（QIO 模式），典型分区：

```
Flash 偏移      大小        用途
──────────────────────────────────
0x000000        64KB        Bootloader (二级)
0x010000        ~4MB        NuttX 固件 (nuttx.bin)
0x410000        ~2MB        LittleFS 数据分区 (字体/图标/技能)
0x610000        ~10MB       SmartFS 文件系统 (/mnt)
0xFCA000        216KB       NVS 存储
0xFFC000        16KB        eFuse 镜像
```

在 defconfig 中通过以下配置控制：
```ini
CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET=0xFCA000
CONFIG_ESPRESSIF_STORAGE_MTD_SIZE=0x36000
```

---

## 七、已知限制与风险

### 7.1 技术风险

| 风险项 | 严重度 | 缓解措施 |
|--------|--------|----------|
| OpenVela NuttX 未合入 ESP32-P4 | 高 | 从上游 cherry-pick，或手动移植关键文件 |
| `common/espressif/` 冲突 | 高 | 仔细比对 OpenVela 定制修改，手动合并 |
| 板载 C6/ESP-Hosted 驱动兼容性 | 高 | 先验证以太网；需要无线时确认 C6 固件、传输总线和 NuttX 驱动 |
| ES8311 音频 codec bring-up | 中 | 先验证 I2C/I2S 和设备节点，再进行录放音质量测试 |
| MIPI-DSI LCD 初始化序列 | 中 | 从 EV Board 官方示例或乐鑫 FAE 获取 |
| 触摸屏 IC 型号不确定 | 中 | 拆机确认 IC，或使用通用 I2C 触摸扫描 |
| 工具链 ABI 兼容性 | 低 | RISC-V 工具链通用性较好 |

### 7.2 功能限制

- **无线功能依赖板载 C6**：P4 本身不带 Wi-Fi/BLE；是否接入 ESP-Hosted
  取决于板载 C6 固件和 NuttX 支持状态
- **板载音频仍需软件适配**：ES8311 和麦克风已在板上，但当前通用 I2S
  初始化不等同于 codec 驱动和录音可用
- **屏幕尺寸变化**：UI 布局需重新设计，不是简单缩放
- **功耗较高**：400MHz 双核 + 大屏，功耗远高于 ESP32-S3

### 7.3 后续优化方向

1. **SMP 调度优化**：利用双核将 LVGL 渲染和 cAGENT 推理分配到不同核心
2. **硬件加速**：利用 PPA 和 2D-DMA 加速 GUI 渲染
3. **JPEG 硬解码**：利用 H.264 编码器加速图像处理
4. **LP Core 低功耗**：将传感器轮询放到 LP Core，主核可休眠
5. **MIPI-CSI 摄像头**：利用摄像头接口实现视觉 AI 功能

---

## 八、参考资源

### 8.1 官方文档

| 资源 | 链接 |
|------|------|
| ESP32-P4 技术参考手册 | https://www.espressif.com/en/products/socs/esp32-p4 |
| ESP-IDF ESP32-P4 编程指南 | https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/ |
| ESP-Hosted 框架 | https://github.com/espressif/esp-hosted |
| ESP32-P4 Function EV Board 用户指南 | https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html |
| ESP32-P4 Function EV Board BSP 引脚定义 | https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h |
| NuttX ESP32-P4 板级支持 | https://github.com/apache/nuttx/tree/master/boards/risc-v/esp32p4 |
| NuttX ESP32-P4 Arch 层 | https://github.com/apache/nuttx/tree/master/arch/risc-v/src/esp32p4 |
| NuttX Common Espressif 驱动 | https://github.com/apache/nuttx/tree/master/arch/risc-v/src/common/espressif |

### 8.2 项目内参考

| 文件 | 说明 |
|------|------|
| [esp32s3-box defconfig](../board/esp32s3-box/configs/audio_event/defconfig) | ESP32-S3 参考配置 |
| [contest_board 骨架](../board/contest_board/) | 板级适配骨架样例 |
| [deliverable_preliminary.md](./deliverable_preliminary.md) | 项目整体说明 |
| [ESP32S3-BOX3-SCH.MD](./ESP32S3-BOX3-SCH.MD) | ESP32-S3-BOX-3 原理图参考 |

### 8.3 工具链

| 工具 | 说明 |
|------|------|
| `riscv-none-elf-gcc` | OpenVela 预编译 RISC-V 工具链 |
| `esptool.py` >= 4.8 | 乐鑫烧录工具 (需支持 ESP32-P4) |
| `mklittlefs` | LittleFS 镜像制作工具 |
| `menuconfig` | NuttX 图形化配置工具 |

---

## 九、里程碑与排期建议

| 阶段 | 内容 | 预计耗时 | 交付物 |
|------|------|----------|--------|
| **M1** | 工具链 + 构建系统就绪 | 2-3 天 | `nsh` defconfig 编译通过 |
| **M2** | NuttX 在 EV Board 上 boot | 3-5 天 | NSH shell 可交互 |
| **M3** | 以太网 + 文件系统 | 2-3 天 | ping + /mnt 可用 |
| **M4** | 板载音频 | 2-4 天 | ES8311 录放音设备可用 |
| **M5** | LCD + 触摸屏驱动 | 5-7 天 | framebuffer/LVGL 基础渲染可用 |
| **M6** | 板载 C6 / ESP-Hosted（可选） | 5-7 天 | 无线联网可用 |
| **M7** | 应用迁移（可选） | 3-5 天 | 选定应用可运行 |
| **总计** | | **以板级验收为准，不以单一应用作为前置条件** |

---

## 十、当前实际迁移任务手册

> 本章记录 2026-07-04 已经执行过的 Route A 迁移进度，以及后续继续操作的方法。
> 前文偏“方案设计”，本章偏“可复现任务清单”。
> 文中“当前”均需以构建前的实际检查为准；不要仅凭历史构建日志判断。

### 10.1 当前完成状态

当前结论：**ESP32-P4 Function EV Board 的 Route A 移植骨架已经落到 contest 项目目录，并且构建已经进入实际源码编译阶段；但还没有取得最终 build pass。**

已完成：

| 项目 | 状态 | 说明 |
|------|------|------|
| 从上游 NuttX 迁入 ESP32-P4 芯片层 | 已完成 | 放在 `contest2026_031_niudanxianqianchong/chips/esp32p4/` |
| 从上游 NuttX 迁入 EV Board 板级层 | 已完成 | 放在 `contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/` |
| 从上游 NuttX 迁入 ESP32-P4 board common 层 | 已完成 | 放在 `contest2026_031_niudanxianqianchong/board/esp32p4/common/` |
| 改造 35 个 defconfig 为 Route A custom 模式 | 已完成 | 使用 `ARCH_CHIP_CUSTOM` 和 `ARCH_BOARD_CUSTOM` |
| 打通 openvela 配置阶段 | 基本完成 | 已能生成 chip/board 链接 |
| 打通 RISC-V 交叉工具链选择 | 已完成 | 补充 `CONFIG_RISCV_TOOLCHAIN_GNU_RV32=y` |
| 拉取 Espressif HAL 依赖 | 已验证可进行 | 需要联网访问 GitHub |
| 最小 `nsh` 固件完整编译通过 | 未完成 | 上一次构建被中断，尚未拿到最终结果 |
| 板上启动、烧录、串口 NSH 验证 | 未完成 | 需要先完成固件编译 |
| 板载 ES8311、麦克风与扬声器 | 未完成 | 需补 codec board glue，通用 I2S 不足以验证板载音频 |
| LCD/MIPI-DSI、触摸屏 | 未完成 | 属于 boot 之后的板级外设阶段 |
| 板载 C6 / ESP-Hosted | 未完成 | P4 无内置 Wi-Fi；需验证本板 C6 固件和互联路径 |
| 应用迁移 | 未完成 | SmartHome、audio_event 等均在板级能力完成后再选择 |

### 10.2 当前目录结构

当前采用 **Route A：项目内 custom chip + custom board**。也就是说，不直接把 ESP32-P4 代码合进 openvela 的 `nuttx/arch/risc-v/src/esp32p4` 和 `nuttx/boards/risc-v/esp32p4`，而是放在比赛项目自己的目录中。

#### 构建前映射检查

manifest 期望把项目内 P4 源码链接到以下 vendor 位置：

```text
chips/esp32p4 -> vendor/espressif/chips/esp32p4
board/esp32p4/common -> vendor/espressif/boards/esp32p4/common
board/esp32p4/esp32p4-function-ev-board
  -> vendor/espressif/boards/esp32p4/esp32p4-function-ev-board
```

在复用已有工作区时，vendor 或 `apps/examples` 链接可能被其他项目覆盖。
构建前必须执行 `readlink -f` 检查实际目标；只有目标与上述项目目录一致时，
通过 vendor 路径选择配置才可以视为在验证本项目 P4 代码。若 defconfig 使用
`ARCH_*_CUSTOM_DIR` 直接指向 contest 目录，也仍应确认 chip、board 和应用
三者来自同一份工作树。

当前关键目录：

```text
contest2026_031_niudanxianqianchong/
├── chips/
│   └── esp32p4/
│       ├── Kconfig
│       ├── Make.defs
│       ├── CMakeLists.txt
│       ├── include/
│       └── common/
│           └── espressif/
├── board/
│   └── esp32p4/
│       ├── common/
│       │   ├── include/
│       │   ├── scripts/
│       │   └── src/
│       └── esp32p4-function-ev-board/
│           ├── Kconfig
│           ├── configs/
│           ├── include/
│           ├── scripts/
│           └── src/
└── docs/
    └── esp32p4-ev-board-adaptation.md
```

构建配置后，openvela 会在 `nuttx/arch/risc-v/src/` 下建立链接：

```text
nuttx/arch/risc-v/src/chip  -> contest.../chips/esp32p4
nuttx/arch/risc-v/src/board -> contest.../board/esp32p4/common
nuttx/arch/risc-v/src/board/board -> contest.../board/esp32p4/esp32p4-function-ev-board/src
```

这些链接是 Route A 是否接通的第一验收点。

### 10.3 为什么选择 Route A

openvela 的芯片/板级移植有两种常见路线：

| 路线 | 做法 | 优点 | 缺点 |
|------|------|------|------|
| Route A | 在项目目录提供 custom chip/custom board | 适合比赛项目，改动集中，不污染 openvela 主树 | 需要自己处理 Kconfig、Make.defs、路径兼容 |
| Route B | 直接把芯片和板级合入 openvela `nuttx/arch`、`nuttx/boards` 或 `vendor` | 更像正式 upstream port | 改动面大，冲突更多，比赛项目维护成本高 |

本项目选择 Route A 的原因：

1. ESP32-P4 还没有在当前 openvela 分支中原生存在。
2. 比赛项目需要快速验证，不适合先做大范围 openvela 主树合并。
3. openvela 的 custom chip/custom board 机制本来就是给项目级移植准备的。
4. 后续如果验证稳定，再考虑把 P4 支持整理成 openvela vendor/chip 正式目录。

### 10.4 如果从头复现，应该怎么操作

以下步骤假设：

- openvela 工作目录是 `/home/arongw/openvela`
- 上游 NuttX 已经克隆在 `~/nuttx`
- 当前使用上游 NuttX 的 ESP32-P4 支持作为移植来源

#### Step 1：确认上游 NuttX 已有 ESP32-P4 代码

操作：

```bash
ls ~/nuttx/arch/risc-v/src/esp32p4
ls ~/nuttx/arch/risc-v/include/esp32p4
ls ~/nuttx/arch/risc-v/src/common/espressif
ls ~/nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board
ls ~/nuttx/boards/risc-v/esp32p4/common
```

原因：

- `arch/risc-v/src/esp32p4` 是芯片专属层，包含 P4 启动、中断、cache、芯片版本等代码。
- `arch/risc-v/include/esp32p4` 是芯片寄存器和头文件。
- `arch/risc-v/src/common/espressif` 是 Espressif RISC-V 芯片共享层，P4 的 GPIO、SPI、I2C、UART、HAL 集成都依赖它。
- `boards/risc-v/esp32p4/common` 是 P4 多个板子共享的链接脚本和初始化代码。
- `esp32p4-function-ev-board` 是目标开发板的板级代码。

知识补充：

NuttX/openvela 的移植通常分三层：

```text
arch common layer    : 架构公共逻辑，例如 RISC-V trap、上下文切换
chip layer           : SoC 专属逻辑，例如 ESP32-P4 clock/cache/irq
board layer          : 开发板专属逻辑，例如引脚、外设初始化、链接脚本
```

ESP32-P4 既需要 chip layer，也需要 Espressif common layer；只复制 `esp32p4/` 目录是不够的。

#### Step 2：复制芯片层到 contest 项目

操作：

```bash
mkdir -p contest2026_031_niudanxianqianchong/chips/esp32p4
cp -a ~/nuttx/arch/risc-v/src/esp32p4/. \
  contest2026_031_niudanxianqianchong/chips/esp32p4/

cp -a ~/nuttx/arch/risc-v/include/esp32p4 \
  contest2026_031_niudanxianqianchong/chips/esp32p4/include

mkdir -p contest2026_031_niudanxianqianchong/chips/esp32p4/common
cp -a ~/nuttx/arch/risc-v/src/common/espressif \
  contest2026_031_niudanxianqianchong/chips/esp32p4/common/
```

原因：

Route A 的 custom chip 目录需要同时提供：

- 芯片源码
- 芯片头文件
- 被芯片源码 include 的 shared common 代码

如果只复制 `esp32p4` 而不复制 `common/espressif`，后续会在 HAL、GPIO、SPI、I2C、linker script 等位置失败。

#### Step 3：复制板级层到 contest 项目

操作：

```bash
mkdir -p contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board
cp -a ~/nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/. \
  contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/

mkdir -p contest2026_031_niudanxianqianchong/board/esp32p4/common
cp -a ~/nuttx/boards/risc-v/esp32p4/common/. \
  contest2026_031_niudanxianqianchong/board/esp32p4/common/
```

原因：

上游 P4 board 分成两个部分：

- `esp32p4-function-ev-board`：具体板子的 `board.h`、bringup、defconfig。
- `common`：P4 板子共用的链接脚本、common 初始化和构建文件。

openvela 配置 Route A custom board 时，`ARCH_BOARD_CUSTOM_DIR` 指向具体板子；但 P4 的 Make/CMake 还会依赖 board common，所以 common 也必须迁过来。

#### Step 4：改造 chip Kconfig

当前关键文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/Kconfig
```

目标：

- 让 openvela 在 `ARCH_CHIP_CUSTOM=y` 时识别 `ARCH_CHIP_ESP32P4`。
- 选择 RISC-V RV32 和 ESP32-P4 所需架构能力。
- 提供 `ESPRESSIF_CHIP_SERIES="esp32p4"` 和 `ESPRESSIF_SIMPLE_BOOT=y` 默认值。

关键知识：

`ARCH_CHIP_CUSTOM` 是 openvela 的“项目自定义芯片”入口。它不是上游 NuttX 原生 ESP32-P4 的普通 `ARCH_CHIP="esp32p4"` 路径，所以需要在 custom Kconfig 里主动定义和 select 相关能力。

当前已处理要点：

```kconfig
if ARCH_CHIP_CUSTOM

config ARCH_CHIP_ESP32P4
    bool
    default y
    select ARCH_RV32
    select ARCH_RV_ISA_M
    select ARCH_RV_ISA_A
    select ARCH_RV_ISA_C
    select ARCH_CHIP_ESPRESSIF
    select ARCH_MINIMAL_VECTORTABLE

if ARCH_CHIP_ESP32P4

config ESPRESSIF_CHIP_SERIES
    string
    default "esp32p4"

config ESPRESSIF_SIMPLE_BOOT
    bool
    default y
```

注意：

不要在 custom Kconfig 中重复定义 `ESPRESSIF_FLASH_16M`、`ESPRESSIF_FLASH_MODE_DIO`、`ESPRESSIF_FLASH_FREQ_80M` 这类 choice symbol。当前 openvela 的 `arch/risc-v/src/common/espressif/Kconfig` 也会定义它们，重复定义会导致配置刷新失败。

#### Step 5：改造 board Kconfig

当前关键文件：

```text
contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/Kconfig
```

目标：

让 openvela 在 `ARCH_BOARD_CUSTOM=y` 时识别当前板子：

```kconfig
if ARCH_BOARD_CUSTOM

config ARCH_BOARD_ESP32P4_FUNCTION_EV_BOARD
    bool
    default y

endif # ARCH_BOARD_CUSTOM
```

原因：

板级源码和 defconfig 中会用到 `ARCH_BOARD_ESP32P4_FUNCTION_EV_BOARD`。如果 custom board Kconfig 不提供这个 symbol，配置阶段会无法稳定选择目标板。

#### Step 6：改造 defconfig 为 Route A

当前已对 `board/esp32p4/esp32p4-function-ev-board/configs/*/defconfig` 做了统一改造。最小 `nsh` 配置中的关键项如下：

```ini
CONFIG_ARCH="risc-v"

CONFIG_ARCH_BOARD_COMMON=y
CONFIG_ARCH_BOARD_CUSTOM=y
CONFIG_ARCH_BOARD_CUSTOM_DIR="../contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_CUSTOM_DIR_RELPATH=y
CONFIG_ARCH_BOARD_CUSTOM_NAME="esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_ESP32P4_FUNCTION_EV_BOARD=y

CONFIG_ARCH_CHIP_CUSTOM=y
CONFIG_ARCH_CHIP_CUSTOM_DIR="../contest2026_031_niudanxianqianchong/chips/esp32p4"
CONFIG_ARCH_CHIP_CUSTOM_DIR_RELPATH=y
CONFIG_ARCH_CHIP_CUSTOM_NAME="esp32p4"
CONFIG_ARCH_CHIP_RISCV_CUSTOM=y
CONFIG_ARCH_CHIP_ESP32P4=y

CONFIG_ARCH_RISCV=y
CONFIG_RISCV_TOOLCHAIN_GNU_RV32=y

CONFIG_ESPRESSIF_CHIP_SERIES="esp32p4"
CONFIG_ESPRESSIF_SIMPLE_BOOT=y
CONFIG_ESPRESSIF_FLASH_16M=y
CONFIG_ESPRESSIF_FLASH_FREQ_80M=y
CONFIG_ESPRESSIF_FLASH_MODE_DIO=y
```

原因：

- `CONFIG_ARCH_CHIP_CUSTOM_DIR` 决定 `nuttx/arch/risc-v/src/chip` 链到哪里。
- `CONFIG_ARCH_BOARD_CUSTOM_DIR` 决定 `include/arch/board` 和 board source 链到哪里。
- `CONFIG_RISCV_TOOLCHAIN_GNU_RV32=y` 决定使用 RISC-V 交叉工具链。如果缺失，构建可能退回宿主 `gcc`，然后在 `UINTPTR_MAX`、`UINT32_MAX` 等目标侧宏处报错。

知识补充：

`defconfig` 不是最终 `.config`，它只是配置输入。执行 `./build.sh <config>` 后，openvela 会根据 Kconfig 解析生成 `nuttx/.config`。因此判断配置是否真的生效，要同时看：

```bash
rg -n "ARCH_CHIP_CUSTOM|ARCH_BOARD_CUSTOM|RISCV_TOOLCHAIN" nuttx/.config
```

#### Step 7：修正 Make.defs 和路径

当前涉及的关键文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/Make.defs
contest2026_031_niudanxianqianchong/chips/esp32p4/common/espressif/Make.defs
contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/scripts/Make.defs
contest2026_031_niudanxianqianchong/board/esp32p4/common/src/Make.defs
```

已经处理的关键点：

1. `chips/esp32p4/Make.defs` 使用相对路径 include common：

```make
include common/Make.defs
include chip$(DELIM)common$(DELIM)espressif$(DELIM)Make.defs
```

原因：

NuttX 的 `mkdeps` 对某些绝对 VPATH 处理不稳定。使用 `chip/common/espressif` 这种构建视角下的相对路径，更符合 NuttX Make 系统习惯。

2. `common/espressif/Make.defs` 中给 P4 HAL 补默认宏：

```make
CFLAGS += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_16M=1
CFLAGS += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_MODE_DIO=1
CFLAGS += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_FREQ_80M=1
CFLAGS += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_SIMPLE_BOOT=1
```

原因：

Espressif HAL 的部分头文件会读取这些配置宏。如果配置没有正确传入，会出现类似 flash clock invalid 的错误。这里是 Make 侧兜底，后续更理想的方式是把 P4 flash choice 与 openvela common Kconfig 更干净地融合。

3. `board/esp32p4/esp32p4-function-ev-board/scripts/Make.defs` 给 stack usage warning 补默认值：

```make
CONFIG_STACK_USAGE_WARNING ?= 0
```

原因：

`nuttx/arch/risc-v/src/common/Toolchain.defs` 中有逻辑：

```make
ifneq ($(CONFIG_STACK_USAGE_WARNING),0)
  ARCHOPTIMIZATION += -Wstack-usage=$(CONFIG_STACK_USAGE_WARNING)
endif
```

如果 `.config` 中没有这个变量，Make 会认为空值不等于 `0`，于是生成错误参数 `-Wstack-usage=`。在 board Make.defs 里补 `0`，可以把影响限制在这次 P4 Route A 移植内。

#### Step 8：修正头文件兼容

已处理：

将迁入 P4 代码中的：

```c
#include <nuttx/debug.h>
```

替换为：

```c
#include <debug.h>
```

原因：

当前 openvela 代码树中使用的是 `nuttx/include/debug.h`，不提供 `include/nuttx/debug.h`。上游 NuttX 与当前 openvela 头文件布局存在差异，所以需要兼容替换。

#### Step 9：构建最小 nsh

操作：

```bash
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

预期现象：

1. 配置阶段能刷新 `.config`。
2. 能创建 `arch/risc-v/src/chip` 和 `arch/risc-v/src/board` 链接。
3. 能拉取或复用 `chip/esp-hal-3rdparty`。
4. 进入大量 `CC:` 编译输出。
5. 最终生成 NuttX 固件产物。

当前状态：

上一次联网构建已经完成 `esp-hal-3rdparty` 拉取，并进入大量 `CC:` 编译阶段；但构建过程被中断，没有拿到最终成功或失败结果。

如果遇到网络错误：

```text
fatal: unable to access 'https://github.com/espressif/esp-hal-3rdparty.git/':
Could not resolve host: github.com
```

原因是构建需要从 GitHub 拉取 Espressif HAL 依赖。解决方法是允许构建命令联网，或预先把依赖放到构建期望的位置。

### 10.5 已解决问题清单

| 错误/现象 | 原因 | 当前处理 |
|----------|------|----------|
| `chip/hal_.mk` 找不到 | `CONFIG_ESPRESSIF_CHIP_SERIES` 为空 | 在 custom Kconfig/defconfig 中提供 `esp32p4` |
| Kconfig duplicate / choice warning | custom Kconfig 重复定义 openvela common espressif symbols | 删除 custom Kconfig 中重复 flash choice 定义 |
| `fatal error: nuttx/debug.h: No such file or directory` | 上游 NuttX 与当前 openvela 头文件路径不同 | 改为 `#include <debug.h>` |
| `mkdeps` 找不到 common/espressif 源文件 | absolute VPATH 与当前 Make 流程不兼容 | 改为 `chip/common/espressif` 相对路径 |
| `SPI timing flash clock is invalid` | HAL 未拿到 flash mode/freq/size 配置宏 | 在 Make.defs 中补 P4 flash 相关 CFLAGS |
| `clean_bootloader` target 缺失 | distclean 调用了当前 bootloader 模式下未定义的目标 | 在 Bootloader.mk 中补 fallback target |
| `gcc: error: missing argument to '-Wstack-usage='` | `CONFIG_STACK_USAGE_WARNING` 没有默认值但被 Make 判断为非 0 | 在 board Make.defs 中补 `CONFIG_STACK_USAGE_WARNING ?= 0` |
| 使用宿主 `gcc` 编译，出现 `UINTPTR_MAX` 等宏错误 | 未选择 RISC-V 工具链 | 在 35 个 defconfig 中补 `CONFIG_RISCV_TOOLCHAIN_GNU_RV32=y` |

### 10.6 当前未完成任务

#### 任务 A：完成 `nsh` 配置编译

操作：

```bash
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

验收标准：

- 构建输出最终成功。
- 产物目录中生成 `nuttx`、bin/hex 等固件文件。
- 不再出现 Kconfig、Make.defs、HAL include、linker script 级别错误。

如果失败：

优先按错误类型判断：

| 失败位置 | 判断方式 | 处理方向 |
|----------|----------|----------|
| 配置阶段 | `Kconfig`、`olddefconfig`、`Loaded configuration` 附近失败 | 修 Kconfig/defconfig |
| `.context` 阶段 | `LN:`、`CP:`、`Clone:` 附近失败 | 修 custom 路径或 HAL 依赖 |
| 依赖生成阶段 | `mkdeps`、`.ddc` 失败 | 修 include path、VPATH、工具链 |
| 编译阶段 | `CC:` 后具体 `.c` 文件失败 | 修 API 差异或头文件兼容 |
| 链接阶段 | `LD:`、`undefined reference` | 修 Make.defs 源文件列表或配置依赖 |

#### 任务 B：烧录并验证 NSH 启动

前置条件：

- `nsh` 固件已经编译通过。
- 本机能识别 ESP32-P4 Function EV Board 的串口/USB 下载口。

操作方向：

```bash
# 具体烧录命令需要根据最终产物和 openvela/ESP32-P4 bootloader 输出确认
esptool.py --chip esp32p4 -p <串口设备> -b 460800 write_flash ...
```

验收标准：

- 串口看到 boot log。
- 能进入 NSH prompt。
- 能执行基础命令，例如 `help`、`uname`、`free`。

知识补充：

编译通过只说明软件链接完整；板上启动还会验证：

- bootloader 与 app image 格式是否匹配
- flash offset 是否正确
- linker script 的 RAM/flash 布局是否正确
- UART console 引脚是否与硬件一致

#### 任务 C：验证基础外设

建议顺序：

1. UART console
2. GPIO
3. I2C
4. SPI
5. PSRAM
6. SPI Flash 文件系统
7. Ethernet
8. USB console

原因：

先验证低风险、低依赖外设，再验证网络、存储和显示。这样出现问题时更容易定位是 board pin、driver、Kconfig 还是硬件连接问题。

#### 任务 D：验证板载音频

当前状态：未开始 ES8311 board glue。

验收顺序：

1. I2C 总线可用，并确认 codec 地址；
2. I2S 引脚和时钟与目标板原理图/BSP 一致；
3. ES8311 lower-half 完成注册，录音与播放设备可见；
4. 用固定格式 PCM 分别完成麦克风采集和扬声器播放测试。

不要仅以通用 `pcm_in0` 节点存在作为板载音频验收。

#### 任务 E：移植显示和触摸

当前状态：未开始实际代码适配。

需要确认：

- EV Board 搭配的 LCD 面板型号
- MIPI-DSI lane 数量、时钟、初始化序列
- 触摸芯片型号，例如 GT911/FT5x06/CST 系列
- I2C bus、INT、RST 引脚
- LVGL framebuffer 使用方式

建议步骤：

1. 先让 `/dev/lcd0` 或 framebuffer 设备注册成功。
2. 再用简单色块/刷屏测试验证 LCD。
3. 然后注册 `/dev/input0` 触摸设备。
4. 最后接入 LVGL。

知识补充：

LVGL 不应该是第一步。显示链路应拆成：

```text
MIPI DSI host -> panel init -> framebuffer/lcd device -> LVGL flush callback
```

触摸链路应拆成：

```text
I2C bus -> touch chip probe -> input device register -> LVGL indev
```

这样可以避免把底层驱动问题误判成 UI 问题。

#### 任务 F：迁移应用（可选）

前置条件：

- NSH 可启动。
- 基础网络可用，优先 Ethernet。
- LVGL 显示和触摸可用。

工作内容：

- 选择具体应用作为板级验收之外的后续目标。
- SmartHome 需要单独准备网络、LVGL、资源文件与 cAGENT 依赖。
- audio_event 可用于验证音频链路，但不取代 ES8311 独立录放音测试。

知识补充：

应用不应反向决定底层 bring-up 顺序。需要网络的应用初期优先走以太网，
将板载 C6/ESP-Hosted 留到基础板级能力稳定之后。

#### 任务 G：板载 C6 / ESP-Hosted（可选）

当前状态：未开始。

原因：

ESP32-P4 没有内置 WiFi/BLE；Function EV Board 则带板载 ESP32-C6。要联网有两条路：

1. 先使用 EV Board 的 Ethernet。
2. 后续验证板载 ESP32-C6 的 ESP-Hosted 或 AT 支持；外接伴侣芯片仅作为板卡变体方案。

建议：

比赛 demo 阶段优先 Ethernet；如果必须无线，再做 ESP-Hosted。

### 10.7 后续推荐执行顺序

建议按下面顺序继续：

```text
1. 重新跑 nsh build，拿到完整成功/失败结果
2. 如果失败，只修第一个真实 error
3. nsh build pass 后，确认固件产物和烧录方式
4. 板上启动，拿到串口 NSH
5. 验证 UART/GPIO/I2C/SPI/PSRAM/Ethernet
6. 验证板载 ES8311 音频
7. 增加 LCD 和触摸，再接入 LVGL
8. 验证板载 C6 / ESP-Hosted（如需要）
9. 最后选择并迁移应用
```

原因：

这是典型 bring-up 的低风险路径。每一步只增加一个变量，失败时定位成本最低。

### 10.8 当前最重要的下一步

当前不要急着做板载音频、LCD、触摸或任何应用。下一步只做一件事：

```bash
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

然后根据结果判断：

- 如果 build pass：进入烧录和 NSH boot。
- 如果 build fail：记录第一个真实 error，继续修 P4 与当前 openvela 的 API/构建差异。

当前移植已经越过了“文件没接上”的阶段，正在进入“版本差异修补”和“实际硬件 bring-up”阶段。

---

*本文档将随适配进展持续更新。*
