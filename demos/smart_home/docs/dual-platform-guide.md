# smart_home 双平台配置指南

本文档说明如何在 **ESP32-S3-BOX-3 真机**和 **Goldfish 模拟器**上构建和运行 smart_home demo。

两个平台都支持 **LVGL** 和 **Console** 两种 UI 模式，资源路径统一使用 `/data/res/`。

---

## 0. 接入 openvela 工作区

`openvela_smarthome` 作为独立仓库提交。拉取到 openvela 工作区根目录后，需要通过软链接接入 openvela 的标准应用、包和板级配置路径：

```bash
cd openvela
git clone <your-openvela-smarthome-repo-url> openvela_smarthome
source build/envsetup.sh

# 应用和 cAGENT
mkdir -p packages/demos
ln -sfnT ../../openvela_smarthome/demos/smart_home packages/demos/smart_home
ln -sfnT ../openvela_smarthome/packages/cAGENT packages/cAGENT

# Goldfish 模拟器 defconfig
mkdir -p vendor/openvela/boards/vela/configs/goldfish-smart_home
ln -sfn "$(pwd)/openvela_smarthome/board/goldfish-arm64/configs/smart_home/defconfig" \
  vendor/openvela/boards/vela/configs/goldfish-smart_home/defconfig

# ESP32-S3-BOX-3 defconfig
mkdir -p vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home
ln -sfn "$(pwd)/openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig" \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/defconfig
```

如果目标路径已经是普通目录或普通文件，请先确认没有本地改动，再手动备份或移走；上述命令适合目标不存在或已经是软链接的情况。

openvela 工作区中 `apps/packages` 通常是指向 `../packages` 的软链接，因此
`apps/packages/demos/smart_home` 和 `packages/demos/smart_home` 最终等价。本文档统一使用
`packages/demos/smart_home`，更贴近 Kconfig 实际 source 的路径。

软链接验证：

```bash
readlink -f packages/demos/smart_home
readlink -f packages/cAGENT
readlink -f vendor/openvela/boards/vela/configs/goldfish-smart_home/defconfig
readlink -f vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/defconfig
```

---

## 1. 平台对比

| 维度 | ESP32-S3-BOX-3 | Goldfish 模拟器 |
|------|----------------|----------------|
| 构建路径 | `vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/` | `vendor/openvela/boards/vela/configs/goldfish-smart_home/` |
| UI 模式 | LVGL + Console | LVGL + Console |
| 分辨率 | 320×240（紧凑布局） | 480×320 或更大（标准布局） |
| 网络 | WiFi (`wlan0`) | Ethernet (`eth0`) |
| 触摸/显示 | GT911 + ILI9341 LCD | 模拟器 LCD |
| 资源路径 | `/data/res/` | `/data/res/` |
| `/data` 存储 | LittleFS SPI Flash 分区 | `adb push` 推送 |
| Skills 文件 | `/data/res/skills/*.md` | `/data/res/skills/*.md` |
| 图标文件 | `/data/res/icons/*.png` | `/data/res/icons/*.png` |

---

## 2. UI 模式选择

两种模式通过运行参数自动切换，不需要重新编译：

```text
nsh> smart_home                  ← LVGL 模式（无参数）
nsh> smart_home "打开客厅灯"      ← Console 模式（带参数，跳过 LVGL 初始化）
```

defconfig 中通过 choice 选择默认编译哪种 UI 后端：

| 配置 | 说明 |
|------|------|
| `CONFIG_SMART_HOME_DEMO_UI_LVGL=y` | 编译 LVGL UI（需要 LCD + 触摸 + 图标资源） |
| `CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y` | 编译 Console UI（纯文本交互） |

编译 `LVGL` 时，带参数运行仍走 Console 路径（节省内存）。编译 `Console` 时，`smart_home` 无参数进入交互式 prompt（`home>`）。

### 2.1 分辨率适配

UI 代码自动适配不同分辨率：

| 分辨率范围 | 布局模式 | 适用平台 |
|-----------|---------|---------|
| ≤340×260 | 紧凑布局（`compact`） | ESP32-S3-BOX-3（320×240） |
| >340×260 | 标准布局 | Goldfish 模拟器（480×320 或更大） |

Goldfish 模拟器使用更大的分辨率，文字和图标更清晰，交互面积更大。

---

## 3. 资源文件管理

两个平台的资源路径统一为 `/data/res/`：

```text
/data/res/
  skills/          ← Agent 技能文件（5 个 .md）
    smart_home_device_control.md
    smart_home_safety.md
    smart_home_scenes.md
    smart_home_timer.md
    smart_home_weather.md
  icons/           ← LVGL 图标文件（25 个 .png）
    icon_nav_home.png
    icon_nav_chat.png
    ...
```

