# ESP32-P4 移植开发记录

本文档记录 `esp32p4-function-ev-board` 移植到 openvela Route A 过程中的实际问题、判断和处理结果。

> 状态说明（2026-08-19）：本文是 2026-07-05 的历史排障记录，不是当前
> 构建状态的证明。当前工作树已存在 `esp-hal-3rdparty` 目录，但尚未在本次
> 复核中重新获得完整 build pass 或实板 NSH 日志；继续工作前应按文中构建
> 命令重新执行，并只处理第一个真实错误。

## 2026-07-05：`SPI timing flash clock is invalid`

### 构建命令

```bash
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

### 报错现象

```text
In file included from chip/common/espressif/esp_vectors.S:33:
/home/arongw/openvela/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h:933:4:
error: #error "SPI timing flash clock is invalid"
```

报错发生在 `esp_vectors.S` 编译阶段。`sdkconfig.h` 中对应逻辑会检查是否定义了 flash 频率宏：

```c
CONFIG_ESPRESSIF_FLASH_FREQ_20M
CONFIG_ESPRESSIF_FLASH_FREQ_40M
CONFIG_ESPRESSIF_FLASH_FREQ_80M
```

如果三个宏都没有定义，就会触发：

```c
#error "SPI timing flash clock is invalid"
```

### 原因判断

`nsh/defconfig` 中已经写了：

```ini
CONFIG_ESPRESSIF_FLASH_16M=y
CONFIG_ESPRESSIF_FLASH_FREQ_80M=y
CONFIG_ESPRESSIF_FLASH_MODE_DIO=y
```

但当前 Route A custom chip 下，这些 flash 配置没有正式进入 `nuttx/.config` 和 `nuttx/include/nuttx/config.h`。

之前在 `chips/esp32p4/common/espressif/Make.defs` 中只给 `CFLAGS` 补了宏：

```make
CFLAGS += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_FREQ_80M=1
```

这对 `.c` 文件有效，但这次失败的是 `.S` 汇编预处理文件：

```text
chip/common/espressif/esp_vectors.S
```

`.S` 文件使用 `AFLAGS`，没有拿到只加在 `CFLAGS` 里的 flash 宏，因此 `sdkconfig.h` 仍然认为 flash 频率无效。

### 最小补丁

修改文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/common/espressif/Make.defs
```

将 flash/simple boot 宏抽成公共变量，同时加入 `CFLAGS` 和 `AFLAGS`：

```make
ESPRESSIF_FLASH_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_16M=1
ESPRESSIF_FLASH_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_MODE_DIO=1
ESPRESSIF_FLASH_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_FREQ_80M=1
ESPRESSIF_FLASH_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_SIMPLE_BOOT=1
CFLAGS += $(ESPRESSIF_FLASH_DEFINES)
AFLAGS += $(ESPRESSIF_FLASH_DEFINES)
```

### 验证结果

重新构建后，原错误已经越过：

```text
AS: chip/common/espressif/esp_vectors.S
```

说明 `esp_vectors.S` 已经拿到了 flash 频率宏，`SPI timing flash clock is invalid` 不再出现。

### 新的下一处错误

构建继续向前后，新的第一个真实错误变为：

```text
/home/arongw/openvela/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h:672:37:
error: 'CONFIG_ESPRESSIF_LOG_LEVEL' undeclared
```

相关定义：

```c
#define CONFIG_BOOTLOADER_LOG_LEVEL CONFIG_ESPRESSIF_LOG_LEVEL
```

这说明 `esp-hal-3rdparty` 的 `sdkconfig.h` 还需要 `CONFIG_ESPRESSIF_LOG_LEVEL`，但当前 Route A 配置链路没有提供该宏。

### 当前结论

本次最小补丁只解决 flash 宏没有传递给汇编文件的问题。它没有试图修复完整 Kconfig 接入。

当前下一步有两个方向：

1. 继续采用最小补丁，在 Make 侧为 `CONFIG_ESPRESSIF_LOG_LEVEL` 提供兜底宏，让 `nsh` build 继续向前推进。
2. 回到 Kconfig 层，把 ESP32-P4 所需的 Espressif common 配置正式接入 `.config`。

当前阶段建议优先使用方向 1，逐个修复构建暴露出的缺失宏；等 `nsh` 能走到更后面，再统一整理 Kconfig。

## 2026-07-05：`CONFIG_ESPRESSIF_LOG_LEVEL` 缺失

### 报错现象

```text
/home/arongw/openvela/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h:672:37:
error: 'CONFIG_ESPRESSIF_LOG_LEVEL' undeclared
```

相关 include 链路：

