# smart_home ESP32-S3-BOX 开发记录

本文记录 `smart_home` 在 ESP32-S3-BOX-3/openvela 上联调过程中遇到的关键问题、根因和当前修复方案。

## 1. HTTPS 调用云端 LLM 的熵源问题

### 1.1 现象

在 BOX-3 上 Wi-Fi、DNS 和 TCP 443 已经可用后，执行：

```sh
smart_home "打开客厅灯，亮度35%"
```

早期只看到 cAGENT 返回通用网络错误：

```text
MODEL_RESP  | iter=1  | err=-10 | network
ERROR (-10): no detail
```

`ping api.deepseek.com` 能解析并收到 ICMP 回包，`telnet 103.220.64.100 443` 也没有立刻报错，因此问题不在基础 IP 路由、DNS 或 TCP 端口连通性。

给 `packages/cAGENT/src/runtime/runtime_openvela.c` 临时加入 TLS/HTTP 诊断输出后，真实失败点变为：

```text
[cagent_ov] http_post POST api.deepseek.com:443/v1/chat/completions body=5129 resp_cap=8192 timeout=30000
[cagent_ov] ctr_drbg_seed ret=-0x0034: CTR_DRBG - The entropy source failed
[cagent_ov] http_post connect failed err=-10
```

也就是说，此时还没有进入 TCP 连接和 TLS 握手，失败发生在：

```c
mbedtls_ctr_drbg_seed()
```

### 1.2 根因

`smart_home -> cAGENT -> runtime_openvela.c -> HTTPS` 这条路径使用的是 apps 侧 mbedTLS：

```text
apps/crypto/mbedtls
```

BOX-3 开启 Wi-Fi 时，ESP32-S3 wireless 构建也会引入 ESP HAL 3rdparty 中的 mbedTLS 相关源码和头文件：

```text
nuttx/arch/xtensa/src/esp32s3/Wireless.mk
  -> esp-hal-3rdparty/components/mbedtls
```

但本次运行时错误来自 cAGENT 的 HTTPS runtime，即 apps mbedTLS 的 DRBG 初始化。具体原因是 mbedTLS entropy 层没有成功拿到可用熵源。

ESP32-S3 侧已有硬件随机数路径：

```text
esp_random()
  -> esp32s3_rng.c
  -> /dev/random
```

openvela apps mbedTLS 也提供了硬件熵源适配：

```text
apps/crypto/mbedtls/source/entropy_alt.c
  -> mbedtls_hardware_poll()
  -> open("/dev/random")
```

但是如果没有打开 `CONFIG_MBEDTLS_ENTROPY_HARDWARE_ALT`，mbedTLS 不会走这个 `/dev/random` 熵源，最终导致 `mbedtls_ctr_drbg_seed()` 返回 `CTR_DRBG - The entropy source failed`。

### 1.3 修复方案

在 BOX-3 专用 smart_home defconfig 中启用 ESP32-S3 随机数设备和 apps mbedTLS 硬件熵源：

```text
openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

关键配置：

```text
CONFIG_DEV_RANDOM=y
CONFIG_ESP32S3_RNG=y
CONFIG_MBEDTLS_ENTROPY_HARDWARE_ALT=y
```

修复后的熵源链路为：

```text
ESP32-S3 esp_random()
  -> /dev/random
  -> mbedtls_hardware_poll()
  -> mbedtls_entropy_func()
  -> mbedtls_ctr_drbg_seed()
  -> HTTPS TLS handshake
```

### 1.4 重新编译和烧录

```sh
cd openvela
source build/envsetup.sh

./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)

cd nuttx
make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600
```

### 1.5 验证结果

修复后再次执行：

```sh
smart_home "打开客厅灯，亮度35%"
```

板端日志显示 `ctr_drbg_seed` 已通过，TLS 和 HTTP 都正常：

```text
[cagent_ov] http_post POST api.deepseek.com:443/v1/chat/completions body=5129 resp_cap=8192 timeout=30000
[cagent_ov] tcp connected api.deepseek.com:443 fd=3
[cagent_ov] TLS handshake OK: TLSv1.2 / TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384
[cagent_ov] HTTP status 200 header_bytes=491
[cagent_ov] http_post done status=200 body_len=917
```

Agent 完整执行两轮模型请求和一次工具调用：

```text
MODEL_RESP  | iter=1  | err=0 | ok
TOOL_CALL   | iter=1  | tool=set_light
TOOL_RES    | iter=1  | tool=set_light | err=0
MODEL_RESP  | iter=2  | err=0 | ok
RUN_DONE    | iter=0  | err=0 | done
```

最终输出：

```text
assistant: 客厅灯已打开，亮度设置为35%。
```

这说明 BOX-3 上的最小云端 LLM 闭环已经跑通：

```text
NSH 输入
  -> smart_home
  -> cAGENT
  -> apps mbedTLS HTTPS
  -> DeepSeek OpenAI-compatible API
  -> tool call
  -> set_light
  -> assistant 最终回复
