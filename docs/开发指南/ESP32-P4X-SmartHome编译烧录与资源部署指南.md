# ESP32-P4X SmartHome 编译、烧录与资源部署指南

> 适用对象：OpenVela、ESP32-P4 Function EV Board、`smart_home` 配置。
> 本文以当前已验证的 **16 MiB Flash：6 MiB 固件区 + 10 MiB LittleFS 数据区** 为准。

## 1. 目标与边界

本文覆盖以下闭环：

```text
源码修改
  -> 构建 smart_home 固件
  -> 烧录 nuttx.bin
  -> 打包 /data LittleFS 资源（可选 secrets.json）
  -> 烧录资源镜像
  -> 串口进入 NSH 验证显示、触摸、资源和网络
```

固件与资源镜像是两个独立的写入目标：修改 C/LVGL/Kconfig 后需要构建并烧录固件；修改字体、技能、普通 JSON 配置或 `secrets.json` 时，通常只需重新打包、烧录资源镜像。

## 2. 当前镜像布局

每次烧录前应以最终 `nuttx/.config` 为准；当前 `smart_home` defconfig 和板端启动日志确认的布局如下。

| 区域 | Flash 范围 | 大小 | 内容 |
| --- | --- | ---: | --- |
| 固件 | `0x002000` 起 | 最大 6 MiB | `nuttx/nuttx.bin`，ESP32-P4 Simple Boot |
| LittleFS | `0x600000`–`0xFFFFFF` | 10 MiB | `/data`：字体、技能、图标、配置、可选密钥 |

固件和资源分区不重叠。不要把旧文档中的 `0x800000`、`0xE00000` 等地址用于此配置。

## 3. 前置条件

- 仓库根目录为 `~/openvela`，且存在 `build.sh` 与 `myenv/`。
- 开发板以下载模式连接，使用的端口以实际枚举结果为准。本文用 `/dev/ttyACM0` 举例。
- 使用同一份 `smart_home` defconfig 构建和烧录，不能将其他配置生成的 `nuttx.bin` 混用。
- 烧录前退出 `picocom`、screen 或 tmux 串口窗口，避免下载工具占用端口。

查看当前串口设备：

```bash
ls -l /dev/serial/by-id/
ls -l /dev/ttyACM*
```

## 4. 构建固件

在仓库根目录执行：

```bash
cd ~/openvela
source myenv/bin/activate

./build.sh \
  contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/smart_home \
  -j8
```

构建成功后核对产物和关键分区配置：

```bash
test -f nuttx/nuttx.bin && ls -lh nuttx/nuttx.bin

rg '^(CONFIG_ARCH_CHIP_ESP32P4|CONFIG_ESPRESSIF_SIMPLE_BOOT|CONFIG_ESPRESSIF_STORAGE_MTD_(OFFSET|SIZE)|CONFIG_ESPRESSIF_FLASH_MODE_DIO)' \
  nuttx/.config
```

期望至少看到：

```text
CONFIG_ESPRESSIF_SIMPLE_BOOT=y
CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET=0x600000
CONFIG_ESPRESSIF_STORAGE_MTD_SIZE=0xa00000
CONFIG_ESPRESSIF_FLASH_MODE_DIO=y
```

### 4.1 应用静态库残留的处理

若源码已经更新，但错误日志仍引用已删除的旧符号，或链接到旧的 `libapps.a` 对象，可只清理应用归档后再构建：

```bash
cd ~/openvela
make -C nuttx apps_clean
rm -f nuttx/staging/libapps.a

./build.sh \
  contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/smart_home \
  -j8
```

这不是常规构建步骤；不要因为一次编译失败就执行 `distclean` 或全量擦除 Flash。

## 5. 烧录固件

先确认端口上的芯片确为 ESP32-P4：

```bash
cd ~/openvela
source myenv/bin/activate
esptool --chip esp32p4 --port /dev/ttyACM0 chip_id
```

随后烧录 Simple Boot 固件。ESP32-P4 ROM 从 `0x2000` 装载此镜像：