```text
chip/common/espressif/esp_start.c
  -> chip/common/espressif/esp_lowputc.h
    -> hal/uart_hal.h
      -> components/log/include/esp_log_level.h
        -> nuttx/esp32p4/include/sdkconfig.h
```

`sdkconfig.h` 中将多个 ESP-IDF log 配置映射到 `CONFIG_ESPRESSIF_LOG_LEVEL`：

```c
#define CONFIG_LOG_DEFAULT_LEVEL CONFIG_ESPRESSIF_LOG_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL CONFIG_ESPRESSIF_LOG_LEVEL
#define CONFIG_BOOTLOADER_LOG_LEVEL CONFIG_ESPRESSIF_LOG_LEVEL
```

当前 Route A 配置链路没有把 `CONFIG_ESPRESSIF_LOG_LEVEL` 写入 `nuttx/.config` 或 `nuttx/include/nuttx/config.h`，因此 C 编译时该宏未定义。

### 原因判断

`chips/esp32p4/common/espressif/Kconfig` 中实际有 log level 定义：

```kconfig
config ESPRESSIF_LOG_LEVEL_INFO
config ESPRESSIF_LOG_LEVEL
    int
    default 3 if ESPRESSIF_LOG_LEVEL_INFO
```

其中 `INFO` 对应数值 `3`。

但当前为了避免 Kconfig 重复定义和 choice 冲突，没有完整 source 这份 Espressif common Kconfig。因此 `CONFIG_ESPRESSIF_LOG_LEVEL` 没有进入最终 `.config`。

### 最小补丁

继续沿用 Make 侧兜底方案，并将原来的 `ESPRESSIF_FLASH_DEFINES` 泛化为 `ESPRESSIF_SDKCONFIG_DEFINES`：

```make
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_16M=1
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_MODE_DIO=1
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_FLASH_FREQ_80M=1
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_SIMPLE_BOOT=1
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_LOG_LEVEL=3
CFLAGS += $(ESPRESSIF_SDKCONFIG_DEFINES)
AFLAGS += $(ESPRESSIF_SDKCONFIG_DEFINES)
```

修改文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/common/espressif/Make.defs
```

### 验证结果

重新构建后，`CONFIG_ESPRESSIF_LOG_LEVEL` 报错已经越过。新的第一个真实错误变为：

```text
chip/common/espressif/esp_lowputc.c:355:27:
error: 'CONFIG_ESP_CONSOLE_UART_NUM' undeclared
```

相关代码：

```c
if (uart_num != CONFIG_ESP_CONSOLE_UART_NUM)
```

### 新的下一处问题

`CONFIG_ESP_CONSOLE_UART_NUM` 是 ESP-IDF 风格 console UART 编号宏。当前 `nsh/defconfig` 中启用了：

```ini
CONFIG_UART0_SERIAL_CONSOLE=y
```

`Bootloader.mk` 中也有类似推导逻辑：

```make
$(if $(CONFIG_UART0_SERIAL_CONSOLE),$(call cfg_val,CONFIG_ESP_CONSOLE_UART_NUM,0))
$(if $(CONFIG_UART1_SERIAL_CONSOLE),$(call cfg_val,CONFIG_ESP_CONSOLE_UART_NUM,1))
```

但主编译路径没有得到这个宏，因此 `esp_lowputc.c` 编译失败。

### 当前结论

`CONFIG_ESPRESSIF_LOG_LEVEL` 已通过最小补丁解决。当前继续暴露出的缺失项仍然属于同一类问题：Espressif HAL/NuttX glue 代码需要 ESP-IDF 风格 SDK 配置宏，但 Route A custom chip 当前没有完整 Kconfig 接入。

下一步可继续在 `ESPRESSIF_SDKCONFIG_DEFINES` 中按当前控制台配置补：

```make
${DEFINE_PREFIX}CONFIG_ESP_CONSOLE_UART=1
${DEFINE_PREFIX}CONFIG_ESP_CONSOLE_UART_NUM=0
```

其中 `0` 对应当前 `CONFIG_UART0_SERIAL_CONSOLE=y`。

## 2026-07-05：继续推进最小补丁后的状态

### 1. `CONFIG_ESP_CONSOLE_UART_NUM` 缺失

报错位置：

```text
chip/common/espressif/esp_lowputc.c:355:27:
error: 'CONFIG_ESP_CONSOLE_UART_NUM' undeclared
```

原因仍然是 ESP-IDF 风格 SDK 配置宏没有进入主编译路径。当前 `nsh/defconfig` 使用：

```ini
CONFIG_UART0_SERIAL_CONSOLE=y
```

因此最小补丁按 UART0 console 补：

```make
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESP_CONSOLE_UART=1
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESP_CONSOLE_UART_NUM=0
```

修改文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/common/espressif/Make.defs
```

