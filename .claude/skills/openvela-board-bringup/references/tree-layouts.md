# 三种源树风格与注册点定位

目录约定:路径相对 openvela 工作区根(本仓的上一级目录)。

## 目录

- [上游 NuttX 风格](#上游-nuttx-风格)
- [openvela vendor 风格](#openvela-vendor-风格)
- [Route A 风格(本仓)](#route-a-风格本仓)
- [通用构件与注册点](#通用构件与注册点)
- [自检问题](#自检问题)

## 上游 NuttX 风格

```
nuttx/boards/<arch>/<family>/<板>/
├── src/          # 板级初始化与外设注册(按子系统拆文件)
├── include/
├── Kconfig       # 板选项
├── scripts/      # 链接脚本、烧录(如 flash.sh)、Make.defs
└── configs/<demo>/defconfig   # 每个演示一个配置
nuttx/boards/<arch>/<family>/common/   # 家族共享代码
```

实例:`nuttx/boards/xtensa/esp32/esp32-devkitc`(54 个 configs)、`esp32-lyrat`(音频)、`esp32-audio-kit`、`esp32-ethernet-kit`。家族目录 `nuttx/boards/xtensa/esp32s3` 等。

## openvela vendor 风格

```
vendor/<厂商>/
├── chips/<chip>/        # 芯片层(HAL、公共驱动)
└── boards/<family>/
    ├── common/          # 家族共享
    └── <板>/
        ├── configs/ scripts/ include/ src/ Kconfig
        └── pack/        # 资源打包(vendor 树特有)
```

实例:`vendor/espressif/boards/esp32s3/{esp32s3-box, esp32s3-eye}`、`vendor/espressif/boards/esp32p4`、`vendor/artinchip/boards/d12x/demo68-nor`。其他厂商树:allwinnertech、beken、bes、flagchip、gigadevice、rockchip、st 等(均在 `vendor/` 下)。

## Route A 风格(本仓)

```
chips/esp32p4/
├── esp-hal-3rdparty/    # 芯片 HAL 三方库(注意副本问题, 见 boards/esp32p4x.md)
├── include/  common/
board/
├── esp32p4/
│   ├── common/                     # 家族共享(scripts/include/src)
│   └── esp32p4-function-ev-board/  # 板实例(configs/scripts/include/src)
└── goldfish-arm64/configs/         # 仿真配置(smart_home 等), 先仿真后真机
```

## 通用构件与注册点

三种风格的板目录构件一致,新增外设时逐个对齐:

| 构件 | 作用 | 动作 |
|---|---|---|
| `src/` | 板级初始化、外设注册函数 | 新增注册文件或在 `src/Makefile` 追加对象 |
| `include/` | 板头文件、引脚定义 | 新外设的引脚/资源宏 |
| `Kconfig` | 板级选项 | 新增 `CONFIG_<板>_<外设>` 选项 |
| `scripts/` | 链接/烧录脚本 | 芯片内存布局变化时才动 |
| `configs/<demo>/defconfig` | 演示矩阵 | 新演示 = 新目录 + defconfig;用 `savedefconfig` 生成,保持字母序 |

注册点定位手法:在目标板目录 `grep -r "外设名\|驱动名" src/ Kconfig`;再对照参考板同名文件(见 reference-boards.md)看差集。

## 自检问题

改动落位前回答:

1. 这段代码是**芯片公共**(进 chips/ 或 family common)还是**板实例**(进板 src/)?
2. 新外设是否需要新 Kconfig 选项 + defconfig 组合,还是复用现有 CONFIG?
3. 演示 defconfig 是否独立成 configs/<demo>,没混进别的 demo?
4. 是否动到了 `scripts/`(内存布局/烧录),动了就要全量验证启动链?
