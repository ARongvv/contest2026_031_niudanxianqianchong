---
name: openvela-board-bringup
description: "openvela 新板/新外设 bring-up 的系统级流程与排障。覆盖三种源树风格定位(上游 nuttx/boards、vendor 厂商树 chips+boards、Route A chips+board)、从最小系统(boot 串口 NSH)到显示、输入、音频、摄像头、网络、存储的分阶段验收, probe-first 隔离验证法, defconfig 演示矩阵, 先仿真后真机, 按外设域索引参考板, 板级玄学坑沉淀(esp32p4x 首例)。Trigger: 新板适配、板级bringup、board bring-up、外设点亮、点亮屏幕/摄像头/触摸/音频、外设接入后无输出、probe 验证、bringup 阶段划分、真机验收 checklist、custom board adaptation。不含单个驱动的代码实现(用 nuttx-driver-development)、编译烧录命令执行(用 openvela-build / openvela-esp32-workflow)。"
---

# openvela 板级 Bring-up

从"一块新板"到"外设逐项真机验收"的系统级流程。先用本技能定阶段和验收标准;写驱动代码时转 `nuttx-driver-development`;执行编译烧录时转 `openvela-build`(工作区)或 `openvela-esp32-workflow`(本仓,ESP32 专属)。

## 第一步: 判定源树风格

动任何代码前,先确定目标板属于哪种源树组织,找到目录层级和注册点:

| 风格 | 位置 | 适用 |
|---|---|---|
| 上游 NuttX | `nuttx/boards/<arch>/<family>/<板>` | 社区公共板 |
| openvela vendor | `vendor/<厂商>/{chips,boards}/<family>/<板>` | 厂商树 |
| Route A | `chips/<chip>` + `board/<family>/{common,<板>}` | 自定义芯片/板(比赛仓形态) |

三种风格的构件同名(`src/include/Kconfig/scripts/configs`),注册点定位方法见 [references/tree-layouts.md](references/tree-layouts.md)。

## 阶段骨架

按依赖顺序推进,**每阶段完成"四件套"后才进下一阶段**:

```
Phase 0 定位与盘点 → 1 最小系统 → 2 显示 → 3 输入
                  → 4 音频 → 5 摄像头 → 6 网络 → 7 存储/大内存
```

四件套 = ① defconfig 组合独立成 `configs/<demo>` ② probe/最小示例单独验证 ③ 真机验收证据 ④ 复盘文档落位。

各阶段的目标、验证命令、验收证据、常见失败与升级手段,见 [references/phase-checklist.md](references/phase-checklist.md)。

## probe-first 规则

- 每个外设先跑独立 probe 应用验证物理链路(枚举/寄存器/中断/数据流),再接子系统(fb/input/audio/video)。本仓范例:`app/csi_probe`、`app/dsi_probe`、`app/gt911_probe`。
- 证据分级下结论:枚举成功 ≠ 数据有效 ≠ 时序/帧率达标。
- 排障时单变量切换:一次只改 probe 参数、defconfig 开关、时钟或引脚中的一处。

## 先仿真后真机

- 仿真(goldfish/qemu)验证协议与应用逻辑;真机验证驱动、资源、TLS 与交互。
- 任何测试结论必须带平台标签,不互相替代。

## 参考板索引

接入某类外设前,先在 [references/reference-boards.md](references/reference-boards.md) 按外设域找到工作区内同域参考板(摄像头/显示/音频/以太网),读它们的实现再动手。

## 板级 quirks

每块板的"玄学坑"一个文件,只放该板特有知识,见 [references/boards/](references/boards/)。当前实例:

- [esp32p4x.md](references/boards/esp32p4x.md) — ESP32-P4X Function EV Board(ESP-HAL 副本与软链接、SC2336 CSI、ES8311+GDMA、C6 托管、构建链接玄学等,含仓内文档索引)

适配新板时新建 `references/boards/<板>.md`,沿用其结构;通用知识不进板级文件。

## 复盘文档

每阶段验收或每次故障收敛后,把结论落位到该板文档目录(本仓惯例:`docs/硬件适配/` 放适配方案与交接、`docs/开发日志/` 放故障复盘),文件名含板名与主题。技能引用的是文档路径,正文以仓内文档为唯一事实源,不复制内容。