验证结果：该错误已越过。

### 2. `CONFIG_ESPRESSIF_CPU_FREQ_MHZ` 缺失

报错位置：

```text
esp-hal-3rdparty/components/esp_hw_support/hw_random.c
error: 'CONFIG_ESPRESSIF_CPU_FREQ_MHZ' undeclared
```

`sdkconfig.h` 中有如下映射：

```c
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ CONFIG_ESPRESSIF_CPU_FREQ_MHZ
```

当前 `nsh/defconfig` 已选择：

```ini
CONFIG_ESPRESSIF_CPU_FREQ_400=y
```

因此最小补丁补：

```make
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESPRESSIF_CPU_FREQ_MHZ=400
```

验证结果：该错误已越过。

### 3. `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM` 缺失

报错位置：

```text
esp-hal-3rdparty/components/esp_system/port/soc/esp32p4/clk.c:108:
error: 'CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM' undeclared
```

NuttX 原始 `Bootloader.mk` 对 UART0 console 的推导是：

```make
CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=0
```

因此最小补丁补：

```make
ESPRESSIF_SDKCONFIG_DEFINES += ${DEFINE_PREFIX}CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=0
```

验证结果：该错误已越过。

### 4. `esp-hal-3rdparty/nuttx/src/platform/os.c` 与 openvela API 不匹配

报错分为两类：

```text
error: 'O_RDWR' undeclared
error: 'O_CREAT' undeclared
error: 'F_GETFL' undeclared
error: 'O_NONBLOCK' undeclared
error: 'F_SETFL' undeclared
```

以及：

```text
error: too many arguments to function 'nxtask_init'
```

第一类原因是缺少 POSIX flags 头文件。第二类原因是从 NuttX ESP32-P4 引入的 `esp-hal-3rdparty` 适配代码使用了另一版 `nxtask_init()` 调用形式，而当前 openvela 中 `nxtask_init()` 原型为：

```c
int nxtask_init(FAR struct tcb_s *tcb, const char *name, main_t entry,
                FAR const posix_spawn_file_actions_t *actions,
                FAR const posix_spawnattr_t *attr,
                FAR char * const argv[], FAR char * const envp[]);
```

最小补丁：

```c
#include <fcntl.h>
#include <spawn.h>
```

并将任务创建改为当前 openvela 风格：

```c
posix_spawnattr_init(&attr);
posix_spawnattr_setstacksize(&attr, (size_t)stack_size);
posix_spawnattr_setpriority(&attr, priority);

ret = nxtask_init(tcb, name, task_wrapper_entry, NULL, &attr, argv, NULL);
posix_spawnattr_destroy(&attr);
```

修改文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/esp-hal-3rdparty/nuttx/src/platform/os.c
```

验证结果：该错误已越过。

注意：后续配置刷新时曾出现 `esp-hal-3rdparty` 目录缺失，导致该文件一度不在工作区中。当前已恢复包含 ESP32-P4 的正确 HAL 目录，并已重新应用这段兼容补丁。

当前实际修改点：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/esp-hal-3rdparty/nuttx/src/platform/os.c
```

补丁内容：

```c
#include <fcntl.h>
#include <spawn.h>

posix_spawnattr_t attr;

posix_spawnattr_init(&attr);
posix_spawnattr_setstacksize(&attr, (size_t)stack_size);
posix_spawnattr_setpriority(&attr, priority);

ret = nxtask_init(tcb, name, task_wrapper_entry, NULL, &attr, argv, NULL);
posix_spawnattr_destroy(&attr);
```

### 5. `riscv_mtimer.c` 与 oneshot API 不匹配

报错位置：

```text
common/riscv_mtimer.c:66:4:
error: 'const struct oneshot_operations_s' has no member named 'start_absolute'
```

原因是 `riscv_mtimer.c` 使用 `CONFIG_ONESHOT_COUNT` 的新计数接口，但当前 `nsh` 配置没有启用 `CONFIG_ONESHOT`，导致 `nuttx/include/nuttx/timers/oneshot.h` 暴露的是旧 `timespec` 接口。

已做两处处理：

1. 在芯片 Kconfig 中为 ESP32-P4 选择 oneshot count 接口：

```kconfig
select ONESHOT
select ONESHOT_COUNT
select ONESHOT_FAST_DIVISION
```

2. 在当前验证目标 `nsh/defconfig` 中显式补：

