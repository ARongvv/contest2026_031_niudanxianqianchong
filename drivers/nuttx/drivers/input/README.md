# GT911 touch driver

`gt911.c` 与 `gt911.h` 是 P4X 触摸适配的竞赛开发源码。GT911 属于
Goodix GT9XX 系列，开始具体协议实现前必须先评估 NuttX 现有
`drivers/input/gt9xx.c` 是否已满足需求。

若保留独立 GT911 驱动，应只实现现有 GT9XX 通用驱动不能覆盖的能力，并在
设计说明中记录差异。P4X 板级层负责 I2C 控制器、GPIO reset、可选 INT 与
设备路径；通用驱动不得硬编码这些板级信息。
