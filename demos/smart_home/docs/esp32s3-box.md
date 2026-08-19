# smart_home 在 ESP32-S3-BOX 上的运行方案

## 1. 目标与实施原则

目标是在 ESP32-S3-BOX-3 上完成以下闭环：

```text
LCD/触摸或 NSH 输入
  -> smart_home
  -> cAGENT
  -> HTTPS 调用云端 LLM
  -> tool call
  -> 虚拟设备或真实 GPIO/外设响应
  -> LVGL/串口显示结果
```

实施分为三个阶段：

1. **Console 最小闭环**：先验证构建、Wi-Fi、DNS、TLS、LLM 和工具调用。
2. **配置持久化**：增加 `/data` 文件系统、Wi-Fi 自动重连和 LLM 配置保存。
3. **LVGL 产品界面**：最后接入 LCD、触摸、PNG 图标和异步 Agent worker。

不要在第一阶段同时启用持久化、外部字体和 LVGL。分阶段可以把网络、Agent、文件系统和 UI 问题隔离开。

## 2. 当前基础与缺口

ESP32-S3-BOX-3 当前从本仓库自定义板级目录接入：

```text
openvela_smarthome/board/esp32s3-box-3
  -> vendor/espressif/boards/esp32s3/esp32s3-box-3
```

现有配置已经提供：

- ESP32-S3 启动、串口和 NSH。
- Wi-Fi 与 `wlan0`。
- LCD、触摸和 LVGL。
- PSRAM。
- WAPI 命令和 Wi-Fi 配置保存接口。

smart_home 当前已经提供：

- cAGENT Agent 装配。
- OpenAI-compatible LLM provider。
- 8 个智能家居工具。
- 虚拟设备和环境状态。
- Console UI 与 LVGL UI。

仍需补齐或持续验证：

- CMake 板级接入；当前 BOX-3 自定义板缺少板级 `CMakeLists.txt`，
  第一阶段先使用 Make 构建路径。
- LLM 配置读取与安全保存。
- BOX-3 专用 `smart_home` defconfig 已派生，后续需用 `savedefconfig`
  固化 menuconfig 修改。
- `/data` LittleFS 分区已配置（见第 10 节），资源镜像生成和烧录流程已就绪。
- Wi-Fi 联通性和 DNS 需在板端实际验证。
- TLS CA 校验与设备时间同步。
- `esp-hal-3rdparty` 兼容补丁的持久化（见第 6 节）。

ESP32-S3-BOX-3 上启用 Wi-Fi 与 cAGENT openvela TLS 时会同时牵出 ESP HAL
mbedTLS 和 apps `CRYPTO_MBEDTLS`。在线 LLM 的第一阶段接入优先按
`docs/mbedtls-transport-route-b.md` 的路线 B 执行：通过 smart_home 注入
`agent_runtime_t.http_post` transport，先用 HTTP 网关跑通云模型闭环，再评估
BOX-3 直连 HTTPS。

## 3. 仓库与 manifest 接入

本仓库 manifest 应包含：

```xml
<linkfile src="packages/cAGENT" dest="packages/cAGENT"/>
<linkfile src="demos/smart_home" dest="packages/demos/smart_home"/>
```

同步后检查：

```bash
ls -ld packages/cAGENT
ls -ld packages/demos/smart_home
```

两个路径应指向本仓库中的源码。

提交前还必须确认 `packages/cAGENT` 和 `demos/smart_home` 不会以意外的嵌套 Git 仓库形式提交。本仓库应能在一次 clone/repo sync 后得到完整源码，不能只留下不可解析的 gitlink。

## 4. 构建路径选择

ESP32-S3-BOX-3 当前优先使用 openvela Makefile 构建路径。原因是
`--cmake` 配置阶段会检查自定义板目录下是否存在板级 `CMakeLists.txt`，
而当前目录：

```text
vendor/espressif/boards/esp32s3/esp32s3-box-3
```

只有 Make 风格的 `Kconfig`、`scripts/Make.defs` 和 `src/Make.defs`，
没有板级 `CMakeLists.txt`。因此执行：

```bash
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  --cmake menuconfig
```

会在 CMake 配置阶段报错：

```text
No CMakeLists.txt found at .../esp32s3-box-3
```

第一阶段不要为此阻塞 smart_home 验证，直接走 Make 路径。

