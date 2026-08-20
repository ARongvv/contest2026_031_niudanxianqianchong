# GT911 touch driver

`gt911.c` 与 `gt911.h` 是 P4X 触摸适配的竞赛开发源码。它基于 NuttX
`touchscreen_upper` 的 lower-half 接口实现，提供：

- GT911 16 位寄存器 I2C 访问、产品 ID 探测和坐标状态确认；
- 至多五点触摸解析，携带稳定的 track ID、按下、移动和抬起事件；
- GPIO 中断模式：ISR 仅投递 LPWORK，I2C 读写始终在工作线程完成；
- 轮询模式：未提供完整 IRQ 回调时，使用 watchdog 定期投递同一个工作项。

NuttX 现有 `drivers/input/gt9xx.c` 采用独立 VFS 读取路径，且当前只读取
第一个触点。保留本驱动是为了适配 LVGL 所需的 `touch_lowerhalf_s.maxpoint`
语义、多点事件及 IRQ/轮询两种板级接线方式，并非复制一套同等功能的协议实现。

P4X 板级层负责 I2C 控制器、GPIO reset、可选 INT 与设备路径；通用驱动不得
硬编码这些板级信息。GT911 在 reset 释放时采样 INT 电平选择 I2C 地址，因此
板级 `reset()` 必须完整执行该电气时序。

当前源码尚未接入 NuttX 的 Kconfig、Make.defs/CMakeLists 或 P4X board glue；
这些将在确认 I2C 与 reset/INT 引脚后单独完成。