```

## 2. 临时 TLS 诊断输出

为了定位 `AGENT_ERROR_NETWORK=-10`，当前在 `runtime_openvela.c` 中临时加入了控制台诊断输出，覆盖以下关键阶段：

```text
http_post request
ctr_drbg_seed
net_connect
ssl_config_defaults
ssl_setup
ssl_set_hostname
ssl_handshake
ssl_write header/body
ssl_read header/body
HTTP status
```

这些输出对 BOX-3 首轮 bring-up 很有价值，因为板端 `dmesg` 不一定可用，原本写到 `syslog` 的 mbedTLS 错误不容易看到。

后续如果希望收敛为正式代码，可以考虑：

```text
CONFIG_CAGENT_RUNTIME_OPENVELA_TLS_DEBUG
```

或类似 Kconfig 开关，把 `printf("[cagent_ov] ...")` 包起来。比赛演示阶段可以先保留，便于现场排查网络和 TLS 问题。

## 3. 后续排障顺序

如果再次出现：

```text
MODEL_RESP | err=-10 | network
```

建议按以下顺序判断：

1. `ping api.deepseek.com`：确认 DNS 和基础网络。
2. 观察 `[cagent_ov] ctr_drbg_seed`：如果失败，检查 `CONFIG_DEV_RANDOM`、`CONFIG_ESP32S3_RNG`、`CONFIG_MBEDTLS_ENTROPY_HARDWARE_ALT`。
3. 观察 `[cagent_ov] tcp connected`：如果没有出现，优先查 DNS/TCP/路由。
4. 观察 `[cagent_ov] ssl_handshake`：如果失败，再查证书校验、系统时间、TLS 版本和 mbedTLS 配置。
5. 观察 `[cagent_ov] HTTP status`：如果不是 2xx，优先查 API Key、模型名、请求体和服务端错误响应。
6. 如果 HTTP 200 但 Agent 解析失败，再查 cAGENT 的 OpenAI-compatible JSON 解析和 response buffer 大小。

## 4. BOX-3 移植问题汇总

除第 1 节的 apps mbedTLS 熵源问题外，本轮 `smart_home` 迁移到 ESP32-S3-BOX-3 还遇到过以下问题。建议按状态分为三类管理：

| 问题 | 当前状态 | 后续建议 |
|------|----------|----------|
| 构建路径选择 | 已规避 | BOX-3 先走 Make 路径，CMake 板级接入后置 |
| BOX-3 专用 defconfig | 已建立 | 用 `savedefconfig` 固化配置，避免 `.config` 漂移 |
| ESP HAL mbedTLS 与 apps mbedTLS 共存 | 临时规避 | 将三个脚本沉淀为正式补丁或构建前置步骤 |
| Wi-Fi 关联、DHCP 与路由 | 已验证但需保留排障流程 | 记录 WAPI 配网顺序、热点要求和连通性检查 |
| `/data/res/skills` 资源缺失 | 已解决 | 固化 LittleFS 镜像生成和烧录流程 |
| LittleFS 分区烧录端口 | 已解决 | 文档统一使用实际端口 `/dev/ttyACM0` |
| 板端诊断能力不足 | 临时规避 | 给 TLS 诊断输出加 Kconfig 开关 |
| 系统时间与证书校验 | 潜在问题 | 后续启用 SNTP/date 或补时间设置接口 |
| LVGL 触摸屏无法工作 | 已解决 | 见第 6 节；defconfig 补初始化选项、调换 bringup 顺序、设置 maxpoint |
| 字体锯齿与图标不显示 | 已解决 | 见第 7 节；补齐 Montserrat 14、启用 LVGL FS_POSIX |
| Chat 发送后卡住 | 已解决 | 见第 8 节；非阻塞 connect、强制 AF_INET、增大线程栈 |
| `savedefconfig` 覆盖配置 | 已规避 | 见第 9.1 节；构建后手动恢复关键选项 |
| `distclean` 触发 HAL 重新 clone | 已规避 | 见第 9.2 节；用 `rm .config` 替代，HAL 和补丁自动保留 |

### 4.1 构建路径选择

最初尝试使用：

```sh
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  --cmake menuconfig
```

遇到两个阶段的问题：

```text
Please distclean previous make build with `make distclean`
No CMakeLists.txt found at .../esp32s3-box-3
```

第一个问题来自 Make 构建和 CMake 构建的输出状态混用；第二个问题是 BOX-3 自定义板当前没有板级 `CMakeLists.txt`。该板级目录目前主要是 Make 风格接入：

```text
Kconfig
scripts/Make.defs
src/Make.defs
configs/smart_home/defconfig
```

当前处理策略：

```sh
cd openvela
source build/envsetup.sh

./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)
```

后续如果要统一使用 `--cmake`，需要补齐 BOX-3 板级 `CMakeLists.txt`，并验证 board `src/`、链接脚本、启动文件和 common board 逻辑都能被 CMake 收集。

### 4.2 BOX-3 专用 defconfig

`smart_home` 不直接复用通用 BOX 配置，而是使用独立配置：

```text
openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

该配置需要同时覆盖：