### 4.1 Makefile 接入状态

当前本仓库已包含：

```text
packages/cAGENT/Make.defs
demos/smart_home/Make.defs
```

`packages/Make.defs` 会 include `packages/*/Make.defs`，
`packages/demos/Make.defs` 会 include `packages/demos/*/Make.defs`，
因此启用 `CONFIG_SMART_HOME_DEMO=y` 后，Make 构建应能收集
`cAGENT` 和 `smart_home`。

需要重点确认：

```text
CONFIG_CAGENT=y
CONFIG_SMART_HOME_DEMO=y
```

### 4.2 后续 CMake 接入

如果后续需要统一走 `--cmake`，需要给 BOX-3 自定义板补板级
`CMakeLists.txt`，并确认 ESP32-S3 board `src/`、启动文件、链接脚本和
board common 逻辑都能被 CMake 正确纳入。这个改动面大于 Console 首测，
不作为第一阶段前置条件。

增加持久化模块后，Makefile 和 CMakeLists 都要登记新源文件：

```text
Makefile:      CSRCS += src/config/smart_home_persist.c
CMakeLists.txt list(APPEND SMART_HOME_SRCS src/config/smart_home_persist.c)
```

完成链接后运行 `menuconfig`，应能同时看到：

```text
CONFIG_CAGENT
CONFIG_SMART_HOME_DEMO
```

## 5. BOX-3 专用 defconfig

不要直接修改通用 `esp32s3-box/configs/openvela/defconfig`。当前本仓库已保存
BOX-3 专用配置：

```text
openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

并通过 manifest 映射到：

```text
vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/defconfig
```

第一阶段从 BOX-3 的 `audio_event/defconfig` 派生并收敛，重点配置为：

```text
CONFIG_SMART_HOME_DEMO=y
CONFIG_SMART_HOME_DEMO_UI_CONSOLE=y

CONFIG_CAGENT=y
CONFIG_CAGENT_MODEL_OPENAI=y
CONFIG_CAGENT_RUNTIME_OPENVELA=y
CONFIG_CAGENT_RUNTIME_OPENVELA_TLS=y

CONFIG_ESP32S3_WIFI=y
CONFIG_WIRELESS_WAPI=y
CONFIG_WIRELESS_WAPI_CMDTOOL=y
CONFIG_WIRELESS_WAPI_INITCONF=y
CONFIG_NETDB_DNSCLIENT=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y

# /data LittleFS 资源分区
CONFIG_ESP32S3_SPIFLASH=y
CONFIG_ESP32S3_MTD=y
CONFIG_ESP32S3_STORAGE_MTD_OFFSET=0xe00000
CONFIG_ESP32S3_STORAGE_MTD_SIZE=0x100000
CONFIG_ESP32S3_SPIFLASH_LITTLEFS=y
CONFIG_ESP32S3_SPIFLASH_LITTLEFS_MOUNTPT="/data"
CONFIG_FS_LITTLEFS=y
```

第一阶段建议关闭不需要的 demo，减少镜像与 RAM 占用：

```text
# CONFIG_EXAMPLES_LVGLDEMO is not set
# CONFIG_SMART_HOME_DEMO_UI_LVGL is not set
```

`CONFIG_SMART_HOME_DEMO` 已通过 Kconfig 选择 cAGENT、OpenAI provider 和
openvela TLS runtime；上面的显式列表用于检查最终 `.config`，避免依赖关系
没有生效。

打开自定义内核配置时使用 Make 路径，不加 `--cmake`：

```bash
cd openvela
source build/envsetup.sh

./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  menuconfig
```

保存并退出 `menuconfig` 后，固化回专用 `defconfig`：

```bash
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  savedefconfig
```

检查关键项：

```bash
rg -n "SMART_HOME|CAGENT|WAPI|ESP32S3_WIFI|LVGL|AUDIO_EVENT" \
  openvela_smarthome/board/esp32s3-box-3/configs/smart_home/defconfig
