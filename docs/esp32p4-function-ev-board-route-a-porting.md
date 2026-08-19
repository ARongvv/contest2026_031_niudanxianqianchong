# ESP32-P4 Function EV Board 路线 A 移植方案

> 文档版本: v1.1
> 适用项目: contest2026_031_niudanxianqianchong / openvela
> 目标硬件: ESP32-P4 Function EV Board
> 路线定义: 按 openvela `vendor_template` 思路，将 ESP32-P4 芯片层和板级层都放在 `vendor/espressif/` 下，通过 `CONFIG_ARCH_CHIP_CUSTOM` 和 `CONFIG_ARCH_BOARD_CUSTOM` 接入。

## 一、方案定位

openvela 官方新平台适配文档强调通过 `vendor` 仓库隔离厂商定制代码。路线 A 遵循该思路，不把 ESP32-P4 的芯片层直接合入 `nuttx/arch/risc-v/src/esp32p4`，而是在项目目录维护源码、经 manifest 链接到 vendor 位置的方式接入。

本路线的验收对象是板级移植本身。SmartHome、audio_event 等应用均不属于
最小 bring-up 的前置条件，只能在相应板载外设验证完成后作为可选集成目标。

```text
vendor/espressif/
├── chips/
│   └── esp32p4/                         # ESP32-P4 custom chip 层
└── boards/
    └── esp32p4/
        └── esp32p4-function-ev-board/   # ESP32-P4 EV Board custom board 层
```

最终 defconfig 通过以下配置接入:

```ini
CONFIG_ARCH="risc-v"
CONFIG_ARCH_RISCV=y
CONFIG_ARCH_CHIP_CUSTOM=y
CONFIG_ARCH_CHIP_CUSTOM_NAME="esp32p4"
CONFIG_ARCH_CHIP_CUSTOM_DIR="../vendor/espressif/chips/esp32p4"
CONFIG_ARCH_CHIP_CUSTOM_DIR_RELPATH=y

CONFIG_ARCH_BOARD_CUSTOM=y
CONFIG_ARCH_BOARD_CUSTOM_NAME="esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/espressif/boards/esp32p4/esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_CUSTOM_DIR_RELPATH=y
```

该路线的优点是符合 vendor 隔离原则，便于比赛/厂商仓独立管理。代价是 ESP32-P4 的启动、中断、串口、timer、heap、cache、PSRAM、HAL 集成和链接脚本都需要在 vendor custom chip 中维护，后续同步 Apache NuttX 上游会更重。

## 二、移植目标

第一阶段目标不是直接运行 SmartHome，而是点亮最小系统:

1. ESP32-P4 能完成 RISC-V 启动并进入 `nx_start()`
2. 串口或 USB console 能输出启动日志
3. `nsh_main` 能运行并进入 `nsh>`
4. `ls /dev` 能看到基础节点，如 `/dev/console`、`/dev/null`、`/dev/zero`
5. 后续逐步开启 GPIO、I2C、SPI flash、以太网、板载 ES8311 音频、LCD、触摸和板载 C6 网络

## 三、目录规划

建议新建以下目录:

```text
vendor/espressif/chips/esp32p4/
├── Kconfig
├── Make.defs
├── CMakeLists.txt                       # 如当前构建路径需要 CMake 支持
├── chip.h
├── include/
│   ├── chip.h
│   └── irq.h
├── common/
│   └── espressif/                       # P4 需要的 Espressif RISC-V 共享层
├── esp_chip_rev.c
├── hal_esp32p4.mk
├── hal_esp32p4.cmake
├── esp32p4_allocateheap.c               # 如未复用上游文件名，可保留上游命名
├── esp32p4_irq.c
├── esp32p4_lowputc.c
├── esp32p4_serial.c
├── esp32p4_start.c
└── esp32p4_timerisr.c

vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/
├── Kconfig
├── CMakeLists.txt
├── configs/
│   ├── nsh/
│   │   └── defconfig
│   ├── ethernet/
│   │   └── defconfig                    # 第二阶段
│   └── smart_home/
│       └── defconfig                    # 第三阶段
├── include/
│   └── board.h
├── scripts/
│   ├── Make.defs
│   ├── common.ld
│   ├── esp32p4_aliases.ld
│   ├── esp32p4_flat_memory.ld
│   └── esp32p4_sections.ld
└── src/
    ├── Make.defs
    ├── esp32p4-function-ev-board.h
    ├── esp32p4_boot.c
    ├── esp32p4_bringup.c
    ├── esp32p4_appinit.c
    ├── esp32p4_reset.c
    └── board-specific drivers...
```