```text
CONFIG_SMART_HOME_DEMO
CONFIG_CAGENT
CONFIG_CAGENT_MODEL_OPENAI
CONFIG_CAGENT_RUNTIME_OPENVELA
CONFIG_CAGENT_RUNTIME_OPENVELA_TLS
CONFIG_ESP32S3_WIFI
CONFIG_WIRELESS_WAPI
CONFIG_NETDB_DNSCLIENT
CONFIG_CRYPTO_MBEDTLS
CONFIG_ESP32S3_SPIFLASH_LITTLEFS
CONFIG_ESP32S3_SPIFLASH_LITTLEFS_MOUNTPT="/data"
```

配置变更后要及时固化：

```sh
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  savedefconfig
```

否则容易出现当前 `.config` 可用，但重新拉起构建或换机器后 defconfig 缺项的问题。

### 4.3 ESP HAL mbedTLS 与 apps mbedTLS 共存

BOX-3 开启 Wi-Fi 后，ESP HAL 会引入自己的 mbedTLS 相关源码和头文件；cAGENT HTTPS 又启用了 apps 侧 `CONFIG_CRYPTO_MBEDTLS`。两套 mbedTLS 的配置宏、结构体和头文件优先级不完全兼容。

本轮使用三个临时脚本处理：

```text
openvela_smarthome/scripts/fix_box3_mbedtls_header_priority.sh
openvela_smarthome/scripts/fix_box3_mbedtls_disable_ccm.sh
openvela_smarthome/scripts/fix_box3_spinlock_initializer.sh
```

三类问题分别是：

```text
apps mbedTLS 头文件优先级过高
ESP HAL 与 apps mbedTLS 的 CCM 结构体定义不兼容
ESP HAL 用 0 初始化 NuttX struct spinlock_t
```

这些脚本目前是临时方案。`distclean` 后 ESP HAL 目录可能被重新生成，补丁需要重新应用。后续应考虑把修复沉淀为：

```text
正式 patch
构建前置脚本
或 BOX-3 smart_home bring-up 脚本
```

### 4.4 Wi-Fi 关联、DHCP 与路由

调试早期出现过：

```text
ifconfig wlan0
  inet addr:10.0.0.2 DRaddr:10.0.0.1

ping 8.8.8.8
  ERROR: sendto failed at seqno 0: 101

renew wlan0
  ERROR: netlib_obtain_ipv4addr() failed
```

同时 `wapi show wlan0` 里 AP 显示为：

```text
AP: ff:ff:ff:ff:ff:ff
```

这通常说明接口上看起来有 IP，但 Wi-Fi 还没有真正完成关联，或者 DHCP/默认路由状态没有进入可通信状态。后续排查顺序建议为：

```sh
wapi scan wlan0
wapi psk wlan0 <password> 3
wapi essid wlan0 <ssid> 1
ifconfig wlan0
ping 8.8.8.8
ping api.deepseek.com
```

如果 `ping api.deepseek.com` 能解析并稳定回包，才进入 TLS/LLM 调试阶段。

### 4.5 `/data/res/skills` 资源缺失

早期执行：

```sh
smart_home "打开客厅灯，亮度35%"
```

出现：

```text
smart_home_agent_app_init failed: -13
```

这是应用初始化阶段找不到技能资源导致的。模拟器可以通过 ADB 推送：

```text
res/skills/*.md -> /data/res/skills/
```

但 ESP32-S3-BOX-3 没有 ADB，因此改用 LittleFS 资源分区：

```text
res/skills/*.md
  -> data_lfs.bin
  -> flash 0xE00000
  -> /data/res/skills/
```

资源镜像生成脚本：

```sh
bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
```

烧录示例：

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash \
  0xE00000 out/box3_littlefs_data/data_lfs.bin
```

### 4.6 LittleFS 烧录端口

生成 `data_lfs.bin` 后，曾使用：

```sh
--port /dev/ttyUSB0
```

烧录失败：

```text
Could not open /dev/ttyUSB0, the port is busy or doesn't exist
```

实际设备节点是：

```text
/dev/ttyACM0
```

后续文档和脚本提示应统一使用 `/dev/ttyACM0`，或者先执行：

```sh
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

确认当前串口。

### 4.7 板端诊断能力不足

BOX-3 NSH 上不是所有 Linux 常用命令都存在。本轮确认过：

```text
dmesg: open failed: 2
ntpdate: command not found
sntp: command not found
date: command not found
```

因此只依赖 `syslog` 不够，运行期错误容易被折叠成：

```text
ERROR (-10): no detail
```

当前临时方案是在 `runtime_openvela.c` 中直接打印 `[cagent_ov]` 诊断。后续建议新增类似：

```text
CONFIG_CAGENT_RUNTIME_OPENVELA_TLS_DEBUG
```

将 TLS/HTTP 诊断输出变成可开关能力。

### 4.8 系统时间与证书校验

系统时间不正确可能影响严格证书校验，但这不是本次 `ctr_drbg_seed` 失败的原因。本次失败发生在 TLS 握手之前；启用硬件熵源后已经成功：

```text
TLS handshake OK: TLSv1.2 / TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384
HTTP status 200
```

不过后续如果从：

