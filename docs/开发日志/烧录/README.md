# ESP32-P4 烧录与控制台排障记录

本文记录 `esp32p4-function-ev-board` 最小 NSH 验证中已经复现的两个
问题。记录时间：2026-08-20。

## 问题一：Simple Boot 镜像写入 `0x0` 后 ROM 循环报 `invalid header`

### 现象

构建可生成有效的 `nuttx/nuttx.bin`：

```text
esptool.py -c esp32p4 elf2image --ram-only-header \
  -fs 16MB -fm dio -ff 80m -o nuttx.bin nuttx
Successfully created ESP32-P4 image.
```

将该镜像按当前构建规则写到 `0x0` 后，串口出现复位循环：

```text
ESP-ROM:esp32p4-eco7-20260109
rst:0x7 (HP_SYS_HP_WDT_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)
invalid header: 0x06700513
```

### 已排除项

从板端 Flash 读取 `0x0` 的前 256 字节，与本地镜像逐字节一致，且首字节
均为有效 ESP 镜像头 `0xe9`：

```text
Flash 0x0:    e9 03 02 4f 2e 53 f4 4f ...
nuttx.bin:    e9 03 02 4f 2e 53 f4 4f ...
```

本地镜像经 `esptool.py image-info` 验证为 ESP32-P4 镜像，包含 3 个 RAM
段，大小为 16MB / DIO / 80MHz，校验和有效。

### 根因

ESP32-P4 ROM 的 Flash 启动入口是 `0x2000`。当前工作区的
`nuttx/tools/espressif/Config.mk` 却在 `CONFIG_ESPRESSIF_SIMPLE_BOOT=y`
时无条件设置：

```make
APP_OFFSET := 0x0000
```

因此 ROM 从 `0x2000` 读取到的不是新镜像头，而是写在 `0x0` 的镜像中间
内容：

```text
nuttx.bin + 0x2000: 13 05 70 06 ...
ROM 输出：             invalid header: 0x06700513
```

二者完全对应，说明错误是启动偏移不匹配，而非镜像损坏、Flash 读写失败或
SPI Flash 模式错误。

### 临时烧录方法

在正式修复构建规则前，将 Simple Boot 镜像写到 P4 ROM 规定的位置：

```bash
cd ~/openvela

myenv/bin/esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write_flash -fs 16MB -fm dio -ff 80m \
  0x2000 nuttx/nuttx.bin
```

### 正式修复

将 `nuttx/tools/espressif/Config.mk` 的 Simple Boot 分支改为按芯片选择
应用镜像偏移：

```make
else ifeq ($(CONFIG_ESPRESSIF_SIMPLE_BOOT),y)
ifeq ($(CONFIG_ARCH_CHIP_ESP32P4),y)
        APP_OFFSET := 0x2000
else
        APP_OFFSET := 0x0000
endif
        APP_IMAGE := nuttx.bin
        FLASH_APP := $(APP_OFFSET) $(APP_IMAGE)
        ESPTOOL_BINDIR := .
endif
```

该逻辑与上游 NuttX 的 ESP32-P4 Simple Boot 处理一致。修复后应使用：

```bash
cd ~/openvela/nuttx
make flash ESPTOOL_PORT=/dev/ttyACM0
```

并确认最终 `esptool.py write_flash` 命令中包含 `0x2000 nuttx.bin`。

## 问题二：USB-JTAG 口可见 ROM 日志，但看不到 NuttX 的 `nsh>`

### 现象

主机只枚举到一个 USB-Serial/JTAG 设备：

```text
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_... -> ../../ttyACM0
/dev/ttyACM0
```

在 `/dev/ttyACM0` 上可以看到 ESP-ROM 的启动信息，但启动 NuttX 后没有
`nsh>` 输出。

### 根因

当前 `nsh/defconfig` 使用物理 UART0 作为 NuttX console：

```text
CONFIG_UART0_SERIAL_CONSOLE=y
CONFIG_ESPRESSIF_UART0_TXPIN=37
CONFIG_ESPRESSIF_UART0_RXPIN=38
CONFIG_UART0_BAUD=115200
# CONFIG_ESPRESSIF_USBSERIAL is not set
```

`/dev/ttyACM0` 是 USB-Serial/JTAG 通道，能显示 ROM 日志；而 NuttX 的
`/dev/console` 被路由至 GPIO37/GPIO38 的 UART0。两者不是同一输出通道。
因此“ROM 有日志、NuttX 无日志”不能直接判定为启动失败。

### 验证路径

优先使用已有的 USB console 配置，不必连接额外 USB-TTL：

```bash
cd ~/openvela
export PATH="$PWD/prebuilts/gcc/linux-x86_64/riscv-none-elf/bin:$PATH"

./build.sh \
  vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/usbconsole \
  -j"$(nproc)"
```

该配置包含：

```text
CONFIG_ESPRESSIF_USBSERIAL=y
# CONFIG_ESPRESSIF_UART0 is not set
```

烧录时仍应使用问题一中的 `0x2000` 偏移。烧录完成后：

```bash
picocom -b 115200 /dev/ttyACM0
```

按板载 RST/EN 键，预期在同一终端看到 NuttX 启动日志和 `nsh>`。

另一条路径是外接 3.3V USB-TTL：

```text
P4 GPIO37 (UART0 TX) -> USB-TTL RX
P4 GPIO38 (UART0 RX) -> USB-TTL TX
P4 GND               -> USB-TTL GND
```

禁止将 5V 电平直接接入 P4 UART 引脚。

## 当前结论与后续顺序

1. 先使用 `usbconsole` 配置、`0x2000` 偏移烧录，验证 ROM 到 NuttX 到
   `nsh>` 的完整最小启动链路。
2. 将 P4 `0x2000` Simple Boot 偏移修复并入构建规则，避免 `make flash`
   再次默认写到 `0x0`。
3. 再验证 `nsh`（UART0 console）配置，并记录实际 UART0 物理接口或
   USB-TTL 接线方式。
