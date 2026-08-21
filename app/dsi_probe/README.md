# ESP32-P4X MIPI-DSI Host Probe

`dsi_probe` 是 ESP32-P4X-Function-EV-Board 的分阶段 MIPI-DSI 验证程序。它验证
DSI Host、D-PHY、面板硬件 reset、generic short/long packet，以及 EK79007 的
两 lane 和默认初始化写序列。

构建时选择 `LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE`，NSH 中执行：

```console
nsh> dsi_probe
```

成功必须看到 `host bus=0 initialized`、两条 packet accepted、
`EK79007 DCS initialisation writes accepted` 和最终 `PASS`。

`DCS power-mode read` 是可选诊断项：若看到 `DCS read=unavailable`，表示 Host
已完成 BTA 读请求但面板没有提供 payload；该结果不影响 M1 命令写链路验收。此时
应在后续 DPI video 模式下，以实际画面验证面板的像素输出链路。任何 Host 初始化、
FIFO 或初始化写序列的失败，都会输出明确步骤和负 errno。

## M2a：Host 内建色条

在启用 `CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE_VIDEO_PATTERN=y` 的
`dsi_probe` 配置中执行：

```console
nsh> dsi_probe video 60
```

该命令在完成 EK79007 DCS 初始化、sleep out 和 display on 后，使用 P4 DSI Host
的内部 vertical colour-bar generator，以 P4X 的 1024×600 timing 启动 DPI video，
并将 GPIO26 的 LCD adapter PWM 输入拉高。应观察到稳定色条；60 秒后程序会关闭
背光、停止 video、释放 Host 并返回 NSH。

此测试不分配 framebuffer，不使用 GDMA/DMA2D，也不启动 LVGL。因此它验证的是
面板、DPI 时序、DSI bridge、packetizer、D-PHY 和背光的显示闭环；它**不**证明
framebuffer 更新、cache 同步、`/dev/fb0` 或 LVGL flush 已可用。