## 四、上游文件映射

路线 A 仍然建议以 Apache NuttX 的 ESP32-P4 支持为基线，只是落盘到 vendor 目录。

| 上游 NuttX 路径 | 路线 A 目标路径 | 处理方式 |
| --- | --- | --- |
| `arch/risc-v/src/esp32p4/Kconfig` | `vendor/espressif/chips/esp32p4/Kconfig` | 改为 `if ARCH_CHIP_CUSTOM` 或以 `ARCH_CHIP_CUSTOM_NAME="esp32p4"` 为条件 |
| `arch/risc-v/src/esp32p4/Make.defs` | `vendor/espressif/chips/esp32p4/Make.defs` | 保留 `include risc-v/Make.defs`，改写 `common/espressif` 和 HAL 路径 |
| `arch/risc-v/src/esp32p4/hal_esp32p4.mk` | `vendor/espressif/chips/esp32p4/hal_esp32p4.mk` | 保留 P4 HAL 源文件列表，调整相对路径 |
| `arch/risc-v/src/esp32p4/esp_chip_rev.c` | `vendor/espressif/chips/esp32p4/esp_chip_rev.c` | 保留，用于芯片 revision 检测 |
| `arch/risc-v/include/esp32p4/*` | `vendor/espressif/chips/esp32p4/include/*` | 保留 `chip.h`、`irq.h` 等芯片头 |
| `arch/risc-v/src/common/espressif/*` | `vendor/espressif/chips/esp32p4/common/espressif/*` | 拷贝 P4 所需共享层，避免修改 `nuttx/arch` |
| `boards/risc-v/esp32p4/common/*` | `vendor/espressif/boards/esp32p4/common/*` 或 board 内部 `scripts/src` | 如多个 P4 板共用，建议保留 common |
| `boards/risc-v/esp32p4/esp32p4-function-ev-board/*` | `vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/*` | 作为 board 层起点 |

注意: 不建议把上游 `common/espressif` 整目录盲目覆盖到 `nuttx/arch/risc-v/src/common/espressif`。路线 A 的目标是将 P4 所需共享层放进 vendor custom chip，减少核心仓变更。

## 五、芯片层适配步骤

### 5.1 创建 `vendor/espressif/chips/esp32p4/Kconfig`

该文件负责声明 ESP32-P4 custom chip 的内部配置。建议包含:

```kconfig
if ARCH_CHIP_CUSTOM

comment "ESP32-P4 Custom Chip Configuration"
    depends on ARCH_CHIP_CUSTOM_NAME = "esp32p4"

config ESPRESSIF_ESP32P4
    bool
    default y
    depends on ARCH_CHIP_CUSTOM_NAME = "esp32p4"

choice ESP32P4_CHIP_REVISION
    prompt "ESP32-P4 chip revision"
    default ESP32P4_REV_V3
    depends on ESPRESSIF_ESP32P4

config ESP32P4_REV_LESS_V3
    bool "ESP32-P4 revision < v3.0"

config ESP32P4_REV_V3
    bool "ESP32-P4 revision v3.x"

endchoice

config ESP32P4_REV_MIN
    int
    default 0 if ESP32P4_REV_LESS_V3
    default 300 if ESP32P4_REV_V3

source "vendor/espressif/chips/esp32p4/common/espressif/Kconfig"

endif
```

实际 Kconfig 应以 Apache NuttX 上游 `esp32p4/Kconfig` 为准，保留 cache、PSRAM、revision 和外设依赖项。

### 5.2 创建 `Make.defs`

`Make.defs` 是路线 A 的核心。它需要做三件事:

1. 选择 RISC-V 架构通用构建规则
2. 加入 ESP32-P4 启动和中断等芯片源文件
3. 接入 Espressif HAL 和共享驱动

示意:

```makefile
############################################################################
# vendor/espressif/chips/esp32p4/Make.defs
############################################################################

include risc-v/Make.defs

CHIP_SERIES := esp32p4
ESP_HAL_3RDPARTY_REPO := esp-hal-3rdparty

INCLUDES += ${INCDIR_PREFIX}$(TOPDIR)$(DELIM)../vendor/espressif/chips/esp32p4
INCLUDES += ${INCDIR_PREFIX}$(TOPDIR)$(DELIM)../vendor/espressif/chips/esp32p4/include
INCLUDES += ${INCDIR_PREFIX}$(TOPDIR)$(DELIM)../vendor/espressif/chips/esp32p4/common/espressif

CHIP_CSRCS += esp32p4_start.c
CHIP_CSRCS += esp32p4_irq.c
CHIP_CSRCS += esp32p4_lowputc.c
CHIP_CSRCS += esp32p4_serial.c
CHIP_CSRCS += esp32p4_timerisr.c
CHIP_CSRCS += esp32p4_allocateheap.c
CHIP_CSRCS += esp_chip_rev.c

include $(TOPDIR)$(DELIM)..$(DELIM)vendor$(DELIM)espressif$(DELIM)chips$(DELIM)esp32p4$(DELIM)common$(DELIM)espressif$(DELIM)Make.defs
include $(TOPDIR)$(DELIM)..$(DELIM)vendor$(DELIM)espressif$(DELIM)chips$(DELIM)esp32p4$(DELIM)hal_esp32p4.mk
```

真实实现时应尽量保留上游 NuttX `arch/risc-v/src/esp32p4/Make.defs` 和 `common/espressif/Make.defs` 的构建逻辑，重点调整路径。

### 5.3 接入 esp-hal-3rdparty

ESP32-P4 依赖 Espressif HAL。路线 A 下建议在 custom chip 中维护 HAL 拉取/引用逻辑:

```text
vendor/espressif/chips/esp32p4/hal_esp32p4.mk
vendor/espressif/chips/esp32p4/hal_esp32p4.cmake
```

需要确认 HAL 版本中至少包含:

```text
components/soc/esp32p4/
components/hal/esp32p4/
components/esp_rom/esp32p4/
components/esp_hw_support/port/esp32p4/
components/bootloader_support/src/esp32p4/
nuttx/esp32p4/include/
```

如果当前 `esp-hal-3rdparty-cache` 中缺少这些目录，需要更新 `ESP_HAL_3RDPARTY_VERSION`，或将可用 HAL 作为 vendor 外部依赖固定下来。

### 5.4 启动入口

芯片层必须提供启动入口，职责包括:

1. 关闭或初始化 watchdog
2. 建立中断向量和 CPU 上下文
3. 初始化时钟、cache、MMU/MPU
4. 清 BSS、拷贝 data/ramfunc
5. 初始化 early serial
6. 初始化 heap 区域
7. 调用 `nx_start()`

建议先复用上游 ESP32-P4 启动代码，不从 ESP32-C6 手写改名。ESP32-P4 是双核 RISC-V，启动和 cache/PSRAM 细节比 C6 复杂。

### 5.5 中断、timer、serial 和 heap

最小 NSH 阶段必须优先确认这些模块:

| 模块 | 必要性 | 验证标准 |
| --- | --- | --- |
| interrupt | 必须 | timer tick 正常，串口中断可用 |
| lowputc/serial | 必须 | 早期日志和 `/dev/console` 可用 |
| timerisr/tickless | 必须 | `nsh>` 不死机，sleep/usleep 正常 |
| allocateheap | 必须 | 系统能创建任务和打开文件节点 |
| reset | 建议 | `reboot` 或异常复位可用 |

## 六、板级层适配步骤

### 6.1 创建最小 board 目录

```text
vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/
├── Kconfig
├── configs/nsh/defconfig
├── include/board.h
├── scripts/Make.defs
└── src/
    ├── Make.defs
    ├── esp32p4_boot.c
    ├── esp32p4_bringup.c
    └── esp32p4_appinit.c
```

第一版 board 层只做基础 bringup，不启用 MIPI-DSI、触摸、摄像头和 WiFi。

### 6.2 `esp32p4_boot.c`

该文件提供板级入口:

```c
void esp32p4_board_initialize(void)
{
  /* Early board init, keep minimal in phase 1. */
}

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  esp32p4_bringup();
}
#endif
```

函数名应和上游 ESP32-P4 arch 层期望的 board hook 保持一致。如果上游使用不同 hook 名称，应以 P4 upstream port 为准。

### 6.3 `esp32p4_bringup.c`

最小阶段建议只挂载 procfs，并保留后续外设注册框架:

```c
int esp32p4_bringup(void)
{
#ifdef CONFIG_FS_PROCFS
  int ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs: %d\n", ret);
    }
#endif

  return OK;
}
```

第二阶段再逐步加入:

```text
board_gpio_initialize()
board_i2c_initialize()
board_spi_initialize()
board_spiflash_initialize()
board_emac_initialize()
board_i2s_initialize()
board_lcd_initialize()
board_touchscreen_initialize()
```