```text
MBEDTLS_SSL_VERIFY_OPTIONAL
```

收紧到严格 CA 校验，或者接入对证书时间更敏感的 HTTPS 服务，就需要补齐时间同步能力。可选路线：

```text
启用 SNTP/ntpdate 命令
应用启动时设置可信编译时间
通过 Wi-Fi 联网后调用 SNTP API
演示阶段保持 VERIFY_OPTIONAL，但明确记录安全边界
```

## 5. 开启 smart_home LVGL UI

### 5.1 显示屏型号与驱动选择

ESP32-S3-BOX-3 自定义板使用的是 ILI9342C-compatible LCD。当前板级 Kconfig 中：

```text
CONFIG_ESP32S3_BOX_LCD
  -> select LCD_ILI9341
```

因此 BOX-3 开 smart_home UI 时走 `LCD_ILI9341` 路线，不使用普通 ESP32-S3-BOX 的 ST7789 配置。不要照搬：

```text
CONFIG_LCD_ST7789_*
```

本项目应使用：

```text
CONFIG_ESP32S3_BOX_LCD=y
CONFIG_LCD_ILI9341=y
CONFIG_LCD_ILI9341_IFACE0=y
CONFIG_LCD_RPORTRAIT=y
```

触摸屏为 BOX-3 板级 GT911 路线，由：

```text
CONFIG_ESP32S3_BOARD_TOUCHSCREEN=y
```

启用。

### 5.2 defconfig 配置

当前已在 BOX-3 `smart_home/defconfig` 中开启 LVGL UI：

```text
openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

关键项：

```text
CONFIG_ESP32S3_BOARD_TOUCHSCREEN=y
CONFIG_ESP32S3_BOX_LCD=y
CONFIG_ESP32S3_I2C0_SCLPIN=18
CONFIG_ESP32S3_I2C0_SDAPIN=8
CONFIG_ESP32S3_SPI2_CLKPIN=7
CONFIG_ESP32S3_SPI2_CSPIN=5
CONFIG_ESP32S3_SPI2_MISOPIN=21
CONFIG_ESP32S3_SPI2_MOSIPIN=6
CONFIG_ESP32S3_SPI_SWCS=y

CONFIG_GRAPHICS_LVGL=y
CONFIG_LCD_ILI9341=y
CONFIG_LCD_ILI9341_IFACE0=y
CONFIG_LCD_RPORTRAIT=y
CONFIG_LV_NUTTX_LCD_DOUBLE_BUFFER=y
CONFIG_LV_USE_NUTTX=y
CONFIG_LV_USE_NUTTX_LCD=y
CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y
CONFIG_LV_USE_LODEPNG=y

