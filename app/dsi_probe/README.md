# ESP32-P4X MIPI-DSI Host Probe

`dsi_probe` 是 ESP32-P4X-Function-EV-Board 的独立命令模式验证程序。它只验证
DSI Host、D-PHY、面板硬件 reset、generic short/long packet 和一条 DCS 读链路；
不启动 DPI video、GDMA、framebuffer、LVGL、背光或 EK79007 完整初始化序列。

构建时选择 `LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE`，NSH 中执行：

```console
nsh> dsi_probe
```

成功必须看到 `host bus=0 initialized`、两条 packet accepted、DCS power mode 和
最终 `PASS`。任何失败都会输出明确的步骤与负 errno。
