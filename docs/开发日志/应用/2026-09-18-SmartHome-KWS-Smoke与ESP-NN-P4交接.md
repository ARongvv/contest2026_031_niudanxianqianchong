# SmartHome KWS Smoke 与 ESP-NN P4 交接

交接日期：2026-09-18。

本文记录 ESP32-P4X `smart_home` 中 `wake_large` KWS 的当前验证基线，以及
ESP-NN P4 后端的后续接入边界。它不是音频采集或常驻唤醒词功能的完成声明。

## 1. 工作树和提交基线

KWS 工作在独立 worktree 中完成，避免覆盖主开发目录的未提交 UI 修改：

```text
/home/arongw/openvela/worktrees/contest2026_031_niudanxianqianchong-kws
分支：feat/p4x-smarthome-kws
基线：ce54fdf feat(smarthome): add P4 KWS smoke validation
```

接手前先确认本地状态：

```bash
cd ~/openvela/worktrees/contest2026_031_niudanxianqianchong-kws
git status --short
git log --oneline -5
```

本记录形成时，`ce54fdf` 已提交，工作树应保持干净。编译、烧录和 P4 真机运行由
接手者执行；当前没有以编译成功或真机耗时冒充已验证结论。

## 2. 已交付：reference KWS smoke

`kws_smoke` 是独立 NSH 命令，只验证以下基础能力：

1. `wake_large` int8 TFLite FlatBuffer 能由 TFLM 解析，且 schema 匹配；
2. 所需 9 个算子能注册：Shape、StridedSlice、Pack、Reshape、Conv2D、
   DepthwiseConv2D、Mean、FullyConnected、Softmax；
3. 静态、16 字节对齐的 tensor arena 能完成 `AllocateTensors()`；
4. 以确定性 int8 特征输入执行 warmup 和多次 `Invoke()`，打印 min/P50/mean/P95/max；
5. 打印 input/output 的量化参数、实际 arena 使用量和五类输出分数。

它刻意**不**做 ES8311 打开、PCM 录音、特征提取、LVGL 更新、网络访问、唤醒状态机
或 Agent 调用。因此 smoke 输出只说明推理链路可用，不能说明真实语音唤醒准确率。

相关文件：

| 文件 | 职责 |
| --- | --- |
| `demos/smart_home/src/kws_smoke/kws_smoke_main.cc` | 模型检查、算子 resolver、arena、计时和输出。 |
| `demos/smart_home/src/kws_smoke/models/model.cc` | 嵌入式 `wake_large` 模型字节数组。 |
| `demos/smart_home/src/kws_smoke/models/model.h` | 模型符号声明。 |
| `demos/smart_home/Kconfig` | `SMART_HOME_KWS_SMOKE` 及 stack/arena 配置。 |
| `demos/smart_home/CMakeLists.txt` | `kws_smoke` 的 C++ 源、模型和 `tflite_micro` 依赖。 |
| `board/esp32p4/esp32p4-function-ev-board/configs/smart_home/defconfig` | 当前 smoke 所需的 TFLM、FlatBuffers、C++ 和数学库配置。 |

模型的运行时契约为 input `int8[124, 40, 3]`（共 14880 个元素）、output `int8[5]`；
类别顺序为 `wake`、`hard_neg`、`other_speech`、`background`、`silence`。模型文件是
生成的 C++ 数组，修改或重新导入时须通过模型大小、schema、shape 和目标板输出一起复核，
不能只凭文件名替换。

## 3. 构建和验收入口

当前 defconfig 已启用 `CONFIG_SMART_HOME_KWS_SMOKE=y`。在完成项目既有的 P4 构建与
烧录流程后，在 NSH 中执行：

```text
nsh> kws_smoke
nsh> kws_smoke --warmup 10 --repeat 100
```

最低验收要求：

1. 无 schema mismatch、算子注册失败或 `AllocateTensors failed`；
2. 日志显示 input 为 14880 个 int8、output 为 5 个 int8；
3. `arena_used` 小于配置的 `arena_reserved`；
4. 100 次样本均完成并打印 P95；
5. 保存完整串口日志，记录固件 commit、频率/性能模式、`repeat` 和 arena 配置。

若 arena 不足，只调整 `CONFIG_SMART_HOME_KWS_SMOKE_ARENA_SIZE` 并重新测量；不要以
动态 heap 替代静态 arena，也不要据此改动 SmartHome 主任务的栈大小。