CONFIG_SMART_HOME_DEMO_UI_LVGL=y
```

同时保留 HTTPS 已验证过的熵源配置：

```text
CONFIG_DEV_RANDOM=y
CONFIG_ESP32S3_RNG=y
CONFIG_MBEDTLS_ENTROPY_HARDWARE_ALT=y
```

### 5.3 UI 资源镜像

Console 最小闭环只需要：

```text
/data/res/skills/*.md
```

LVGL UI 还会加载 PNG 图标：

```text
/data/res/icons/*.png
```

因此生成 LittleFS 镜像时要打开 `WITH_ICONS=1`：

```sh
cd openvela
WITH_ICONS=1 bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
```

烧录资源分区：

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash \
  0xE00000 out/box3_littlefs_data/data_lfs.bin
```

字体资源是可选项。当前 UI 代码在找不到：

```text
/data/res/fonts/MiSans-Normal.ttf
```

时会回退到 LVGL Montserrat 内置字体。第一轮亮屏不强制启用 FreeType。

### 5.4 编译和烧录固件

```sh
cd openvela
source build/envsetup.sh

./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)

cd nuttx
make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=921600
```

### 5.5 运行

启动后进入 NSH：

```sh
smart_home
```

如果 UI backend 已切到 LVGL，`smart_home` 应初始化屏幕、触摸、面板页、聊天页和设置页。若仍进入 console prompt：

```text
home>
```

说明最终 `.config` 仍选择了：

```text
CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y
```

需要重新检查 choice 配置并执行 `savedefconfig`。

## 6. ESP32-S3-BOX-3 触摸屏无法工作

### 6.1 现象

启用 LVGL UI 后启动 `smart_home`，屏幕正常显示但触摸完全无响应。串口日志中 indev 为 NULL：

```text
[smart_home_lvgl] disp=0x3fcc8e98 indev=0 input=/dev/input0
```

`disp` 正常创建，但 `indev=0` 说明 LVGL 没有成功创建触摸输入设备。

### 6.2 根因分析

排查过程中发现了三个叠加的问题：

#### 问题 1：`CONFIG_BOARD_LATE_INITIALIZE` 未启用

`esp32s3_bringup()` 是整个板级初始化（LCD、I2C、触摸屏）的入口。它通过 `board_late_initialize()` 调用，但该函数受条件编译保护：

```c
// esp32s3_boot.c
#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  esp32s3_bringup();
}
#endif
```

defconfig 中缺少 `CONFIG_BOARD_LATE_INITIALIZE=y` 和 `CONFIG_BOARDCTL=y`，导致 `esp32s3_bringup()` 从未执行，LCD 和触摸屏驱动都没有初始化。

#### 问题 2：初始化顺序导致 GT911 I2C 地址丢失

GT911 触摸控制器和 ILI9341 LCD 共享 GPIO 48 作为复位引脚。GT911 在复位释放时采样 INT 引脚（GPIO 3）电平来决定 I2C 地址：

| GPIO 3 电平 | I2C 地址 |
|------------|---------|
| LOW | 0x5d |
| HIGH | 0x24 |

原始 bringup 代码先初始化触摸屏、后初始化 LCD：

```c
// 原始顺序（有问题）
board_touchscreen_initialize();  // GPIO 3=LOW → 复位 GPIO 48 → GT911 选择 0x5d
board_lcd_initialize();          // LCD 驱动再次复位 GPIO 48 → GPIO 3 已变为 INPUT_PULLUP(HIGH)
                                 // → GT911 切换到 0x24，但驱动仍用 0x5d 通信 → 失败
```

与 ESP-IDF 官方 BSP 对比，BSP 采用相反的顺序（LCD 先、触摸后），且触摸驱动将 RST 引脚设为 `GPIO_NUM_NC`，不主动管理复位。

#### 问题 3：`touch_lower.maxpoint` 未设置

NuttX 触摸屏框架的 `touch_lowerhalf_s` 结构体有一个 `maxpoint` 字段。LVGL 的 `lv_nuttx_touchscreen_create()` 通过 `ioctl(TSIOC_GETMAXPOINTS)` 查询该值。GT911 驱动没有设置此字段，默认为 0。LVGL 判定 `maxpoint == 0` 为不支持触摸，返回 NULL：

```c
// lv_nuttx_touchscreen.c
if(maxpoint == 0) {
    LV_LOG_ERROR("touchscreen %s unsupported maxpoint %d", dev_path, maxpoint);
    close(fd);
    return NULL;
}
```

### 6.3 修复方案

涉及三个文件的修改：

#### defconfig：启用板级初始化

```text
CONFIG_BOARD_LATE_INITIALIZE=y
CONFIG_BOARDCTL=y
```

#### esp32s3_bringup.c：调换初始化顺序

LCD 先初始化（复位 GPIO 48 时 GT911 一并被复位），触摸后初始化：

```c
#ifdef CONFIG_ESP32S3_BOX_LCD
  ret = board_lcd_initialize();       // LCD + GT911 复位
#endif
#ifdef CONFIG_ESP32S3_BOARD_TOUCHSCREEN
  ret = board_touchscreen_initialize();  // 探测 GT911 地址
#endif
```

#### esp32s3_board_touchsceen_gt911.c：地址探测 + maxpoint

去掉 `gt911_hardware_reset()`（不再管理 GPIO 48），改为运行时探测两个可能的 I2C 地址：

```c
// 先试 0x5d（INT=LOW 时的地址）
dev->i2c_addr = GT911_ADDR;        // 0x5d
if (gt911_verify_communication(dev) == OK) return OK;

// 再试 0x24（INT=HIGH 时的地址）
dev->i2c_addr = GT911_ADDR_BACKUP;  // 0x24
if (gt911_verify_communication(dev) == OK) return OK;
```

注册触摸设备前设置 `maxpoint`：

```c
dev->touch_lower.maxpoint = 1;
touch_register(&dev->touch_lower, GT911_PATH, GT911_SAMPLE_CACHES);
```

### 6.4 修复后的初始化时序

```text
kernel boot
  → board_late_initialize()
    → esp32s3_bringup()
      → board_lcd_initialize()
        → SPI 初始化
        → ILI9341 复位 GPIO 48（LOW → HIGH）
        → GT911 被一并复位，采样 GPIO 3 → 选择 I2C 地址
      → board_touchscreen_initialize()
        → I2C0 初始化
        → gt911_probe() → 尝试 0x5d → 尝试 0x24 → 成功
        → touch_register("/dev/input0", maxpoint=1)
      → smart_home
        → lv_nuttx_init()
          → open("/dev/input0") ✓
          → ioctl(TSIOC_GETMAXPOINTS) → 1 ✓
          → indev ≠ 0 ✓
```

### 6.5 涉及文件

| 文件 | 修改内容 |
|------|----------|
| `board/esp32s3-box-3/configs/smart_home/defconfig` | 添加 `CONFIG_BOARD_LATE_INITIALIZE=y` 和 `CONFIG_BOARDCTL=y` |
| `board/esp32s3-box-3/src/esp32s3_bringup.c` | LCD 先初始化、触摸后初始化 |
| `board/esp32s3-box-3/src/esp32s3_board_touchsceen_gt911.c` | 删除硬件复位；I2C 地址参数化与双地址探测；设置 `maxpoint=1` |

### 6.6 与 ESP-IDF 官方 BSP 的对比

| 维度 | ESP-IDF BSP | openvela（修复后） |
|------|------------|-------------------|
| 初始化顺序 | LCD 先，触摸后 | LCD 先，触摸后 |
| RST 引脚管理 | 触摸设 `GPIO_NUM_NC`，LCD 统一管理 | 触摸不管理 GPIO 48，LCD 驱动负责复位 |
| I2C 地址策略 | 运行时探测 primary + backup | 运行时探测 0x5d + 0x24 |
| maxpoint | 由 BSP 设置 | 在 `touch_register` 前设置为 1 |

## 7. 字体锯齿与图标不显示

### 7.1 字体锯齿

LVGL UI 启用后，文字出现明显锯齿。

#### 根因

`smart_home_lvgl_style.c` 中 `load_font()` 依赖 FreeType 加载 TTF 字体：

```c
static lv_font_t *load_font(int size)
{
#ifdef CONFIG_LV_USE_FREETYPE
    return lv_freetype_font_create(...);
#else
    return NULL;    // ← FreeType 未启用，所有自定义字体返回 NULL
#endif
}
```

defconfig 中没有 `CONFIG_LV_USE_FREETYPE`，因此 `g_style.font_12/14/16/20` 全部为 NULL。`smart_home_lvgl_font()` 回退到内置 `lv_font_montserrat_12`。

代码中大量文字使用 size 14、16，但 defconfig 只有 `CONFIG_LV_FONT_MONTSERRAT_12=y` 和 `CONFIG_LV_FONT_MONTSERRAT_20=y`，缺少 14 号字体。

#### 修复

在 defconfig 中补齐字体：

```text
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y    ← 新增
CONFIG_LV_FONT_MONTSERRAT_20=y
```

LVGL v9 的内置 Montserrat 字体默认使用 4bpp 抗锯齿渲染，不需要额外后缀。

### 7.2 图标不显示

LVGL 面板和导航栏的 PNG 图标全部空白。

#### 根因

两层问题：

**a) 资源未打包**：图标文件需要通过 LittleFS 镜像烧录到 `/data/res/icons/`。生成镜像时需要打开 `WITH_ICONS=1`：

```sh
WITH_ICONS=1 bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
```

**b) LVGL 文件系统驱动未启用**：即使 PNG 文件存在于 NuttX 文件系统，LVGL 的 `lv_image_set_src(img, "/data/res/icons/xxx.png")` 需要一个文件系统驱动才能读取。defconfig 中缺少 `CONFIG_LV_USE_FS_POSIX`。

#### 修复

defconfig 中添加：

```text
CONFIG_LV_USE_FS_POSIX=y
CONFIG_LV_FS_POSIX_LETTER=65      # 65 = 'A'，盘符前缀
```

LVGL 的 POSIX 文件系统驱动要求路径带盘符前缀（如 `A:/data/res/icons/xxx.png`）。`LV_FS_POSIX_LETTER` 不能为 0（编译报错 `LV_FS_POSIX_LETTER must be set to a valid value`）。

修改 `smart_home_lvgl_style.c`，将 `access()` 检查和 `lv_image_set_src()` 的路径分离：

```c
/* POSIX 路径用于 access() 检查文件存在 */
snprintf(posix_path, sizeof(posix_path),
         "%s/%s.png", SMART_HOME_ICONS_ROOT, filename);
