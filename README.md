# ESP32-P4 Function EV Board openvela 适配

本项目参加 2026 openvela AI 硬件开发者大赛「新硬件适配」赛道，目标是以
Route A（vendor custom chip / board）方式，将 **ESP32-P4 Function EV
Board** 接入 openvela。

当前工作的第一阶段是最小 `nsh` 固件在目标板启动。板载音频、显示、触摸和
ESP32-C6 无线协处理器均为后续外设验证，不是本阶段的前置条件。

## 当前状态

截至 2026-08-19：

- 已纳入 ESP32-P4 芯片层、共享板级层和 Function EV Board 板级层。
- manifest 已映射 P4 源码到 `vendor/espressif/` 下的构建位置。
- `configs/nsh` 已使用 RV32 RISC-V 配置，并固定 P4 所需的 flash 参数。
- P4 HAL 依赖版本已固定，且包含适配当前 openvela 任务创建接口的自动补丁。
- 尚未重新取得完整的 `nsh` build pass、烧录成功或串口 `nsh>` 日志；请以实际
  构建和实板验证结果为准。

## 目录与链接关系

所有项目源码只在本仓维护。`repo sync` 后，manifest 会将下列目录链接到
openvela 构建树；不要手工复制这些文件到 `nuttx/` 或 `vendor/`。

| 本仓目录 | 构建树位置 | 用途 |
| --- | --- | --- |
| `chips/esp32p4/` | `vendor/espressif/chips/esp32p4/` | P4 custom chip 层、共享 Espressif 代码及 HAL 集成 |
| `board/esp32p4/common/` | `vendor/espressif/boards/esp32p4/common/` | P4 板级共享代码与链接脚本 |
| `board/esp32p4/esp32p4-function-ev-board/` | `vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/` | Function EV Board 的配置和 bring-up |

`packages/cAGENT` 与 `demos/smart_home` 的 manifest 链接已关闭，不参与当前
P4 最小系统构建。

## 构建最小 NSH

### 前置条件

- 已按本仓 `contest2026_031_niudanxianqianchong.xml` 同步完整 openvela 工作区。
- 可用的 RV32 RISC-V 工具链（配置选择 `CONFIG_RISCV_TOOLCHAIN_GNU_RV32=y`）。
- 首次构建可访问 GitHub，或已准备好固定版本的 `esp-hal-3rdparty` 依赖。

在 **openvela 工作区根目录**（本仓的上一级）执行：

```bash
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

构建配置来自
`board/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig`。重点确认配置
阶段生成的 `nuttx/.config` 含有 `CONFIG_ARCH_RV32=y`、
`CONFIG_ARCH_CHIP_ESP32P4=y` 和 `CONFIG_ESPRESSIF_CHIP_SERIES="esp32p4"`。

若构建失败，请记录并处理第一个真实错误；不要同时修改多个无关模块。烧录命令、
固件分段和 flash offset 必须以最终构建产物及 bootloader 输出为准，尚未在本项目
中完成实板确认。

## HAL 兼容补丁

P4 芯片层会拉取固定提交的 `esp-hal-3rdparty`。其中
`nuttx/src/platform/os.c` 仍使用旧版 `nxtask_init()` 调用方式，而当前
openvela/NuttX 需要通过 `posix_spawnattr_t` 传递优先级和栈大小。

补丁位于：

```text
chips/esp32p4/common/espressif/patches/
└── 0001-nuttx-platform-openvela-nxtask-init.patch
```

`chips/esp32p4/common/espressif/Make.defs` 会在 HAL clone/reset/checkout 后先
检查再应用此补丁。因此不要直接提交
`chips/esp32p4/esp-hal-3rdparty/` 中的临时修改；应更新补丁并在固定 HAL 提交上
验证它能够应用。

## 推荐验证顺序

1. 通过 `nsh` 配置并完成完整构建。
2. 确认生成的 bootloader 和 NuttX 固件，以及对应的烧录方式。
3. 在实板获取串口启动日志与 `nsh>` 提示符。
4. 依次验证 UART、GPIO、I2C、SPI Flash、PSRAM、以太网。
5. 最后再接入板载 ES8311 音频、LCD/MIPI-DSI、触摸及 ESP32-C6 网络功能。

## 相关文档

| 文档 | 内容 |
| --- | --- |
| [P4 Function EV Board 适配文档](docs/esp32p4-ev-board-adaptation.md) | 硬件差异、适配范围、进度和排障记录 |
| [Route A 移植方案](docs/esp32p4-function-ev-board-route-a-porting.md) | custom chip / board 架构与移植路径 |
| [开发记录](docs/dev.md) | 历史构建问题与处理依据；不是当前构建成功的证明 |
| [AI Coding 日志说明](logs/README.md) | 对话日志的目录与提交格式 |
| [第三方依赖与许可证声明](THIRD_PARTY_NOTICES.md) | P4 最小构建的依赖、版本及许可证 |

## 开发约定

- 只修改 `contest2026_031_niudanxianqianchong/` 内的项目源码、文档和补丁。
- 不将 `nuttx/`、`apps/`、`vendor/` 等工作区公共目录的临时改动混入本仓提交。
- 保持提交可审阅：板级移植、第三方 HAL 兼容补丁和文档应分开提交。
- 本仓新增内容采用 [Apache License 2.0](LICENSE)；发布前按
  [第三方依赖声明](THIRD_PARTY_NOTICES.md) 复核随附材料的授权。