```

## 6. HAL 兼容补丁

ESP32-S3 同时启用 Wi-Fi 和 cAGENT TLS 时，`esp-hal-3rdparty` 需要三处临时
补丁。脚本位于 `openvela_smarthome/scripts/`，详见
`scripts/readme.md`。

| 脚本 | 问题 | 修复 |
|------|------|------|
| `fix_box3_mbedtls_header_priority` | apps mbedTLS 头文件优先级过高，ESP HAL 用了错误的 `cipher_info_t` | `-I` 改 `-isystem` |
| `fix_box3_mbedtls_disable_ccm` | ESP-IDF 和 NuttX 的 `mbedtls_ccm_context` 结构体不同 | 注释 `MBEDTLS_CCM_C` |
| `fix_box3_spinlock_initializer` | ESP HAL 用 `0` 初始化 NuttX `spinlock_t`（struct 类型） | 改为 `SP_UNLOCKED` 宏 |

这些补丁参考了 `packages/ai_agent/fix_esp32s3.sh`（Fix 1-3）的方案。

HAL 目录由构建系统自动下载，`distclean` 会删除它。补丁必须在构建**并行运行**
（见 7.2 节）。在补丁持久化前切换 EYE/BOX defconfig，可能再次遇到
`invalid initializer` 或 `esp_mbedtls_*` undefined reference。

## 7. 构建、烧录与资源部署

### 7.1 完整构建流程

在 openvela 工作区根目录执行。当前 BOX-3 首测走 Make 路径，不加
`--cmake`：

```bash
cd openvela
source build/envsetup.sh

cd nuttx && make distclean && cd openvela

./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)
```

`distclean` 会删除 `esp-hal-3rdparty` 目录，下次构建时由构建系统自动重新
clone 和 patch。

### 7.2 ESP HAL 兼容补丁（构建时并行运行）

ESP32-S3 同时启用 Wi-Fi 和 cAGENT TLS 时，需要对 ESP HAL 的 mbedTLS 做三处
临时补丁。脚本位于 `openvela_smarthome/scripts/`，必须在构建
**并行运行**（因为 `esp-hal-3rdparty` 由构建系统异步下载）：

```bash
# 终端 1：启动构建
cd openvela
./build.sh \
  vendor/espressif/boards/esp32s3/esp32s3-box-3/configs/smart_home/ \
  -j$(nproc)

# 终端 2：并行运行补丁脚本
cd openvela
bash openvela_smarthome/scripts/fix_box3_mbedtls_header_priority.sh
bash openvela_smarthome/scripts/fix_box3_mbedtls_disable_ccm.sh &
bash openvela_smarthome/scripts/fix_box3_spinlock_initializer.sh &
```

三个补丁分别解决：

| 脚本 | 问题 | 修复 |
|------|------|------|
| `fix_box3_mbedtls_header_priority` | apps mbedTLS 头文件优先级过高 | `-I` 改 `-isystem` |
| `fix_box3_mbedtls_disable_ccm` | CCM 结构体定义冲突 | 注释 `MBEDTLS_CCM_C` |
| `fix_box3_spinlock_initializer` | ESP HAL 用 `0` 初始化 NuttX spinlock | 改为 `SP_UNLOCKED` |

脚本包含最多 180 秒的等待机制，会在目标文件出现后自动应用补丁。
详见 `scripts/readme.md`。

### 7.3 烧录固件

确认构建产物存在：

```text
nuttx/nuttx
nuttx/nuttx.bin
```

烧录前确认串口和权限：

```bash
id                              # 确认属于 dialout 组
ls -l /dev/ttyACM*              # 确认串口存在
```

烧录固件到 `0x0`：

```bash
cd openvela

# 方式 1：通过 NuttX Make
make -C nuttx flash \
  ESPTOOL_PORT=/dev/ttyACM0 \
  ESPTOOL_BAUD=921600

# 方式 2：直接调用 esptool
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write-flash 0x0 nuttx/nuttx.bin
```

设备重枚举后串口号可能变化，应以实际 `/dev/ttyACM*` 为准。

### 7.4 烧录资源分区

固件烧录完成后，生成并烧录 LittleFS 资源镜像到 `0xe00000`：

```bash
cd openvela

# 生成 LittleFS 镜像（默认含字体、图标、技能和 MCP endpoint 配置）
bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh

# 烧录资源分区
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write-flash 0xe00000 out/box3_littlefs_data/data_lfs.bin
```

真机需要 MCP/Node 凭据时显式加入 `secrets.json`：

```bash
WITH_SECRETS=1 bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
```

**烧录顺序**：固件 `0x0` -> 资源 `0xe00000`。
如果执行过 `erase-flash`，固件和资源都会被擦掉，需要按此顺序重新烧录。

### 7.5 设备验证

打开串口终端：

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

如果编译遇到 mbedTLS 链接错误（如 `esp_mbedtls_*` undefined reference），
参见 `docs/mbedtls-transport-route-b.md` 了解双 mbedTLS 冲突的完整分析和
长期解耦方案。

## 8. Wi-Fi 连接与持久化

### 8.1 自动联网（推荐）

`smart_home` 启动时会自动连接 Wi-Fi，无需手动输入 `ifup`/`wapi`/`renew` 命令。

**首次使用：** 先在 NSH 中手动连接 Wi-Fi 并保存配置：

```text
ifup wlan0
wapi mode wlan0 2
wapi psk wlan0 88888888 3
wapi essid wlan0 123 1
renew wlan0
wapi save_config wlan0
```

保存后 `/data/wapi.conf` 会生成如下 JSON：

```json
{"wlan0":{"mode":2,"auth":4,"cmode":8,"alg":3,"ssid":"123","bssid":"00:00:00:00:00:00","psk":"88888888"}}
```

此后每次启动 `smart_home`，程序会自动读取 `/data/wapi.conf` 并完成关联 + DHCP + DNS 验证。

**修改 Wi-Fi 热点：**

```text
wapi save_config wlan0   # 用新的 ssid/psk 之后
reboot
```

或直接编辑配置文件（NSH 不支持重定向，需要用 `sh` 或写文件工具）。

**Kconfig 默认值：** 如果 `/data/wapi.conf` 不存在，程序回退到编译时默认值（SSID=`123`，密码=`88888888`）。可通过 `menuconfig` 修改：

```text
CONFIG_SMART_HOME_WIFI_SSID="YourSSID"
CONFIG_SMART_HOME_WIFI_PASSWORD="YourPassword"
CONFIG_SMART_HOME_WIFI_RETRY_COUNT=3
```

### 8.2 手动联网（调试用）

如果需要手动操作 Wi-Fi（调试、排查问题），仍可使用以下命令：

```text
ifup wlan0
wapi scan wlan0
wapi mode wlan0 2
wapi psk wlan0 88888888 3
wapi essid wlan0 123 1
renew wlan0
```

`renew` 可能报 `netlib_obtain_ipv4addr() failed`，但 Wi-Fi 实际已关联。
用 `ifconfig wlan0` 确认是否有 IP：

```text
ifconfig wlan0
```

如已有 IP（`inet addr:10.0.0.x`），继续验证连通性：

```text
ping 8.8.8.8
ping api.deepseek.com
```

DNS 设置（NSH 不支持 `>` 重定向，需用 `nslookup` 指定服务器）：

```text
nslookup api.deepseek.com 8.8.8.8
```

判断顺序：

- `wlan0` 没有 IP：检查关联和 DHCP。
- IP 可用但 ping 失败（`sendto failed: 101` 即 `ENETUNREACH`）：检查路由和
  ARP，确认 IP 网段与路由器匹配（常见为 `192.168.x.x`，非 `10.0.0.x`）。
- IP 可用但域名失败：检查 DNS，用 `nslookup host 8.8.8.8` 手动指定。
- 域名可用但 HTTPS 失败：检查系统时间、CA 和 mbedTLS 配置。

常见 NSH 与 bash 差异：

| bash | NSH | 说明 |
|------|-----|------|
| `echo x > file` | 不支持 | NSH 不支持 shell 重定向 |
| `route` | 不可用 | NuttX 无独立 route 命令 |
| `arp -a` | 语法不同 | NuttX arp 需要接口名参数 |

### 8.3 WAPI 配置文件说明

Wi-Fi 配置由 WAPI 管理，存储在 `/data/wapi.conf`（JSON 格式）。
`smart_home` 启动时自动加载该文件并完成连接，无需手动执行 `wapi reconnect`。

配置文件格式：

```json
{
  "wlan0": {
    "mode": 2,
    "auth": 4,
    "cmode": 8,
    "alg": 3,
    "ssid": "YourSSID",
    "bssid": "00:00:00:00:00:00",
    "psk": "YourPassword"
  }
}
```

字段说明：

| 字段 | 含义 | 常用值 |
|------|------|--------|
| mode | 工作模式 | 2 = Managed (STA) |
| auth | WPA 版本 | 4 = WPA2 |
| cmode | 加密方式 | 8 = CCMP |
| alg | WPA 算法 | 3 = CCMP |

## 9. 设备配置文件

### 9.1 配置分层

建议分成三个文件：

```text
/data/wapi.conf             Wi-Fi，由 WAPI 管理
/data/smart_home.conf       LLM 和应用普通配置
/data/smart_home.secret     API Key，后续替换为安全存储
```

`smart_home.conf` 示例：

```ini
version=1
backend_id=deepseek
host=api.deepseek.com
path=/v1/chat/completions
port=443
model=<实际可用模型名>
timeout_ms=30000
max_output_tokens=512
```

配置优先级：

```text
编译期非敏感默认值
  < /data/smart_home.conf
  < 运行时 UI/命令行修改