```ini
CONFIG_ONESHOT=y
CONFIG_ONESHOT_COUNT=y
CONFIG_ONESHOT_FAST_DIVISION=y
```

修改文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/Kconfig
contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig
```

### 历史停止点：`esp-hal-3rdparty` 目录缺失

再次构建时，配置刷新后触发了 HAL 依赖规则：

```text
Cloning Espressif HAL for 3rd Party Platforms
fatal: Could not resolve host: github.com
```

当前失败不是新的 C 编译错误，而是：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/esp-hal-3rdparty
```

目录不存在，Make 规则尝试从 GitHub 克隆，但当前环境无法访问网络。

下一步需要先恢复该目录。有两种方式：

1. 在有网络的环境下重新执行构建，让 Make 自动 clone。
2. 从本地已有的正确版本 `esp-hal-3rdparty` 副本复制或链接到 `contest2026_031_niudanxianqianchong/chips/esp32p4/esp-hal-3rdparty`。

正确版本至少应包含：

```text
nuttx/esp32p4/include/sdkconfig.h
nuttx/src/platform/os.c
components/esp_system/port/soc/esp32p4/clk.c
```

当前在 `/home/arongw/Documents/esp-hal-3rdparty` 找到的副本不满足要求：它缺少 `nuttx/esp32p4` 和 `nuttx/src/platform/os.c`，因此已被挪到：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/esp-hal-3rdparty.incomplete-local-copy
```

不要把这个旧副本作为 P4 构建依赖使用。

恢复该目录后，再继续执行：

```bash
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

预期下一轮才会继续暴露真正的编译问题。

## 2026-07-05：链接脚本预处理再次触发 flash clock 检查

### 报错现象

```text
/home/arongw/openvela/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h:933:4:
error: #error "SPI timing flash clock is invalid"

make[1]: *** [Makefile:185：
/home/arongw/openvela/nuttx/../contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/../common/scripts/esp32p4_flat_memory.ld.tmp] 错误 1
```

这次和前面 `esp_vectors.S` 触发的同名错误不同。当前失败点不是 C 文件或汇编文件，而是生成链接脚本临时文件：

```text
esp32p4_flat_memory.ld.tmp
```

相关 include 链路：

```text
board/esp32p4/common/scripts/esp32p4_flat_memory.ld
  -> #include "common.ld"
    -> #include "sdkconfig.h"
      -> 检查 CONFIG_ESPRESSIF_FLASH_FREQ_80M
```

### 原因判断

前面已经在 `ESPRESSIF_SDKCONFIG_DEFINES` 中补了：

```make
CONFIG_ESPRESSIF_FLASH_16M=1
CONFIG_ESPRESSIF_FLASH_MODE_DIO=1
CONFIG_ESPRESSIF_FLASH_FREQ_80M=1
```

但当时只追加到了：

```make
CFLAGS
AFLAGS
```

因此 C 文件和汇编文件可以看到这些宏，但链接脚本预处理使用的是：

```make
CPPFLAGS
```

板级 `scripts/Make.defs` 中当前定义为：

```make
CPPFLAGS := $(ARCHINCLUDES) $(ARCHDEFINES) $(EXTRAFLAGS)
```

这条路径没有包含 `ESPRESSIF_SDKCONFIG_DEFINES`，所以 `sdkconfig.h` 在预处理 `.ld` 文件时再次看不到 `CONFIG_ESPRESSIF_FLASH_FREQ_80M`。

### 最小补丁

继续使用同一组 Make 侧 SDK 兜底宏，并让它覆盖链接脚本预处理路径：

```make
CFLAGS += $(ESPRESSIF_SDKCONFIG_DEFINES)
CPPFLAGS += $(ESPRESSIF_SDKCONFIG_DEFINES)
AFLAGS += $(ESPRESSIF_SDKCONFIG_DEFINES)
```

修改文件：

```text
contest2026_031_niudanxianqianchong/chips/esp32p4/common/espressif/Make.defs
```

这样同一组 ESP-IDF 风格 SDK 宏会同时覆盖：

```text
CFLAGS   -> C 文件
AFLAGS   -> 汇编文件
CPPFLAGS -> 链接脚本 .ld 预处理
```

### 后续建议

这仍然是最小补丁路线。更规范的长期方案，是把 `CONFIG_ESPRESSIF_FLASH_FREQ_80M`、`CONFIG_ESPRESSIF_FLASH_MODE_DIO`、`CONFIG_ESPRESSIF_CPU_FREQ_MHZ` 等配置通过 Kconfig 正式进入 `.config` 和 `config.h`，减少 Make 侧手动 `-D` 兜底。