### 6.4 链接脚本

ESP32-P4 的链接脚本建议从上游 `boards/risc-v/esp32p4/common/scripts/` 迁移到 vendor board:

```text
scripts/common.ld
scripts/esp32p4_aliases.ld
scripts/esp32p4_flat_memory.ld
scripts/esp32p4_sections.ld
scripts/esp32p4_legacy_sections.ld
```

`scripts/Make.defs` 需要将这些脚本加入 `ARCHSCRIPT`。如果使用 simple boot，还需要确认 ROM linker scripts 和 HAL linker scripts 由 `hal_esp32p4.mk` 注入。

## 七、最小 NSH defconfig

第一版 `vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig` 建议尽量小:

```ini
# Build
CONFIG_HOST_LINUX=y
CONFIG_APPS_DIR="../apps"
CONFIG_BUILD_FLAT=y

# Architecture
CONFIG_ARCH="risc-v"
CONFIG_ARCH_RISCV=y
CONFIG_ARCH_RV32=y
CONFIG_ARCH_RV_ISA_M=y
CONFIG_ARCH_RV_ISA_A=y
CONFIG_ARCH_RV_ISA_C=y
CONFIG_ARCH_STACKDUMP=y

# Custom chip
CONFIG_ARCH_CHIP_CUSTOM=y
CONFIG_ARCH_CHIP_CUSTOM_NAME="esp32p4"
CONFIG_ARCH_CHIP_CUSTOM_DIR="../vendor/espressif/chips/esp32p4"
CONFIG_ARCH_CHIP_CUSTOM_DIR_RELPATH=y

# Custom board
CONFIG_ARCH_BOARD_CUSTOM=y
CONFIG_ARCH_BOARD_CUSTOM_NAME="esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/espressif/boards/esp32p4/esp32p4-function-ev-board"
CONFIG_ARCH_BOARD_CUSTOM_DIR_RELPATH=y

# ESP32-P4
CONFIG_ESPRESSIF_ESP32P4=y
CONFIG_ESP32P4_REV_V3=y
CONFIG_ESP32P4_REV_MIN=300

# Console and serial
CONFIG_DEV_CONSOLE=y
CONFIG_SERIAL=y
CONFIG_UART0_SERIAL_CONSOLE=y
CONFIG_UART0_BAUD=115200

# Scheduler / init
CONFIG_INIT_ENTRYPOINT="nsh_main"
CONFIG_INIT_STACKSIZE=4096
CONFIG_DEFAULT_TASK_STACKSIZE=4096
CONFIG_IDLETHREAD_STACKSIZE=2048
CONFIG_RR_INTERVAL=200
CONFIG_SCHED_WAITPID=y

# Filesystem
CONFIG_FS_PROCFS=y
CONFIG_NFILE_DESCRIPTORS=32
CONFIG_NFILE_STREAMS=16

# NSH
CONFIG_BUILTIN=y
CONFIG_NSH_BUILTIN_APPS=y
CONFIG_NSH_READLINE=y
CONFIG_SYSTEM_NSH=y
CONFIG_NSH_FILEIOSIZE=512

# Debug
CONFIG_DEBUG_FEATURES=y
CONFIG_DEBUG_ASSERTIONS=y
CONFIG_DEBUG_SYMBOLS=y
```

后续再增加:

```ini
CONFIG_ESPRESSIF_GPIO=y
CONFIG_ESPRESSIF_I2C=y
CONFIG_ESPRESSIF_SPI=y
CONFIG_ESPRESSIF_SPIFLASH=y
CONFIG_ESPRESSIF_EMAC=y
CONFIG_ESPRESSIF_I2S=y
CONFIG_GRAPHICS_LVGL=y
```

## 八、构建和验证

### 8.1 构建命令

```bash
./build.sh vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/nsh --cmake -j$(nproc)
```

如当前目标不支持 `--cmake`，使用项目现有 `build.sh` 默认路径:

```bash
./build.sh vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/nsh -j$(nproc)
```

### 8.2 第一阶段成功标准

构建成功:

```text
LD: nuttx
Generated: nuttx.bin
```

启动成功:

```text
NuttShell (NSH)
nsh>
```

NSH 验证:

```text
nsh> uname -a
nsh> ls /dev
nsh> cat /proc/meminfo
nsh> sleep 1
```

### 8.3 失败定位顺序

