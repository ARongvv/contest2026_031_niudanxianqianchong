# ESP32-P4X MIPI-DSI Host Probe

`dsi_probe` 是 ESP32-P4X-Function-EV-Board 的独立命令模式验证程序。它验证
DSI Host、D-PHY、面板硬件 reset、generic short/long packet，以及 EK79007 的
两 lane 和默认初始化写序列；不启动 DPI video、GDMA、framebuffer、LVGL 或背光。

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
