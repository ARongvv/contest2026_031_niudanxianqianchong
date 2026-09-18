# SmartHome LVGL 网络配网与图标 UI 交接

交接日期：2026-09-18。

本文记录 ESP32-P4X `smart_home` 的横屏 LVGL UI、内嵌图标库和手工 Wi-Fi
配网功能的当前代码边界。用户负责后续固件构建、烧录与真机验收；接手时先检查
工作树，不能用本文覆盖本地修改。

## 1. 当前基线与工作树

仓库与分支：

```text
/home/arongw/openvela/contest2026_031_niudanxianqianchong
dev-ai-contest-2026
```

已提交的相关基线：

| Commit | 内容 |
| --- | --- |
| `4270aa4` | 横屏产品 UI 的 PNG→LVGL A8 图标集成与主页、导航、设备页替换。 |
| `9c5e1f1` | 设备二级页、房间调整/新增与 P4X 部署指南。 |
| `b222304` | 关闭默认离线 UI，P4+C6 运行时手工 Wi-Fi 配网、`secrets.json` 凭据持久化与网络状态。 |
| `3ec190f` | 新增 10 个产品图标并生成 32 px LVGL A8 C 资源。 |

本记录形成时另有**尚未提交**的低风险 UI 修改：

- `src/ui/lvgl/smart_home_lvgl_pages.c`：网络页键盘避让底部导航栏；
- `src/ui/lvgl/smart_home_lvgl_home.c`、`images/smart_home_icons.h`：无网络时使用 `wifi-off`；
- `src/ui/lvgl/smart_home_lvgl_home.c`、`smart_home_lvgl_chat.c`：将“小乔”统一改为 OpenVela；
- `board/.../configs/smart_home/defconfig` 也有本地未提交修改，来源与本次 UI 修复无关，**不得顺带提交、丢弃或覆盖**。

接手前执行：

```bash
cd ~/openvela/contest2026_031_niudanxianqianchong
git status --short
git log --oneline -8
git diff
```

## 2. UI 架构和资源规则

显示目标为 1024×600 横屏；产品页面由原生 LVGL 创建，包含首页、设备、场景、
安防、更多、网络设置、聊天和系统设置。完整字体 `MiSans-Normal.ttf` 在启动时
预加载到 PSRAM，再以 `lv_tiny_ttf_create_data()` 创建字号实例，避免全字库按需
文件读取导致启动卡住。

图标唯一视觉源为：

```text
quickapp/smart_home_ui/prototype-web/assets/icons/*.png
```

生成脚本为：

```bash
cd ~/openvela
python3 contest2026_031_niudanxianqianchong/scripts/generate_smart_home_lvgl_icons.py
```

生成结果位于：

```text
demos/smart_home/src/ui/lvgl/icons/generated/*.c
```

当前有 71 个 PNG 源图标、95 个 LVGL v9 `LV_COLOR_FORMAT_A8` C 图片。A8 只保存
透明度，由 LVGL `image_recolor` 统一着色；不要把同一页面改回 Font Awesome 或
LittleFS PNG 运行时加载，以免视觉和渲染路径混用。

调用应使用稳定名称，例如：

```c
smart_home_lvgl_icon_create(parent, "asset:wifi-off", 20, 20);
```

新增的 `MIJIA`、`battery-vertical-charging`、`message`、`plug`、`settings`、
`stopwatch`、`tool`、`user` 等目前均有标准 32 px 版本。仅在图标真实进入顶栏、
导航或首页重点卡片后，再在生成器的尺寸名单中增加 20 px 或 48 px，控制固件体积。

## 3. Wi-Fi 配网闭环

默认 `smart_home` 配置不使用离线 UI。首次启动若没有已保存的 Wi-Fi 凭据，只会
准备 `wlan0` 并显示 UI，不会先等待一次无热点关联的 DHCP 超时。

用户从“更多 → 网络设置”输入 SSID 和密码后：

```text
LVGL 网络页（主 UI 线程）
  -> 独立 8 KiB PSRAM pthread
  -> smart_home_network_connect_credentials()
  -> board_esp_hosted_wifi_connect()
  -> ESP-Hosted RPC: storage=RAM / set_sta_config / wifi_connect
  -> wlan0 DHCP + DNS probe
  -> 成功获取 IP 后原子更新 /data/smart_home/secrets.json
```

凭据仅保存至 `secrets.json`，不能写入 `settings.json`、defconfig、日志或 Git。
下次 `smart_home` 启动会优先读取该文件并尝试自动连接。资源镜像是完整 `/data`
快照；若用不含当前 Wi-Fi 凭据的 `WITH_SECRETS=1` 镜像重刷资源，需要重新配网。

### 3.1 2026-09-18 手机热点真机复测

手机热点已完成一次真实闭环：C6 成功关联并打开 `wlan0` carrier，DHCP 统计为
`tx_dhcp=2`、`rx_dhcp=2`、`tx_errors=0`、`rx_dropped=0`；随后 NuttX 获得 IPv4/网关，
DNS 验证成功，cAGENT 已连接模型服务 `api.deepseek.com:443`。因此顶栏在该阶段显示
在线 Wi-Fi 状态是有真实网络依据的，而非 UI 模拟状态。

