# ESP32-P4X MIPI-DSI Host Probe

`dsi_probe` 是 ESP32-P4X-Function-EV-Board 的分阶段 MIPI-DSI 验证程序。它验证
DSI Host、D-PHY、面板硬件 reset、generic short/long packet，以及项目中
`ek79007_panel_*()` 提供的 EK79007 初始化生命周期。

构建时选择 `LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE`，NSH 中执行：

```console
nsh> dsi_probe
```

成功必须看到 `host bus=0 initialized`、两条 packet accepted、
`EK79007 panel driver initialisation accepted` 和最终 `PASS`。

`DCS power-mode read` 是可选诊断项：若看到 `DCS read=unavailable`，表示 Host
已完成 BTA 读请求但面板没有提供 payload；该结果不影响显示验收。当前应以
`pattern` 和 `video` 的实际画面验证面板输出链路。任何 Host 初始化、FIFO 或
初始化写序列的失败，都会输出明确步骤和负 errno。

## M2c：EK79007 DPI Panel + `draw_bitmap()` 色条扫描

在启用 `CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN=y` 的
`dsi_probe` 配置中执行：

```console
nsh> dsi_probe video 60
```

该命令对齐 ESP-IDF 的 EK79007 创建路径：`ek79007_panel_setup()` 创建一块
caller-owned DPI Panel；`ek79007_panel_initialize()` 先完成 DCS/sleep out，再启动
其单帧 GDMA scanout；Probe 取得该 panel 的 PSRAM RGB565 frame buffer，打开 GPIO26
背光、填入八段色条并调用 `ek79007_panel_draw_bitmap()`。video 路径严格对齐当前
已点亮 ESP-IDF 样例的外层生命周期：创建 Host/DBI/panel 后执行 GPIO27 硬件复位，
随后完成 panel init；不发送该组件不支持的 DCS display on，再提交首帧。为避免
引入非参考流量，video 路径跳过 command-only probe 使用的 generic DSI packet。

首版 profile 与 Espressif 的 `EK79007_1024_600_PANEL_60HZ_CONFIG` 对齐：
1024×600、RGB565、52 MHz nominal pixel clock、HSYNC 10/HBP 160/HFP 160、VSYNC
1/VBP 23/VFP 12。帧缓冲为 1,228,800 B，低于之前 RGB888 固定色条的 1,843,200 B。

该测试仍不注册 `/dev/fb0`，不实现 NuttX framebuffer lower-half，也不启动 LVGL。
它验证 EK79007 DCS、DPI 时序、DSI bridge、GDMA、cache 同步、D-PHY、背光和
`draw_bitmap()` 的单帧提交闭环；它**不**证明部分刷新、双缓冲、vsync 回调或 LVGL
flush 已可用。

Probe 会在视频刚启动和运行 1 秒后分别采样 2000 次 D-PHY 状态，采样间隔
50 us，每个窗口约 100 ms。`PHY activity` 中的 `clk/d0/d1` 表示对应 lane 离开
LP11 stop state 的采样次数，`transitions` 表示 stop-state 组合变化次数：

- `any=0` 且 `transitions=0`：采样窗口内所有 lane 始终处于 LP11，强烈指向
  Host/PHY 未真正输出视频活动；
- `any>0` 且 `transitions>0`：存在 lane 活动；若视觉异常，应继续沿 packet、timing
  和面板解析方向排查；
- `lock_lost>0`：D-PHY PLL 在采样期间不稳定。

离开 stop state 只是活动证据，不能单靠该寄存器断言采到的必然是有效 HS video
packet；最终仍需视觉结果或示波器/逻辑分析仪确认。

## M2d：DSI Host 内置 pattern 对照测试

执行：

```console
nsh> dsi_probe pattern 10
```

该命令复用 `video` 模式完全相同的 EK79007 reset、DCS 初始化、RGB565 DPI timing
和持续扫描生命周期，随后通过 `esp_mipi_dsi_video_pattern_set()` 将 Host 像素源切换
为内置竖向色条。GDMA 生命周期仍保持运行，以保留已验证的 DPI 启动顺序，但 Host
输出不再依赖 framebuffer 中的色条内容，因此可以排除 PSRAM 像素内容和 cache 同步
是否正确这两个变量。

- `pattern` 可见、`video` 异常：优先检查 framebuffer 内容、cache clean 和 GDMA
  像素搬运；
- 两者均异常且 PHY 有活动：优先对照已点亮 ESP-IDF 固件的 Host active timing、
  packet/color coding、DBI 命令传输模式和面板初始化状态；
- `pattern` 期间 PHY 无活动：检查 Host pattern 选择和 video-mode 启动状态。