```

API Key 不得继续放在源码、defconfig、README 或日志中。已经暴露的 Key 必须在服务商后台吊销并重新生成。

### 9.2 应用层实现

cAGENT 已提供 `agent_config_load()`，支持 `key=value` 文件和环境变量覆盖，但不负责保存。建议新增：

```c
int smart_home_config_load(smart_home_model_config_t *config);
int smart_home_config_save(const smart_home_model_config_t *config);
```

启动顺序调整为：

```text
board bring-up
  -> 挂载 /data
  -> Wi-Fi 连接并获得 IP
  -> 加载 smart_home.conf/secret
  -> 校验 host/path/port/model/timeout
  -> 创建 cAGENT 和 model
  -> 启动 console 或 LVGL
```

保存时使用原子替换：

```text
写 smart_home.conf.tmp
  -> fflush
  -> fsync
  -> rename 为 smart_home.conf
```

禁止在日志中打印 Wi-Fi 密码和 API Key。

## 10. `/data` 文件系统

ESP32-S3-BOX bring-up 已支持调用 SPI Flash 初始化。`smart_home`
配置采用最小 LittleFS 资源分区闭环：

```text
CONFIG_ESP32S3_SPIFLASH=y
CONFIG_ESP32S3_MTD=y
CONFIG_ESP32S3_STORAGE_MTD_OFFSET=0xe00000
CONFIG_ESP32S3_STORAGE_MTD_SIZE=0x100000
CONFIG_ESP32S3_SPIFLASH_LITTLEFS=y
CONFIG_ESP32S3_SPIFLASH_LITTLEFS_MOUNTPT="/data"
CONFIG_FS_LITTLEFS=y
```

当前约定：

```text
DATA_OFFSET = 0xe00000
DATA_SIZE   = 0x100000
mount point = /data
```

资源镜像根目录就是 `/data` 的根，因此镜像内路径为：

```text
/res/skills/*.md
/res/icons/*.png
/res/fonts/MiSans-Normal.ttf
/smart_home/mcp_bridge.json
```

生成包含资源和 MCP endpoint 配置的 1MB LittleFS 镜像：

```bash
cd openvela
bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
```

如需加入 `secrets.json`（仅真机联调时使用）：

```bash
WITH_SECRETS=1 bash openvela_smarthome/scripts/make_box3_littlefs_data_image.sh
```

`scripts/subset_font.sh` 使用 `pyftsubset` 从 MiSans 中提取 UI 所需的约 400 个字符
（ASCII + 智能家居常用中文），将 15MB 字体裁剪到 ~144KB。

烧录资源分区（详见 7.4 节）：

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash \
  0xe00000 out/box3_littlefs_data/data_lfs.bin
```

板端验证：

```text
nsh> ls /data
nsh> ls /data/res/skills
nsh> cat /data/res/skills/smart_home_weather.md
nsh> smart_home "打开客厅灯，亮度35%"
```

注意：

1. 不要执行整片 `erase-flash`，否则固件和资源都会被擦掉。
2. `DATA_OFFSET/DATA_SIZE` 必须与最终固件和 OTA 规划不重叠。
3. 脚本优先打包 `scripts/subset_font.sh` 生成的子集字体（MiSans 15MB → ~144KB），并默认加入镜像；若子集字体缺失会回退到完整字体并发出警告。
4. 如果 LittleFS 镜像参数与 NuttX 配置不匹配，挂载失败时通用 bring-up 可能触发 `forceformat`，把预置镜像格式化为空文件系统。

在该闭环验证通过前，不扩大资源分区。

## 11. Console 最小闭环

联网并确认 DNS 后执行：

```text
smart_home "打开客厅灯，亮度35%"
smart_home "我要睡觉了"
```

预期日志链：

```text
MODEL_REQ
MODEL_RESP
TOOL_CALL set_light/run_scene
TOOL_RES
MODEL_REQ
MODEL_RESP
assistant: ...
```

第一阶段验收：

- `smart_home` 可从 NSH 启动。
- `ls /data` 不报错，资源文件可访问。
- Wi-Fi 断开或 DNS 失败时返回明确错误，不崩溃。
- API Key 不出现在串口日志。
- 至少一个工具调用修改虚拟设备状态。
- 连续执行 20 次无明显内存泄漏或死锁。

常见启动错误：

| 错误 | 原因 | 处理 |
|------|------|------|
| `smart_home_agent_app_init failed: -13` | `/data/res/` 不存在或 cAGENT 创建失败 | 检查 LittleFS 是否挂载、资源分区是否烧录 |
| `smart_home_agent_app_init failed: -1` | 网络未就绪或 model 创建失败 | 先确认 Wi-Fi 和 DNS 通 |
| `ls /data` -> `stat failed: 2` | LittleFS 分区未烧录或配置不匹配 | 重新烧录资源分区（见 7.4） |

## 12. LVGL 阶段

Console 稳定后再启用：

```text
CONFIG_SMART_HOME_DEMO_UI_LVGL=y
CONFIG_GRAPHICS_LVGL=y
CONFIG_LV_USE_NUTTX=y
CONFIG_LV_USE_NUTTX_LCD=y
CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y
CONFIG_LV_USE_LODEPNG=y
```

当前 UI 回退路径直接引用 Montserrat 12/14。应改为 `LV_FONT_DEFAULT`，或者确保对应字体配置已启用。

资源策略：

- 两份完整 MiSans 字体约 15 MB，不适合当前 4 MB 镜像方案。
- 子集化后 MiSans 约 144KB（~400 字符），可放入 1MB 资源分区。
- PNG 图标约 204 KB，可放入经过验证的数据分区或转成 C 数组。
- 如需扩展字符集，编辑 `scripts/subset_font.sh` 中的 `chars.txt` 后重新生成。

LVGL 线程只处理界面；LLM 请求、Wi-Fi 重连和工具执行通过 worker/消息队列完成。

## 13. TLS 安全要求

当前 openvela runtime 使用 `MBEDTLS_SSL_VERIFY_OPTIONAL`，且没有装载可信 CA。正式版本必须完成：

1. 配置可信根证书或最小 CA bundle。
2. 使用 `MBEDTLS_SSL_VERIFY_REQUIRED`。
3. 联网后同步系统时间，再执行证书有效期校验。
4. 校验 hostname。
5. API Key 不写入源码和日志。

在这些条件完成前，只能作为受控网络中的开发验证，不应作为安全完成状态。

## 14. 最终验收清单

### 仓库与构建

- [ ] manifest 映射正确，干净 repo sync 后源码完整。
- [ ] cAGENT 与 smart_home 的 Makefile/CMake 构建均登记完整。
- [ ] HAL patch 可自动重放，不依赖手工修改下载目录。
- [ ] BOX 专用 defconfig 位于本仓库并可一键复现。

### 网络与配置

- [ ] `wlan0` 可以连接 2.4 GHz WPA2 网络。
- [ ] DHCP、DNS和 HTTPS 分别验证。
- [ ] `/data` 分区与固件无重叠。
- [ ] Wi-Fi 和 LLM 配置可重启恢复。
- [ ] API Key 已从源码和 Git 历史移除。

### 应用与 UI

- [ ] Console 模式完成一次完整 tool call。
- [ ] LVGL 面板、触摸和 Agent worker 连续运行稳定。
- [ ] 图标或字体缺失时能够回退，不导致启动失败。
- [ ] 断网、超时、无效配置均有明确用户反馈。
- [ ] 至少一个 tool 最终驱动真实 LED、GPIO、继电器或其他外设。
