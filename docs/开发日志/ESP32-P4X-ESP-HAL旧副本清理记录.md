# ESP32-P4X ESP HAL 旧副本清理记录

日期：2026-09-10

## 背景

`chips/esp32p4` 下曾同时存在两个 Espressif HAL 本地检出目录：

```text
esp-hal-3rdparty
esp-hal-3rdparty.incomplete-local-copy
```

前者是 ESP32-P4 构建使用的供应商依赖；后者来自早期本地恢复过程。后者目录占用约 531 MiB，容易使开发者误把不匹配的头文件、HAL 实现或本地修改用于 P4 构建，因此需要清理。

## 判定依据

| 项目 | 当前依赖 | 旧副本 |
| --- | --- | --- |
| Git 提交 | `b90b1837cb5ad24747deb4c895246037cc206ce5` | `9fc713a95b1ff150dd0b0647e465d3c624056bb1` |
| 提交日期 | 2026-07-01 | 2025-06-18 |
| 受 Git 跟踪文件数 | 9,787 | 4,551 |
| 同路径文件数 | \- | 2,653 |
| 同内容文件数 | \- | 839 |

两个提交不构成前后继关系。它们虽然有部分同路径文件，却有 1,814 个同路径文件的内容不同，不能将旧副本视为当前依赖的可替换缓存。

历史记录 [`dev.md`](dev.md) 已明确说明：旧副本缺少 P4 构建所需的 `nuttx/esp32p4` 和 `nuttx/src/platform/os.c`，不得作为 P4 构建依赖。

构建侧只使用规范目录：

- [`chips/esp32p4/hal_esp32p4.mk`](../../chips/esp32p4/hal_esp32p4.mk) 从 `$(ESP_HAL_3RDPARTY_REPO)` 引入 HAL 的 I2C、CSI、ISP、EMAC 等源文件；
- [`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md) 固定当前依赖提交为 `b90b1837cb5ad24747deb4c895246037cc206ce5`；
- [`scripts/apply_p4x_hal_patches.sh`](../../scripts/apply_p4x_hal_patches.sh) 默认只对 `chips/esp32p4/esp-hal-3rdparty` 应用 P4X NuttX 兼容补丁。

在构建文件、Kconfig 和脚本中均未找到对 `esp-hal-3rdparty.incomplete-local-copy` 的引用。

## 旧副本中的本地修改

旧副本含一处未提交修改：

```c
#define LOCK_INITIALIZER_UNLOCKED SP_UNLOCKED
```

该修改位于 `components/esp_hw_support/clk_ctrl_os.c`。当前固定 HAL 的对应提交使用 `0`，且当前 P4X 摄像头构建和 `video_test 300` 的 30 FPS 验收均基于当前 HAL 完成。因此不将该旧副本改动整体迁移；如未来出现时钟锁初始化相关编译或运行问题，应在当前固定 HAL 上复现、评审并形成独立的受管理补丁。

## 清理内容

1. 将 `chips/esp32p4/esp-hal-3rdparty.incomplete-local-copy` 移入系统回收站，使其不再位于项目工作区。
2. 删除 [`chips/esp32p4/.gitignore`](../../chips/esp32p4/.gitignore) 中对应的过期忽略规则。
3. 保留当前 `esp-hal-3rdparty`、其固定提交、P4X 兼容补丁和历史开发记录。

## 验证

清理后应确认：

```bash
test ! -e chips/esp32p4/esp-hal-3rdparty.incomplete-local-copy
git -C chips/esp32p4/esp-hal-3rdparty rev-parse HEAD
test -f chips/esp32p4/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h
test -f chips/esp32p4/esp-hal-3rdparty/nuttx/src/platform/os.c
git diff --check
```

本次清理不改变参与编译的源文件、Kconfig 或板级配置，不需要重新验证摄像头数据链路；后续正常构建仍会使用固定的当前 HAL 和受管理补丁。