这不表示模型对话已完成验收：串口在 TCP 建连及 TLS 上下文建立后报告
`FATAL: read zero bytes from port`，没有 TLS 完成、HTTP 状态或模型响应日志。该问题应与
Wi-Fi 接入分开定位，并在下次测试中保留复位后的完整启动与 TLS 日志。

此前某 AP 的失败日志为 DHCP Client 报文已发送但 `rx_dhcp=0`；与手机热点的有效 DHCP
应答对照后，应优先检查该 AP 的 DHCP/接入策略，而不要将其误写成 C6 WLAN 数据面失效。

当前不支持附近热点扫描：P4 现有的 C6 ESP-Hosted 接口未接入 scan 结果 RPC。后续
扫描功能应以该 RPC 和异步结果列表为前提，不能从 UI 层伪造热点列表。

## 4. 顶栏状态和本轮未提交 UI 修复

### 4.1 Wi-Fi

顶栏 Wi-Fi 图标由 `smart_home_network_status_t` 驱动：

| 状态 | 图标 | 颜色 |
| --- | --- | --- |
| DNS/互联网可用 | `asset:wifi` | 绿色 |
| 已取得 IP、但 DNS/互联网不可用 | `asset:wifi` | 琥珀色 |
| 未关联、无 IP 或状态未知 | `asset:wifi-off` | 灰色 |

`wifi-off` 当前使用已生成的 32 px A8 描述符并在顶栏 20 px 控件内显示；如真机
观察到小尺寸边缘不够清晰，可补生成原生 `wifi-off_20.c`，不改变逻辑名称。

### 4.2 网络页键盘

网络输入框聚焦时，键盘必须：

1. 位于 `SMART_HOME_NAV_H + SMART_HOME_NAV_BOTTOM_PAD + 8` 上方；
2. 通过 `lv_obj_move_foreground()` 位于导航栏前景；
3. 点击“连接”后隐藏。

这避免键盘被底部导航栏遮挡。设备编辑页已有相同避让模式，可作为后续输入页的
参考；不要把键盘直接贴到屏幕 `BOTTOM_MID, y=0`。

### 4.3 品牌文案

首页 Agent 卡片为 `Hi，OpenVela`，聊天输入 placeholder 为“问问 OpenVela…”。
本次只修改名称，不改变 cAGENT、模型或人格提示词。

## 5. 尚未实现：摄像头与麦克风真实状态

顶栏摄像头、麦克风当前仍为静态可用图标；首页也尚无对应开关。后续不可只在 UI
层切换 `camera`/`camera-off`、`microphone`/`microphone-off`，否则会产生假状态。

正确的下一阶段是：

1. 为 SC2336 流控制和音频/KWS 采集建立应用层状态与控制 API；
2. 由 Native 服务维护 `camera_enabled`、`microphone_enabled` 和不可用状态；
3. UI 订阅这些状态，更新顶栏图标；
4. 首页开关调用真实 API，成功回调后再更新画面；硬件不可用时禁用开关并显示原因。

摄像头适配现状和 RAW 验收边界见
[ESP32-P4X-SC2336适配交接记录](../../硬件适配/ESP32-P4X-SC2336适配交接记录.md)。

## 6. 构建、烧录和验收

完整命令和资源布局以
[ESP32-P4X SmartHome 编译、烧录与资源部署指南](../../开发指南/ESP32-P4X-SmartHome编译烧录与资源部署指南.md)
为准。固件修改后的最小流程：

```bash
cd ~/openvela
source myenv/bin/activate

./build.sh \
  contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/smart_home \
  -j8

esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write-flash -fs 16MB -fm dio -ff 80m \
  0x2000 nuttx/nuttx.bin
```

本轮低风险 UI 修改不新增 LittleFS 文件，只需重建、烧录固件；不要因这几项 UI
修改重刷整个 `/data` 分区。启动后验收：

```text
nsh> smart_home
```

1. 无网络时顶栏显示灰色 `wifi-off`；
2. 进入“更多 → 网络设置”，点 SSID 和密码输入框，确认键盘在底栏上方且可完整操作；
3. 首页显示 `Hi，OpenVela`，聊天框显示“问问 OpenVela…”；
4. 输入真实 SSID/密码，确认连接后的图标与颜色按第 4.1 节变化；
5. 重启并再次运行 `smart_home`，确认保存的凭据能自动使用；严禁用 `cat` 打印
   `secrets.json`。
6. 若验证模型对话，须另外确认 TLS 握手、HTTP 状态和模型响应；仅看到
   `tcp connected ...:443` 时，不得记录为模型调用成功。

## 7. 已完成的验证与边界

宿主回归测试命令：

```bash
cmake -S contest2026_031_niudanxianqianchong/demos/smart_home/tests \
  -B /tmp/smart-home-tests-network
cmake --build /tmp/smart-home-tests-network -j4
ctest --test-dir /tmp/smart-home-tests-network --output-on-failure
```

结果：5/5 通过（`config_store`、`secrets`、`device_service`、
`quickapp_provider`、`quickapp_ipc_server`）。这些测试覆盖配置、凭据和服务
契约，不覆盖 LVGL 图层或 P4+C6 真机网络。2026-09-18 已补充手机热点实板验收至 DHCP、DNS
与模型端 TCP；TLS/HTTP/模型响应和其他 AP 兼容性仍是后续必要验收。
