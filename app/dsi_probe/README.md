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
已完成 BTA 读请求但面板没有提供 payload；该结果不影响 M1 命令写链路验收。此时
应在后续 DPI video 模式下，以实际画面验证面板的像素输出链路。任何 Host 初始化、
FIFO 或初始化写序列的失败，都会输出明确步骤和负 errno。

## M2c：EK79007 DPI Panel + `draw_bitmap()` 色条扫描

在启用 `CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN=y` 的
`dsi_probe` 配置中执行：

```console
nsh> dsi_probe video 60
```

该命令对齐 ESP-IDF 的 EK79007 创建路径：`ek79007_panel_setup()` 创建一块
caller-owned DPI Panel；`ek79007_panel_initialize()` 先完成 DCS/sleep out，再启动
其单帧 GDMA scanout；Probe 取得该 panel 的 PSRAM RGB565 frame buffer，填入八段
色条后调用 `ek79007_panel_draw_bitmap()`。video 路径严格对齐 ESP-IDF 的外层
生命周期：先保持 GPIO26 背光关闭，创建 Host/DBI/panel 后才执行 GPIO27 硬件复位；
随后完成 panel init 并发送 display on，最后提交色条并开启 GPIO26 背光。为避免
引入非参考流量，video 路径跳过 command-only probe 使用的 generic DSI packet。

首版 profile 与 Espressif 的 `EK79007_1024_600_PANEL_60HZ_CONFIG` 对齐：
1024×600、RGB565、52 MHz nominal pixel clock、HSYNC 10/HBP 160/HFP 160、VSYNC
1/VBP 23/VFP 12。帧缓冲为 1,228,800 B，低于之前 RGB888 固定色条的 1,843,200 B。

该测试仍不注册 `/dev/fb0`，不实现 NuttX framebuffer lower-half，也不启动 LVGL。
它验证 EK79007 DCS、DPI 时序、DSI bridge、GDMA、cache 同步、D-PHY、背光和
`draw_bitmap()` 的单帧提交闭环；它**不**证明部分刷新、双缓冲、vsync 回调或 LVGL
flush 已可用。