### 3.1 ESP32-S3-BOX-3：LittleFS 烧录

`/data` 是 SPI Flash 上的 LittleFS 分区，通过镜像烧录：

```bash
# 生成镜像（含图标）
WITH_ICONS=1 bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh

# 烧录到 0xe00000
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write-flash 0xe00000 out/box3_littlefs_data/data_lfs.bin
```

### 3.2 Goldfish 模拟器：adb push

`/data` 通过 ADB 推送资源文件：

```bash
# 推送 skills 和 icons 到模拟器
adb push packages/demos/smart_home/res /data/
```

验证：

```text
goldfish-armv8a-ap> ls /data/res/skills
goldfish-armv8a-ap> ls /data/res/icons
```

---

## 4. ESP32-S3-BOX-3 真机

### 4.1 defconfig 位置

```text
openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

### 4.2 关键配置

```text
# UI
CONFIG_SMART_HOME_DEMO=y
CONFIG_SMART_HOME_DEMO_UI_LVGL=y
CONFIG_SMART_HOME_DEMO_DATA_ROOT="/data"

# 板级初始化
CONFIG_BOARD_LATE_INITIALIZE=y
CONFIG_BOARDCTL=y

# 显示（320×240 紧凑布局）
CONFIG_ESP32S3_BOX_LCD=y
CONFIG_LCD_ILI9341_IFACE0=y
CONFIG_LCD_RPORTRAIT=y
CONFIG_GRAPHICS_LVGL=y
CONFIG_LV_COLOR_16_SWAP=y
CONFIG_LV_USE_NUTTX_LCD=y
CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y
CONFIG_LV_USE_LODEPNG=y
CONFIG_LV_USE_FS_POSIX=y
CONFIG_LV_FS_POSIX_LETTER=65
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y

# 触摸
CONFIG_ESP32S3_BOARD_TOUCHSCREEN=y

# 网络
CONFIG_ESP32S3_WIFI=y
CONFIG_WIRELESS_WAPI=y

# 资源分区
CONFIG_ESP32S3_SPIFLASH=y
CONFIG_ESP32S3_SPIFLASH_LITTLEFS=y
CONFIG_ESP32S3_SPIFLASH_LITTLEFS_MOUNTPT="/data"

# TLS
CONFIG_MBEDTLS_ENTROPY_HARDWARE_ALT=y

# 栈大小
CONFIG_INIT_STACKSIZE=32768
```

### 4.3 构建与烧录

```bash
cd openvela
source build/envsetup.sh

# 构建
rm nuttx/.config
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)

# 烧录固件
cd nuttx && make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600 && cd ..

# 烧录资源（首次，或资源更新后）
WITH_ICONS=1 bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write-flash 0xe00000 out/box3_littlefs_data/data_lfs.bin
```

### 4.4 运行

启动监视
```bash
picocom -b 115200 /dev/ttyACM0
```

板端验证资源和功能：

```text
nsh> ls /data
nsh> ls /data/res/skills
nsh> cat /data/res/skills/smart_home_weather.md
nsh> smart_home "打开客厅灯，亮度35%"
```


```text

nsh> ifup wlan0
nsh> wapi psk wlan0 88888888 3
nsh> wapi essid wlan0 123 1
nsh> renew wlan0

# LVGL 模式
nsh> smart_home

# Console 模式
nsh> smart_home "打开客厅灯，亮度35%"
```

---

## 5. Goldfish 模拟器

### 5.1 构建路径

```text
vendor/openvela/boards/vela/configs/goldfish-smart_home/
```

> 该配置通过软链接将本仓库 Goldfish defconfig 映射到 vela 板配置目录，详见第 7.1 节。

### 5.2 关键配置

```text
# UI
CONFIG_SMART_HOME_DEMO=y
CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y
CONFIG_SMART_HOME_DEMO_DATA_ROOT="/data"

# 网络（Ethernet，不需要 WiFi）
# CONFIG_ESP32S3_WIFI is not set
# CONFIG_WIRELESS_WAPI is not set

# 不需要的组件
# CONFIG_QUICKAPP_VAPP is not set
```

### 5.3 构建与运行

```bash
cd openvela
source build/envsetup.sh

## 或者
source openvela/myenv/bin/activate

# 首次或跨架构切换时，必须 distclean
make -C nuttx distclean

# 构建
./build.sh \
  vendor/openvela/boards/vela/configs/goldfish-smart_home/ \
  -j$(nproc)

# 链接镜像（模拟器需要）
cp nuttx/.config cmake_out/vela_goldfish-arm64-v8a-ap/
cp nuttx/vela_*.bin cmake_out/vela_goldfish-arm64-v8a-ap/
ln -sf ../../nuttx/nuttx cmake_out/vela_goldfish-arm64-v8a-ap/nuttx