```bash
cd ~/openvela
source myenv/bin/activate

esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write-flash -fs 16MB -fm dio -ff 80m \
  0x2000 nuttx/nuttx.bin
```

烧录成功只说明字节已写入；复位后仍须通过串口确认 NuttX 启动。

## 6. 打包 LittleFS 资源

资源打包脚本会生成完整的 10 MiB LittleFS 镜像，默认包含：

- `/data/res/fonts/MiSans-Normal.ttf`；
- `/data/res/icons/` 中 PNG 资源；
- `/data/res/skills/` 中技能 Markdown；
- `/data/smart_home/backends.json`、`settings.json`、`mcp_bridge.json` 等非敏感配置。

内嵌 LVGL 图标字体和转换后的 C 图像编入固件，不在 LittleFS 内重复打包。

生成默认资源镜像：

```bash
cd ~/openvela
source myenv/bin/activate

bash contest2026_031_niudanxianqianchong/scripts/make_p4x_littlefs_data_image.sh

ls -lh out/p4x_littlefs_data/data_lfs.bin
```

脚本会检查资源分区范围，输出镜像位于：

```text
out/p4x_littlefs_data/data_lfs.bin
```

## 7. 部署 secrets.json

`secrets.json` 用于模型 API Key、App Bridge token、MCP 敏感 header 和运行时
Wi-Fi 凭据。它被 `.gitignore` 排除，不能提交到 Git、日志或截图中。

真实文件路径：

```text
contest2026_031_niudanxianqianchong/demos/smart_home/res/config/secrets.json
```

可参考 `demos/smart_home/config/secrets.example.json` 创建或检查结构。最小结构示例中的值必须自行填写，不能把真实密钥写入文档：

```json
{
  "version": 1,
  "model_api_keys": {
    "deepseek": "<YOUR_KEY>"
  },
  "wifi": {
    "ssid": "<YOUR_WIFI_SSID>",
    "password": "<YOUR_WIFI_PASSWORD>"
  },
  "node_gateway": { "shared_token": "<RANDOM_TOKEN>" },
  "mcp_bridge": { "header_values": {} }
}
```

检查文件存在并限制宿主机文件权限：

```bash
cd ~/openvela
test -s contest2026_031_niudanxianqianchong/demos/smart_home/res/config/secrets.json
chmod 600 contest2026_031_niudanxianqianchong/demos/smart_home/res/config/secrets.json
```

显式加入敏感文件重新打包：

```bash
cd ~/openvela
source myenv/bin/activate

WITH_SECRETS=1 \
bash contest2026_031_niudanxianqianchong/scripts/make_p4x_littlefs_data_image.sh
```

`WITH_SECRETS=1` 仅影响资源镜像，不重新构建固件。该资源镜像是整个 `/data` 分区的快照，烧录后会覆盖当前 `/data` 中运行时写入的文件；重要状态应先导出或备份。特别是，若镜像中的
`secrets.json` 不含当前 Wi-Fi，烧录后需要重新在 UI 中配网。

## 8. 烧录 LittleFS 资源镜像

确认生成的镜像存在后，使用当前资源分区的起始地址 `0x600000`：

```bash
cd ~/openvela
source myenv/bin/activate

esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write-flash -fs 16MB -fm dio -ff 80m \
  0x600000 out/p4x_littlefs_data/data_lfs.bin
```

该操作只覆盖 `/data` LittleFS；不会覆盖 `0x2000` 的 `nuttx.bin`。不要在日常资源更新中执行 `erase_flash`。

## 9. 串口与启动验证

复位后，串口通常为 USB Serial/JTAG：

```bash
picocom -b 115200 /dev/ttyACM0
```

在 NSH 中验证挂载、资源和应用：

```text
nsh> ls /data
nsh> ls /data/res
nsh> ls /data/res/fonts
nsh> ls /data/smart_home
nsh> smart_home
```

预期关键现象：