if (access(posix_path, F_OK) != 0) { ... }

/* LVGL 路径带盘符前缀 */
snprintf(lvgl_path, sizeof(lvgl_path),
         SMART_HOME_LV_FS_PREFIX "%s", posix_path);
lv_image_set_src(img, strdup(lvgl_path));
```

### 7.3 涉及文件

| 文件 | 修改内容 |
|------|----------|
| `defconfig` | 添加 `LV_FONT_MONTSERRAT_14`、`LV_USE_FS_POSIX`、`LV_FS_POSIX_LETTER=65` |
| `smart_home_lvgl_style.c` | 图标路径分离 POSIX 检查与 LVGL 加载 |

## 8. Chat 发送后卡住（HTTP 请求阻塞）

### 8.1 现象

LVGL Chat 界面发送消息后，状态一直显示 "Thinking..."，无响应。串口日志截断在：

```text
[cagent_ov] http_post POST ap
```

### 8.2 根因分析

排查过程中发现了三个叠加的问题：

#### 问题 1：`connect()` 无超时保护

`runtime_openvela.c` 中原来使用 `mbedtls_net_connect()` 建立 TCP 连接。该函数内部调用阻塞式 `connect()`，在 NuttX 上如果目标端口被中间设备过滤（无 SYN-ACK、无 RST），会**永久阻塞**。

`SO_RCVTIMEO`/`SO_SNDTIMEO` 在 `connect()` 之后才设置，无法保护连接阶段。

#### 问题 2：DNS 返回 IPv6 地址

defconfig 启用了 `CONFIG_NET_IPv6=y`。`getaddrinfo()` 可能返回 IPv6（AAAA）地址，但设备只有 IPv4 连接 → `socket(AF_INET6)` → `connect()` 失败或卡住。

#### 问题 3：pthread 栈溢出

`smart_home_lvgl_agent.c` 中 Agent worker 线程使用默认栈大小（`CONFIG_PTHREAD_STACK_DEFAULT=4096`）。mbedTLS TLS 握手需要 ~16KB 栈空间，导致栈溢出崩溃。主进程 `CONFIG_INIT_STACKSIZE=8192` 加上 `output[4096]` 栈上缓冲区也不够。

Console 模式之前能跑通是因为 HTTP 请求在主进程栈上执行，当时栈可能更大或 LVGL 未编译。

### 8.3 修复方案

#### runtime_openvela.c：非阻塞 connect + 超时

替换 `mbedtls_net_connect()` 为手动 socket + 非阻塞 connect + select 超时：

```c
hints.ai_family = AF_INET;              // 强制 IPv4
getaddrinfo(host, port, &hints, &res);  // DNS 解析
fd = socket(...);                        // 创建 socket
fcntl(fd, F_SETFL, O_NONBLOCK);         // 非阻塞
connect(fd, ...);                        // 立即返回 EINPROGRESS
select(fd+1, NULL, &wfds, NULL, &tv);   // 带超时等待
getsockopt(fd, SOL_SOCKET, SO_ERROR);   // 检查结果
fcntl(fd, F_SETFL, 0);                  // 恢复阻塞
setsockopt(SO_RCVTIMEO, SO_SNDTIMEO);   // 设置读写超时
```

每步添加 `fflush(stdout)` 确保诊断日志在阻塞前输出。

#### smart_home_lvgl_agent.c：增大 worker 线程栈

```c
pthread_attr_t attr;
pthread_attr_init(&attr);
pthread_attr_setstacksize(&attr, 65536);  // 64KB
pthread_create(&tid, &attr, agent_worker_main, job);
pthread_attr_destroy(&attr);
```

#### defconfig：增大主进程栈

```text
CONFIG_INIT_STACKSIZE=32768    # 8192 → 32768
```

#### smart_home_main.c：带参数时跳过 LVGL

```c
#ifdef CONFIG_SMART_HOME_DEMO_UI_LVGL
    if (argc > 1) {
        ret = smart_home_ui_run_once(&app, argv[1]);  // console 路径
    } else {
        ret = smart_home_lvgl_run(&app, NULL);         // LVGL 路径
    }