# 启动模拟器
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap

## 或者指定宽高
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap \
  -skin 800x480

```

### 5.4 推送资源与运行

```bash
# 推送资源文件到模拟器
adb push packages/demos/smart_home/res /data/
```

```text
# 联网
goldfish-armv8a-ap> ifup eth0
goldfish-armv8a-ap> renew eth0

# Console 模式
goldfish-armv8a-ap> smart_home "打开客厅灯，亮度35%"

# LVGL 模式（需要模拟器 LCD 支持）
goldfish-armv8a-ap> smart_home
```

---

## 6. 切换平台注意事项

### 6.1 必须 `make clean` 清除旧编译产物

**关键**：两个平台架构不同（ESP32-S3 = xtensa，Goldfish = arm64），切换时**必须清除旧的 `.o` 文件**。仅删除 `.config` 不够——旧架构的 `.o` 文件会残留在 `nuttx/` 和 `apps/` 目录中，导致链接错误：

```text
undefined reference to `cJSON_Parse'
undefined reference to `smart_home_main'
```

**切换平台的标准流程**：

```bash
cd openvela/nuttx
make clean                     # 清除所有 .o 文件
rm -f .config                  # 删除旧配置
rm -f include/arch             # 删除架构符号链接
rm -f include/arch/board
rm -f include/arch/chip
cd ..
```

然后构建新平台：

```bash
# 构建 ESP32-S3-BOX-3
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ -j8

# 或构建 Goldfish
./build.sh vendor/openvela/boards/vela/configs/goldfish-smart_home/ -j8
```

**不要复用上一个平台的 `.config` 或 `.o` 文件。**

### 6.2 savedefconfig 行为

`build.sh` 在构建后自动执行 `savedefconfig`，可能覆盖手动添加的配置。构建后检查关键选项：

```bash
grep "SMART_HOME_DEMO_UI_LVGL\|BOARD_LATE_INITIALIZE\|LV_COLOR_16_SWAP" \
  openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

如果关键配置被覆盖，手动恢复。

### 6.3 ESP HAL 补丁

首次构建 ESP32-S3-BOX-3 时，HAL 需要 clone 并应用补丁。后续构建用 `rm .config` 替代 `distclean`，HAL 和补丁自动保留。

```bash
# 终端 1：构建
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)

# 终端 2：补丁（仅首次需要）
bash openvela_smarthome/scripts/fix_box3_mbedtls_header_priority.sh
bash openvela_smarthome/scripts/fix_box3_mbedtls_disable_ccm.sh &
bash openvela_smarthome/scripts/fix_box3_spinlock_initializer.sh &
```

Goldfish 模拟器不需要 HAL 补丁。

### 6.4 资源文件

两个平台的资源路径都是 `/data/res/`，但推送方式不同：

| 平台 | 推送方式 | 更新频率 |
|------|---------|---------|
| ESP32-S3-BOX-3 | LittleFS 镜像烧录到 `0xe00000` | 资源变更后重新烧录 |
| Goldfish | `adb push packages/demos/smart_home/res /data/` | 随时推送 |

---

## 7. 已知问题与解决方案

### 7.1 `File Make.defs could not be found`（Goldfish 构建失败）

**现象**：

```text
File Make.defs could not be found
Error: ############# config .../board/goldfish-arm64/configs/smart_home/ fail ##############
```

**原因**：

本仓库的 `board/goldfish-arm64/` 目录只提供 smart_home 的 `defconfig` 差异，不承载完整 vela 板级构建文件。若直接把该目录作为构建配置传给 `build.sh`，`configure.sh` 会按以下顺序搜索 `Make.defs` 并全部失败：

```text
1. boards/*/*/${boarddir}/configs/${configdir}/Make.defs   ← 路径不匹配 boards/*/*/
2. boards/*/*/${boarddir}/scripts/Make.defs                ← 路径不匹配
3. ${configpath}/Make.defs                                 ← 不存在
4. ${configpath}/../../scripts/Make.defs                   ← 不存在
5. ${configpath}/../../../common/scripts/Make.defs         ← 不存在
```

**解决方案**：

将本仓库的 Goldfish defconfig 软链接到 vela 板的 configs 目录，使用 vela 板的构建文件：

```bash
# 创建配置目录
mkdir -p vendor/openvela/boards/vela/configs/goldfish-smart_home

# 软链接 defconfig
ln -s \
  $(pwd)/openvela_smarthome/board/goldfish-arm64/configs/smart_home/defconfig \
  vendor/openvela/boards/vela/configs/goldfish-smart_home/defconfig

# 使用 vela 板路径构建
make -C nuttx distclean
./build.sh vendor/openvela/boards/vela/configs/goldfish-smart_home/ -j8
```