- LittleFS 日志包含 `offset=0x00600000 size=0x00a00000` 和 `mounted`；
- `/data/res/fonts/MiSans-Normal.ttf` 存在；
- 若使用 `WITH_SECRETS=1`，`/data/smart_home/secrets.json` 存在；
- `smart_home` 输出 LVGL 页面构建完成日志，并显示首页。

不要通过 `cat /data/smart_home/secrets.json` 验证密钥，文件名存在即可。

## 10. 网络与模型连通性验证

资源镜像中的 API Key 不会替代 Wi-Fi 凭据。默认 `smart_home` 配置已关闭
`CONFIG_SMART_HOME_DEMO_OFFLINE_UI`：首次启动后，从“更多 → 网络设置”输入
SSID 和密码，点击“连接”。应用通过 P4 的 ESP-Hosted RPC 配置板载 C6、等待
DHCP/DNS，并在获得 IP 后原子写入 `/data/smart_home/secrets.json`。下次启动会
优先读取该运行时凭据自动连接；凭据不会写入 `settings.json`、Kconfig 或日志。
尚未保存凭据的首次启动只准备 `wlan0` 并立即显示 UI，不会先等待一次必然失败的
DHCP 超时。

顶栏 Wi-Fi 图标反映真实链路状态：绿色为互联网/DNS 可用，琥珀色为已获得局域网
IP 但互联网或 DNS 不可用，灰色为未连接或未获得 IP。当前版本仅支持手工输入；附近
网络扫描依赖 C6 scan RPC，作为后续功能接入。

配网完成后再进行以下只读检查；不必把 `ifup`/`renew` 当成正常配网步骤：

```text
nsh> ifup wlan0
nsh> renew wlan0
nsh> ifconfig
nsh> ping api.deepseek.com
```

若启动日志出现：

```text
ESP-Hosted C6: STA credentials unset; connect skipped
```

说明 C6 已成功完成 SDIO 初始化并注册了 `wlan0`，但尚未配置 AP 凭据。请进入
“更多 → 网络设置”输入实际 SSID 与密码；除非要预置敏感文件，否则不需要重刷
`secrets.json`。

## 11. 常见问题速查

| 现象 | 首先检查 | 处理 |
| --- | --- | --- |
| `invalid header` 或无法启动 | 固件偏移与 Simple Boot | 确认 `CONFIG_ESPRESSIF_SIMPLE_BOOT=y`，用 `0x2000 nuttx/nuttx.bin` 烧录 |
| `/data` 为空或无法挂载 | LittleFS 偏移、镜像是否烧录 | 重新生成 `data_lfs.bin`，写入 `0x600000` |
| 没有中文或 LVGL 启动卡在字体初始化 | 字体资源与 PSRAM | 确认 `MiSans-Normal.ttf` 已在 `/data/res/fonts/`，并保留当前完整字体 PSRAM 预加载方案 |
| `renew wlan0` 失败 | C6 是否已关联 | 查看是否有 `STA credentials unset`；先配置 WLAN 凭据 |
| 模型显示未配置或请求失败 | secrets、网络、DNS | 确认 `secrets.json` 已随资源镜像写入、后端 ID 匹配、`wlan0` 有 IP 与 DNS |
| 编译仍引用已删除图标符号 | 旧应用静态库 | 按第 4.1 节清理 `libapps.a` 后重新构建 |
| `esptool` 无法打开端口 | 串口程序占用 | 退出 `picocom`/tmux 串口，重新检查 `/dev/ttyACM*` |

## 12. 日常迭代决策

| 修改内容 | 需要执行 |
| --- | --- |
| C 代码、LVGL 布局、内嵌 C 图标、Kconfig | 第 4、5 节：构建并烧录固件 |
| 字体、技能、LittleFS PNG、普通 JSON 配置 | 第 6、8 节：生成并烧录资源镜像 |
| 仅更新 API Key、App token、MCP 敏感 header | 第 7、8 节：`WITH_SECRETS=1` 生成并烧录资源镜像 |
| Wi-Fi 账号或密码 | 从“更多 → 网络设置”在板端配置；不把凭据写入 Git 或本文档 |