| 现象 | 优先排查 |
| --- | --- |
| Kconfig 找不到 custom chip | defconfig 中 `CONFIG_ARCH_CHIP_CUSTOM_DIR` 路径、`Kconfig` 条件 |
| 编译找不到 HAL 头文件 | `hal_esp32p4.mk` include 路径、esp-hal-3rdparty 版本 |
| 链接缺 ROM 符号 | ROM ld 是否加入 `ARCHSCRIPT` |
| 无串口输出 | lowputc、UART pin、console 配置、下载口选择 |
| 进入 `nx_start()` 后死机 | timer interrupt、heap、idle stack、interrupt stack |
| `nsh>` 能进但命令异常 | 文件描述符、procfs、heap 区域、栈大小 |

## 九、分阶段外设 bringup

### Phase 1: NSH 最小系统

只启用 UART console、timer、heap、procfs。目标是稳定进入 `nsh>`。

### Phase 2: 基础外设

按以下顺序添加:

1. GPIO
2. I2C
3. SPI
4. SPI flash / MTD / SmartFS
5. 以太网 EMAC
6. I2S char/audio

每启用一个外设，都要在 board `bringup` 中注册对应 `/dev` 节点，不能只改 defconfig。

### Phase 3: 显示和触摸

ESP32-P4 EV Board 的显示通常走 MIPI-DSI，触摸通常走 I2C。该阶段需要确认:

1. 实际板卡是否为 ESP32-P4 Function EV Board，以及其硬件 revision
2. 芯片 revision 是 `< v3.0` 还是 `v3.x`
3. 面板型号、分辨率、DSI lane、初始化序列
4. 触摸 IC 型号、I2C 地址、reset/int GPIO
5. LVGL 使用 `/dev/lcd0` 和 `/dev/input0` 的配置

### Phase 4: SmartHome 应用

最后迁移 SmartHome:

1. 网络从 ESP32-S3 片内 WiFi 切换到以太网或 ESP-Hosted
2. LVGL 布局从 320x240 调整到 1024x600
3. 文件系统挂载路径保持和应用资源路径一致
4. cAGENT 任务栈、TLS、HTTP client、DNS、socket buffer 根据 P4 内存重新配置

## 十、路线 A 风险和规避

| 风险 | 说明 | 规避方式 |
| --- | --- | --- |
| 上游同步成本高 | P4 upstream port 后续更新需要手工同步到 vendor | 保留 `UPSTREAM_BASE` 记录，定期 diff |
| HAL 版本不匹配 | `hal_esp32p4.mk` 引用的源文件可能在当前 HAL 不存在 | 固定 esp-hal-3rdparty commit，构建前检查目录 |
| common/espressif 重复维护 | vendor 内复制共享层后容易和 `nuttx/arch` 分叉 | 只复制 P4 必需文件，建立差异清单 |
| custom chip Kconfig 条件复杂 | `ARCH_CHIP_CUSTOM` 下不再自动有 `ARCH_CHIP_ESP32P4` | 统一使用 `ESPRESSIF_ESP32P4` 和 `ARCH_CHIP_CUSTOM_NAME` 条件 |
| 双核/SMP bringup 复杂 | P4 是 HP 双核 RISC-V | 第一阶段可先单核启动，SMP 独立验证 |
| 芯片/板卡 revision 差异 | 早期 P4 与 v3.x 存在硬件差异 | defconfig 明确设置 revision，只使用对应板卡资料 |

## 十一、建议提交拆分

为了便于 review 和回退，建议按以下提交拆分:

1. `vendor/espressif: add esp32p4 custom chip skeleton`
2. `vendor/espressif: add esp32p4 HAL integration`
3. `vendor/espressif: add esp32p4 function ev board nsh config`
4. `vendor/espressif: enable gpio/i2c/spi for esp32p4 ev board`
5. `vendor/espressif: enable ethernet for esp32p4 ev board`
6. `vendor/espressif: add display and touchscreen bringup`
7. `contest: add smart_home config for esp32p4 ev board`

## 十二、结论

路线 A 是符合 openvela vendor 隔离思路的移植方式。对 ESP32-P4 Function EV Board，路线 A 的关键不是只新增 board 目录，而是要在 `vendor/espressif/chips/esp32p4` 中完整承接 ESP32-P4 芯片层:

```text
custom chip = RISC-V 启动 + 中断 + timer + serial + heap + HAL + cache/PSRAM + 链接脚本
custom board = pinmux + 外设注册 + defconfig + bringup
```

建议先完成最小 `nsh`，确认 custom chip 路径可用后，再逐步打开以太网、I2S、MIPI-DSI、触摸和 SmartHome。