> defconfig 中已指定 `CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/openvela/boards/vela/"`，因此实际板级文件来自 vela 板；本仓库仅提供 smart_home 的 defconfig 差异。

---

### 7.2 `could not load kernel 'cmake_out/.../nuttx'`（模拟器启动失败）

**现象**：

```text
qemu-system-aarch64: could not load kernel 'cmake_out/vela_goldfish-arm64-v8a-ap/nuttx'
./emulator.sh: 第 128 行： 段错误 （核心已转储）
```

**原因**：

`cmake_out/vela_goldfish-arm64-v8a-ap/nuttx` 是一个相对路径软链接，指向 `nuttx/nuttx`。但该相对路径基于软链接所在目录解析，实际解析为 `cmake_out/vela_goldfish-arm64-v8a-ap/nuttx/nuttx`（不存在），导致断链。

**解决方案**：

修正软链接的相对路径：

```bash
rm cmake_out/vela_goldfish-arm64-v8a-ap/nuttx
ln -s ../../nuttx/nuttx cmake_out/vela_goldfish-arm64-v8a-ap/nuttx

# 验证
readlink -f cmake_out/vela_goldfish-arm64-v8a-ap/nuttx
# 应输出: openvela/nuttx/nuttx
```

同时确保其他构建产物也已复制到 cmake_out：

```bash
cp nuttx/.config cmake_out/vela_goldfish-arm64-v8a-ap/
cp nuttx/vela_*.bin cmake_out/vela_goldfish-arm64-v8a-ap/
```

---

### 7.3 `没有规则可制作目标 "xtensa/core.h"`（跨架构编译错误）

**现象**：

```text
make[1]: *** 没有规则可制作目标"openvela/nuttx/include/arch/xtensa/core.h"，
由"cxa_default_handlers.o" 需求。 停止。
```

**原因**：

从 ESP32-S3（xtensa 架构）切换到 Goldfish（arm64 架构）时，未执行 `distclean`，旧的 xtensa 架构 `.o` 文件和头文件链接残留在构建目录中。libcxxabi 编译时引用了残留的 xtensa 头文件路径。

**解决方案**：

```bash
make -C nuttx distclean
./build.sh vendor/openvela/boards/vela/configs/goldfish-smart_home/ -j8
```

> **提示**：跨架构切换时，`distclean` 比 `make clean` 更彻底——它还会清除 `include/arch` 符号链接和 `.config`。

---

### 7.4 问题速查表

| 问题 | 平台 | 原因 | 解决 |
|------|------|------|------|
| `File Make.defs could not be found` | Goldfish | 直接使用本仓库 `board/goldfish-arm64` 配置路径 | 软链接 defconfig 到 vela 板，见 7.1 |
| `could not load kernel` / 段错误 | Goldfish | cmake_out 软链接断链 | 修正相对路径为 `../../nuttx/nuttx`，见 7.2 |
| `xtensa/core.h` 编译错误 | Goldfish | 跨架构残留 | `distclean` 后重新构建，见 7.3 |
| `undefined reference to cJSON_Parse` / `smart_home_main` | 两者 | 切换平台后旧 `.o` 残留 | `make clean` + `rm .config`，见 6.1 |
| `include/arch/barriers.h` 缺失 | 两者 | 架构符号链接未更新 | 删除 `include/arch*` 后重新构建 |
| `smart_home_agent_app_init failed: -13` | 两者 | `/data/res/skills/` 不存在 | 烧录 LittleFS 或 `adb push` |
| `WiFi connect failed: -2` | Goldfish | 无 WiFi，已改为非致命 | 正常，继续使用 Ethernet |
| 白屏 | ESP32-S3 | 缺少 `LV_COLOR_16_SWAP` | defconfig 添加 `CONFIG_LV_COLOR_16_SWAP=y` |
| `indev=0` | ESP32-S3 | 触摸屏未初始化 | 检查 `BOARD_LATE_INITIALIZE` 和 `maxpoint` |
| TLS EOF | 两者 | 服务端拒绝握手 | 去掉 ALPN、检查 TLS 版本 |
| `undefined reference to vapp_main` | Goldfish | `QUICKAPP_VAPP` 未编译 | `# CONFIG_QUICKAPP_VAPP is not set` |
| `aidl: libc++abi.so.1 not found` | Goldfish 主机 | 缺少系统库 | `sudo apt install libc++abi1` |
| 模拟器分辨率太小 | Goldfish | 默认 HVGA 320×480 | 启动加 `-skin 800x480` |
| UI 元素太小 | ESP32-S3 | 320×240 紧凑布局 | 正常，UI 自动适配小屏 |