#endif
```

`smart_home "text"` 走 console 路径，不初始化 LVGL，节省内存。

### 8.4 涉及文件

| 文件 | 修改内容 |
|------|----------|
| `packages/cAGENT/src/runtime/runtime_openvela.c` | 非阻塞 connect + select 超时；强制 AF_INET；诊断日志 |
| `demos/smart_home/src/ui/lvgl/smart_home_lvgl_agent.c` | worker 线程栈 4KB → 64KB |
| `demos/smart_home/src/app/smart_home_main.c` | 带参数时走 console 路径 |
| `defconfig` | `CONFIG_INIT_STACKSIZE=32768` |

## 9. 构建相关问题

### 9.1 `savedefconfig` 覆盖手动修改

构建脚本 `build.sh` 在编译后自动执行 `make savedefconfig`，会重新生成 defconfig。以下手动添加的配置会被覆盖或修改：

| 配置 | 被覆盖为 | 原因 |
|------|----------|------|
| `CONFIG_SMART_HOME_DEMO_UI_LVGL=y` | `CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y` | Kconfig choice 默认 CONSOLE |
| `CONFIG_BOARDCTL=y` | 被删除 | 由其他选项自动 select |
| `CONFIG_LV_FONT_MONTSERRAT_14=y` | 被删除 | `savedefconfig` 认为是默认值 |

#### 规避方法

- 修改 defconfig 后，先执行 `make clean`（不删 HAL），再构建
- 如果 `savedefconfig` 覆盖了关键选项，构建后手动恢复
- 或者用 `make menuconfig` 修改配置，让 `savedefconfig` 正确保存

### 9.2 `make distclean` 删除 ESP HAL

`make distclean` 会删除 `nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty` 目录。下次构建时重新 clone（~200MB），三个 mbedTLS 兼容补丁也需要重新应用。

#### 规避方法

**不要用 `distclean`**，除非 HAL 版本需要升级：

| 需求 | 命令 | HAL 保留 | 补丁保留 | .o 保留 | 速度 |
|------|------|----------|----------|---------|------|
| 重新配置 | `rm .config` + 重新构建 | ✓ | ✓ | ✗ | **快** |
| 清除编译产物 | `make clean` + 重新构建 | ✓ | ✓ | ✗ | 快 |
| 完全重来 | `make distclean` + 重新构建 | ✗ | ✗ | ✗ | **慢** |

`make distclean` 实际上**不会删除 HAL 物理目录**（只删除 `chip` 符号链接），HAL 文件仍在 `nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty/`。但如果 `distclean` 后重新构建，构建系统会重新创建符号链接并可能重新 clone（取决于 `STORAGETMP` 配置）。

**日常开发推荐用 `rm .config`**，保留 HAL 和补丁，只需要重新编译：

```sh
# 推荐（HAL 和补丁保留，只重新配置和编译）
rm nuttx/.config
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ -j8

