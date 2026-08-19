# ESP32-P4 构建与 Kconfig 排障记录

本文记录 2026-08-19 为 `esp32p4-function-ev-board` 构建最小 `nsh`
配置时遇到的配置生成和编译问题。它是一次构建链路排障记录，不代表当前已经
获得完整 build pass 或实板 `nsh>` 验收结果。

## 适用范围与标准入口

板级配置的标准入口是：

```bash
./build.sh vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

`vendor/espressif/boards/esp32p4/esp32p4-function-ev-board` 是指向竞赛工程
`board/esp32p4/esp32p4-function-ev-board` 的软链接。前者用于保持与工作区标准
板级目录一致；实际 custom board 与 custom chip 源码仍在竞赛工程中。

本板的 HAL 兼容层接入 NuttX Make 构建链路，当前只使用 Make 路径；不要改用
`--cmake` 作为本问题的绕过手段。

## 现象

直接构建时，`nuttx/.config` 没有补齐 Kconfig 默认值，随后 C 编译阶段出现：

```text
unknown type name 'g_interrupt_context'
CONFIG_NCPUS undeclared
CONFIG_STREAM_OUT_BUFFER_SIZE undeclared
CONFIG_STREAM_HEXDUMP_BUFFER_SIZE undeclared
CONFIG_STREAM_BASE64_BUFFER_SIZE undeclared
```

这些宏分别影响 per-CPU、调度器和 stream 结构定义。它们不是 ESP32-P4
defconfig 中应逐项手工维护的选项。

## 根因链路

问题的顺序如下：

1. `build.sh` 配置板级目录后继续执行 Make，但不会替代一次成功的
   `olddefconfig`。因此已有的、未展开的 `.config` 会被继续用于编译。
2. 初次执行 `make -C nuttx olddefconfig` 时系统没有 `kconfig-conf`。安装
   `kconfig-frontends-nox` 后，Kconfig 才实际开始解析；该安装不是根因，只是
   让原有配置问题可见。
3. 解析暴露了两项基础树问题：
   - `nuttx/arch/tricore/Kconfig` 第 116、122 行将帮助标记写成 `--help--`；
     NuttX 语法应为 `---help---`。解析器在此失步后出现的跨文件
     `endif`/`endmenu` 报错均为连锁现象。
   - 自动生成的 `apps/examples/Kconfig` 仍 `source` 不存在的
     `apps/examples/audio_record/Kconfig`，属于过期索引，不是本板需要启用
     `audio_record`。
4. 因 `olddefconfig` 未成功结束，`.config` 没有生成 `CONFIG_NCPUS`、stream
   buffer 默认值及相应的 per-CPU 配置，最终表现为前述 C 编译错误。

因此，先补写 `CONFIG_NCPUS` 等宏只能掩盖失败，后续仍会出现更多缺失的默认值。

## 已执行的处理

- 已将 `nuttx/arch/tricore/Kconfig` 两处帮助标记从 `--help--` 修正为
  `---help---`。这属于外层 NuttX 基础树的独立修复，不应混入竞赛工程的 P4
  板级提交。
- 已尝试以 `make -C apps -B preconfig` 全量再生 Apps Kconfig；该命令目前因
  `apps/import/scripts/Make.defs` 缺失而停止。
- 随后在 `apps/examples` 目录使用已有的 `../tools/mkkconfig.sh -m Examples`
  再生 examples 索引，生成结果已移除失效的 `audio_record/Kconfig` 引用。
  `apps/examples/Kconfig` 是生成且忽略的工作区文件，不应作为手写功能修改提交。

完成上述处理后，尚未重新执行 `olddefconfig` 或完整 P4 构建；当前状态只能说明
已消除已知的两个首要 Kconfig 阻塞点，不能据此宣称构建已通过。

## 建议的复测顺序

在工作区根目录执行。每一步成功后再进入下一步，以保留第一个真实错误：

```bash
make -C nuttx olddefconfig

grep -E '^CONFIG_(UP|PERCPU_ARRAY|NCPUS|SMP_NCPUS|STREAM_OUT_BUFFER_SIZE|STREAM_HEXDUMP_BUFFER_SIZE|STREAM_BASE64_BUFFER_SIZE)=' nuttx/.config

./build.sh vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/nsh -j2
```

判断原则：

- `olddefconfig` 必须以成功状态结束；若失败，只处理其报告的第一处 Kconfig
  解析或缺失文件问题，不要立即继续编译。
- `grep` 用于确认关键配置已由 Kconfig 写入 `.config`；具体取值由依赖关系和
  默认值决定，不应手工猜测或修改。
- 只有配置生成成功后，才判断后续 C/汇编错误是否属于 P4 HAL、custom chip 或
  custom board 的真实问题。
- 配置阶段显示 `No configuration change` 本身不表示失败；前提是前一步
  `olddefconfig` 已经成功完成。

## 当前验收状态

| 项目 | 状态 |
| --- | --- |
| `kconfig-conf` 可用 | 已具备（系统安装 `kconfig-frontends-nox`） |
| Tricore 两处 Kconfig 语法 | 已修正，待重新解析验证 |
| examples 过期 `audio_record` 索引 | 已再生并移除，待完整 Kconfig 流程验证 |
| `make -C nuttx olddefconfig` | 待复测 |
| ESP32-P4 最小 `nsh` 完整构建 | 待复测 |
| 烧录与串口 `nsh>` | 未开始 |

相关文档：[P4 最小 NSH 操作与测试](../../硬件适配/esp32p4-nsh-operation-and-test.md)、
[历史 P4 移植开发记录](../dev.md)。