## 4. ESP-NN P4：当前状态与边界

当前 smoke 使用 TFLM reference 的 `AddConv2D()`、`AddDepthwiseConv2D()` 和
`AddMean()`；**尚未接入或启用 ESP-NN**。

`ccf_audioevent/third_party/esp-nn` 已带有 ESP32-P4 源文件，包括：

```text
include/esp_nn_esp32p4.h
src/convolution/esp_nn_conv_esp32p4.c
src/convolution/esp_nn_depthwise_conv_esp32p4.c
src/common/esp_nn_mean_s8_esp32p4.c
src/common/esp_nn_multiply_by_quantized_mult_esp32p4.S
```

但 OpenVela 当前的公共 TFLM ESP-NN 接入是 S3 专用，不能在 P4 SmartHome 配置中直接
打开。比赛仓库的近期目标是先在 `demos/smart_home/` 内部完成 P4 后端验证，不修改
`apps/mlearning/tflite-micro/`。

建议的后续目录为：

```text
demos/smart_home/
├── third_party/esp-nn/            # 固定来源、许可证、P4 必需源码
└── src/
    ├── kws_smoke/                 # 模型 smoke 和模型级验收
    └── tflm_esp_nn_p4/            # P4 Conv/DW/Mean 的 TFLM adapter
```

`third_party/esp-nn` 不应带入 ESP-IDF `test_app`、tests 或其原始构建逻辑；应保存
`LICENSE` 和 `UPSTREAM.md`，后者记录 ccf 来源、revision、导入清单和本地补丁。
`tflm_esp_nn_p4` 只包含 TFLM registration/wrapper：满足类型、量化、shape、padding、
stride、dilation、对齐等条件时调用 P4 ESP-NN；否则回退 TFLM reference kernel。

## 5. 推荐的提交和验证顺序

保持每一步可独立审查和回退：

1. `chore(smarthome): vendor P4 esp-nn sources`：仅导入必要 ESP-NN 源码、许可证、来源记录；默认不启用；
2. `feat(smarthome): add P4 esp-nn TFLM adapter`：添加 Conv2D 和 DepthwiseConv2D wrapper、私有构建开关和 fallback；
3. `test(smarthome): verify KWS ESP-NN outputs`：KWS smoke 切换 resolver，加入 reference/ESP-NN 输出比较与性能记录；
4. 仅在上述步骤稳定后评估 Mean、FullyConnected、Softmax 的收益；
5. P4 后端稳定且可复用后，再单独讨论抽取到 OpenVela `apps/mlearning/tflite-micro/` 的方案。

首轮只覆盖 Conv2D 和 DepthwiseConv2D。ccf 的模型特征 shape 与 `wake_large` 不同，
不能复制其 tensor ID、输出 mask、性能数据或白名单；必须依据本模型重新跟踪节点和验证。

## 6. 后续接手事项

- 在 P4 板上运行 reference smoke，建立耗时、arena 与串口输出基线；
- 确认导入 ESP-NN 的固定来源策略（复制快照、subtree 或后续公共源码仓库）；
- 完成 P4 wrapper 后，先以同一确定性输入逐字节比较 reference/ESP-NN 输出；
- 输出一致后，再记录 P95 和内存变化；没有可重复收益不得默认启用；
- 最后才接入 ES8311 PCM、`[124,40,3]` 前端特征生成、滑窗/去抖和串口识别结果。

ES8311 与 KWS 运行时的实现方案另见：

- `docs/开发计划/应用与AI/ESP32-P4X-SmartHome-wake_large唤醒词模型接入方案.md`
- `docs/开发计划/应用与AI/ESP32-P4X-SmartHome-KWS推理优化与P4后端接入方案.md`

## 7. 禁止事项

- 不要把 ESP-NN 的 S3 汇编或 `CONFIG_TFLITEMICRO_ESP_NN` 直接用于 P4；
- 不要将 ESP-NN 失败时静默视为加速成功；日志应显示实际选择的后端；
- 不要因 KWS arena 调参改动无关 UI、网络或 C6 配网配置；
- 不要将真实 Wi-Fi、模型 API key、录音或用户语料写入模型源码、文档或 Git；
- 未完成 reference/optimized 一致性验证前，不要把 ESP-NN 设为默认路径。