# 需要清除 .o 时（HAL 和补丁仍然保留）
cd nuttx && make clean && cd ..
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ -j8
```

补丁脚本只需在首次 clone HAL 后运行一次。`rm .config` 方式不会触发重新 clone，补丁自动保留：

```sh
# 首次构建（HAL 需要 clone，补丁需要并行运行）
# 终端 1：启动构建
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ -j8

# 终端 2：并行运行补丁（等待 HAL 目录出现后自动应用）
bash openvela_smarthome/scripts/fix_box3_mbedtls_header_priority.sh
bash openvela_smarthome/scripts/fix_box3_mbedtls_disable_ccm.sh &
bash openvela_smarthome/scripts/fix_box3_spinlock_initializer.sh &

# 后续构建（HAL 和补丁已存在，直接重新配置编译）
rm nuttx/.config
./build.sh vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ -j8
```

## 10. LVGL 模式与局域网/LLM 联调隔离（2026-08）

### 10.1 现象

BOX-3 已通过 `wapi.conf` 连接局域网，`wlan0` 地址为
`192.168.1.103`。在 LVGL 模式运行 `smart_home` 后，电脑无法继续
ping 该地址；在临时 Console 模式运行同一应用时，ping 保持稳定。

为排除启动时重连 Wi-Fi 的可能性，在 `smart_home_network_init()` 的
入口和退出处增加了临时串口日志。LVGL 模式的记录为：

```text
[smart_home_net] init-begin if=wlan0 ip=192.168.1.103 gateway=192.168.1.1 \
  result=0 init=1 ip_status=1 dns=1 online=0
[smart_home_net] init-end if=wlan0 ip=192.168.1.103 gateway=192.168.1.1 \
  result=0 init=0 ip_status=0 dns=0 online=1
```

这证明 Wi-Fi 地址、网关和 DNS 探测在网络初始化阶段均正常；ping
失效发生在其后的应用初始化或 LVGL 运行阶段，而不是 SSID、DHCP 或
默认网关配置错误。

### 10.2 Console 对照组

临时配置保持 App bridge 启用，并将 UI choice 切为 Console：

```text
CONFIG_SMART_HOME_APP_BRIDGE=y
CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y
# CONFIG_SMART_HOME_DEMO_UI_LVGL is not set
```

此模式仍会启动 Wi-Fi、Agent、模型、MCP、技能、工具和 App bridge，
但不会加载 LVGL、FreeType、字体与 LCD 刷新循环。板端日志同样显示
`init-end` 成功，且电脑可持续 ping `192.168.1.103`。

Console 模式还完成了 bridge 的 HTTP 冒烟验证：

```sh
curl -i -H "Authorization: Bearer <app_bridge_token>" \
  http://192.168.1.103:8080/v1/home/snapshot
```

返回 `200 OK`、三台设备快照和 `revision: 0`。随后执行：

```sh
curl -i -X POST http://192.168.1.103:8080/v1/commands \
  -H "Authorization: Bearer <app_bridge_token>" \
  -H "Content-Type: application/json" \
  -d '{"deviceId":"1","action":"set_brightness","value":60}'
```

返回 `202 Accepted` 与 `revision: 1`；再次读取 snapshot 和 history，
设备状态及 `device_state_changed` 事件均一致。这说明局域网、HTTP
监听、鉴权、状态服务和事件历史在非 LVGL 运行时可正常工作。

### 10.3 当前结论与 LLM 假设

已验证的结论：LVGL 是“ping 在应用运行后失效”的必要条件之一；
Agent/MCP/App bridge 的基础启动不是该现象的充分条件。

尚未证实、但优先级很高的假设：此前 LVGL 模式下偶发的云端 LLM
连接失败，可能同样由 LVGL 启动后造成的堆内存、任务栈、lwIP 收包
缓冲或任务调度压力引起。它不能替代第 1 节已修复的 mbedTLS 熵源问题：
两者应作为独立故障分类处理。

尤其不能仅凭 Console 模式的 HTTP 成功，就断言 Console 模式的 LLM
HTTPS 调用也已经成功；必须对同一条 LLM 请求做 A/B 对照。

### 10.4 后续验证矩阵

1. Console 模式执行固定 LLM 提示词，记录 TLS 诊断、HTTP 状态和
   Agent 结果。
2. 仅切换为 LVGL，使用相同后端、网络和提示词，再记录 TLS 诊断。
3. 在 LVGL 启动前、Agent 初始化后、bridge 启动后和 LVGL 首帧后记录
   可用堆、最大连续块、任务栈余量及 `wlan0` 状态。
4. 若仅 LVGL 失败，依次降低 FreeType 字形缓存、draw buffer、应用与
   LVGL worker 栈，观察 ping、HTTP 和 LLM 的恢复点。
5. 使用第二个 NSH 会话（Telnet）或外部网络探针观察运行中状态；
   Console UI 会从同一串口 `stdin` 读取命令，不能与串口 NSH 并行。

在资源数据充分前，不应将问题归因于单一模块，也不应以“增大所有
buffer/stack”作为正式修复；目标是找到最小的资源或调度修正。
