# ESP32-P4X Function EV Board ES8311 麦克风与扬声器适配方案

> 状态：方案设计，尚未实现或完成实板验收。  
> 适用范围：本仓库的 `esp32p4-function-ev-board` custom board；适用于采用
> ES8311、板载模拟麦克风和 NS4150B 功放的 ESP32-P4 Function EV Board / P4X
> Function EV Board 变体。开始实现前必须按实物版本原理图复核引脚和 codec
> 地址。

## 1. 目标与非目标

目标是在 openvela/NuttX 中打通板载音频全链路：

```text
板载麦克风
  -> ES8311 ADC -> I2S0 RX + DMA -> /dev/pcm_in0
  -> smart_home 采集线程 -> PCM16 单声道 -> ASR / KWS

TTS / 提示音 PCM
  -> /dev/pcm0 -> I2S0 TX + DMA -> ES8311 DAC
  -> NS4150B 功放 -> 板载扬声器接口
```

第一期只验收 16 kHz、16-bit、单声道 PCM 的稳定录音和播放；这既满足语音
识别输入，也缩小了 codec、时钟和 DMA 的排障面。

本方案不包含云端 ASR/TTS 协议、回声消除、波束成形、常驻唤醒词模型和音频
压缩格式。它们必须建立在 PCM 采集、播放和质量验收均通过之后。

## 2. 检索结论与当前状态

### 2.1 硬件事实

乐鑫官方资料说明该板具备 ES8311 单声道 ADC/DAC、板载麦克风、NS4150B
功放与扬声器接口；codec 的控制面经 I2C，实时数据面经 I2S。

- [ESP32-P4X Function EV Board 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)
- [Espressif BSP 的音频 API 与引脚定义](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_p4_function_ev_board/API.md)

官方 BSP 对应的固定连线如下。这里的 `DIN/DOUT` 是以 ESP32-P4 为参考方向：

| 信号 | GPIO | 方向 / 用途 |
| --- | ---: | --- |
| I2C0 SCL | 8 | 配置 ES8311，也与触摸控制器共享 |
| I2C0 SDA | 7 | 配置 ES8311，也与触摸控制器共享 |
| I2S0 BCLK / SCLK | 12 | P4 输出到 codec 的位时钟 |
| I2S0 MCLK | 13 | P4 输出到 codec 的主时钟 |
| I2S0 LRCK / WS | 10 | P4 输出到 codec 的帧时钟 |
| I2S0 DOUT | 9 | P4 输出，播放数据送入 codec DAC |
| I2S0 DIN | 11 | P4 输入，codec ADC 的麦克风数据 |
| PA enable | 53 | NS4150B 功放使能 |

ES8311 的 I2C 地址不得凭其他开发板的配置硬编码。实现前应以本板原理图或
官方 BSP 当前使用的 `ES8311_CODEC_DEFAULT_ADDR` 为准，并用 I2C probe/read
实际确认。

### 2.2 当前仓库的能力边界

| 层级 | 现有内容 | 结论 |
| --- | --- | --- |
| NuttX codec | `nuttx/drivers/audio/es8311.c`、`include/nuttx/audio/es8311.h` | 已有可复用 ES8311 lower-half，无需新写寄存器驱动。 |
| P4 I2S | `chips/esp32p4/common/espressif/esp_i2s.c` | 已有 I2S0 TX/RX 与 DMA 适配。 |
| P4 common board | `board/esp32p4/common/src/esp_board_i2s.c` | 仅注册通用 I2S audio lower-half。 |
| P4 Function EV Board | `board/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c` | 当前无 ES8311 初始化或 PA 控制。 |
| Smart Home | `demos/smart_home/src/voice/smart_home_voice_stub.h` | 只有语音接口桩，不采集/播放真实 PCM。 |

本 checkout 的主 `nuttx/` 树没有原生的 `boards/risc-v/esp32p4/` 目录；P4
芯片和板级移植通过竞赛项目的 custom chip/board 路径接入。因此 P4 的改动应
优先落在 `contest2026_031_niudanxianqianchong/chips/esp32p4/` 与
`contest2026_031_niudanxianqianchong/board/esp32p4/`，而不是假定存在主树 P4
board 目录。

### 2.3 不能复用的配置

`configs/i2schar/defconfig` 目前把 I2S0 的 `DINPIN` 和 `DOUTPIN` 都设为
GPIO10，并启用 `AUDIO_I2SCHAR`。这是通用 I2S 回环/字符设备测试配置，并不
匹配本板 ES8311 的引脚，也没有初始化 codec 或功放。

它只能证明 P4 I2S 总线驱动的基础路径，不能作为“板载麦克风、扬声器已经
可用”的证据；不得直接合并到 `configs/smart_home/defconfig`。

## 3. 目标软件架构

采用 NuttX Audio upper-half/lower-half 架构，而不是在 smart_home 中直接控制
I2S 寄存器：

```text
smart_home voice / audio smoke app
          | NuttX Audio API
          v
 /dev/pcm_in0             /dev/pcm0
     |                        |
     +---- ES8311 lower-half -+
                    |
             I2S0 + GDMA   I2C0
                    |        |
                 ES8311 codec
                    |
          MIC ADC / DAC -> NS4150B PA
```

ES8311 应被初始化为两个音频端点：

- 播放端点：注册 `pcm0`，由 NuttX 显示为 `/dev/pcm0`。如需 WAV/PCM decoder，
  沿用现有 board glue 中 `pcm_decode_initialize()` 的模式。
- 采集端点：注册 `pcm_in0`，由 NuttX 显示为 `/dev/pcm_in0`。

`nuttx/boards/xtensa/esp32s3/common/src/esp32s3_es8311.c` 已展示这种注册顺序，
可作为 P4 board glue 的 API 骨架；仅复用其 NuttX 初始化模式，不能照搬 S3
总线函数、GPIO、设备地址或板级常量。

## 4. 建议的板级改动

### 4.1 新增配置开关

在 `board/esp32p4/esp32p4-function-ev-board/Kconfig` 新增类似
`ESP32P4_FUNCTION_EV_BOARD_AUDIO_ES8311` 的 board 选项。它应：

- `select AUDIO`、`DRIVERS_AUDIO`、`AUDIO_ES8311`；
- `select ESPRESSIF_I2S0`、`ESPRESSIF_I2C0`、`ESPRESSIF_I2C0_MASTER_MODE`；
- 依赖或选择 I2S、I2C、work queue 所需的通用项；
- 不选择 `AUDIO_I2SCHAR`，该项只保留给独立总线诊断；
- 在 help 中明确注册 `/dev/pcm0` 和 `/dev/pcm_in0`，并说明它占用 I2S0 与
  I2C0。

I2S0 已会选择 I2S、DMA、GPIO IRQ 与 HPWORK；ES8311 的公共头还要求
`CONFIG_AUDIO`、`CONFIG_I2S`、`CONFIG_I2C` 与 work queue。因此配置项必须由
Kconfig 表达依赖关系，不能依靠开发者手工记忆。

### 4.2 新增 board glue

建议新增以下文件，避免把板型专属 codec 逻辑放入 P4 common 的泛化
`esp_board_i2s.c`：

```text
board/esp32p4/esp32p4-function-ev-board/
├── Kconfig                                  # 新增 board audio 开关
├── src/
│   ├── esp32p4_es8311.c                     # I2C/I2S 获取、codec 初始化、节点注册
│   ├── esp32p4-function-ev-board.h          # board_es8311_initialize() 原型/常量
│   ├── esp32p4_bringup.c                    # 调用专用初始化并记录错误
│   ├── Make.defs                            # Make 构建登记新源文件
│   └── CMakeLists.txt                       # CMake 构建登记新源文件
└── configs/smart_home/defconfig             # 固化经验证的音频配置
```

`board_es8311_initialize()` 的职责如下：

1. 只允许一次初始化，避免 board bring-up 和应用重复注册同名节点。
2. 获取 I2S0 和 I2C0 实例；任一失败即返回明确的负 errno。
3. 准备两个持久的 `struct es8311_lower_s`，填入实板确认的 I2C 地址和频率。
4. 分别初始化 ES8311 播放与采集 lower-half，并注册 `pcm0`、`pcm_in0`。
5. 按原理图初始化 GPIO53 的 PA 默认安全状态；开始播放前再打开功放，停止后
   适时关闭以避免底噪和无意义功耗。
6. 若任一步失败，释放本函数新取得的资源，且不要留下半注册的设备节点。

`esp32p4_bringup.c` 需要在 I2C/I2S 资源可用后调用该函数。启用 ES8311 board
glue 时应跳过通用 `board_i2s_init()`，或将其改为仅负责未被 codec 占用的 I2S
实例；否则同一 I2S0 会被重复初始化并可能重复注册 `pcm0`/`pcm_in0`。

## 5. 目标 Kconfig/defconfig 基线

以下为期望项，名称以当前仓库 Kconfig 为准；最终必须经 `menuconfig` 和
`savedefconfig` 生成，不能直接把这一段复制到 defconfig。

```ini
# Board codec glue（建议新增）
CONFIG_ESP32P4_FUNCTION_EV_BOARD_AUDIO_ES8311=y

# NuttX audio / codec
CONFIG_AUDIO=y
CONFIG_DRIVERS_AUDIO=y
CONFIG_AUDIO_ES8311=y
CONFIG_ES8311_SRC_MCLK=y

# Codec control: I2C0 shared with GT911
CONFIG_ESPRESSIF_I2C0=y
CONFIG_ESPRESSIF_I2C0_MASTER_MODE=y
CONFIG_ESPRESSIF_I2C0_SCLPIN=8
CONFIG_ESPRESSIF_I2C0_SDAPIN=7

# Codec data: I2S0 full duplex, P4 as master
CONFIG_ESPRESSIF_I2S0=y
CONFIG_ESPRESSIF_I2S0_ROLE_MASTER=y
CONFIG_ESPRESSIF_I2S0_RX=y
CONFIG_ESPRESSIF_I2S0_TX=y
CONFIG_ESPRESSIF_I2S0_DATA_BIT_WIDTH_16BIT=y
CONFIG_ESPRESSIF_I2S0_SAMPLE_RATE=16000
CONFIG_ESPRESSIF_I2S0_BCLKPIN=12
CONFIG_ESPRESSIF_I2S0_WSPIN=10
CONFIG_ESPRESSIF_I2S0_DINPIN=11
CONFIG_ESPRESSIF_I2S0_DOUTPIN=9
CONFIG_ESPRESSIF_I2S0_MCLK=y
CONFIG_ESPRESSIF_I2S0_MCLKPIN=13

# 不启用通用回环/字符设备测试
# CONFIG_AUDIO_I2SCHAR is not set
```

`16 kHz / mono / PCM16` 是麦克风链路的首个验收格式。扬声器若最终 TTS 只提供
其他采样率，应该在 codec 基线通过后按 Audio API 协商格式或增加重采样；不要在
初次 bring-up 同时引入重采样器。

## 6. 分阶段实施与验收

| 阶段 | 目标 | 操作和证据 | 通过标准 |
| --- | --- | --- | --- |
| A0 | 硬件复核 | 记录板版本、ES8311 地址、GPIO53 极性；示波器确认 MCLK/BCLK/WS | 原理图、BSP 和实物一致。 |
| A1 | I2C codec probe | 初始化 I2C0，读取 ES8311 可识别寄存器并保留串口日志 | 地址 ACK，读值稳定；GT911 不受影响。 |
| A2 | I2S 时钟/DMA | 开启 I2S0 master、RX/TX，检查时钟频率和无 DMA error 日志 | 16 kHz、16-bit 时序正确，连续运行无 underrun/overrun。 |
| A3 | NuttX 音频节点 | 启动后执行 `ls /dev`，运行最小 audio smoke app | 同时出现 `/dev/pcm0`、`/dev/pcm_in0`，可 reserve/configure/start/stop。 |
| A4 | 扬声器 | 播放固定 1 kHz、低幅度 PCM；逐步控制 PA | 声音连续、无异常爆音；停止后功放可关闭。 |
| A5 | 麦克风 | 录制 10 秒 PCM16 并导出；检查波形和频谱 | 人声可辨、无长静音/大量削波/周期性断裂。 |
| A6 | 稳定性 | 反复 start/stop、30 分钟采集和播放压力测试 | 不泄漏节点/内存，无 DMA 或 I2C 错误。 |
| A7 | Smart Home | PTT 录制、送 ASR、文本经 `lv_async_call` 回 UI | UI 不被采集线程阻塞，失败能恢复。 |

对 A5 导出的 PCM，使用仓库的 `pcm-audio` 工具检查削波、静音插入、爆音、底噪和
周期性失真。例如：

```sh
python .claude/skills/pcm-audio/scripts/audio_analyzer.py recording.pcm \
  --sample-rate 16000 --channels 1
```

建议把首个可复现样本、采集格式、ES8311 输入增益、固件提交和分析结果存入测试
记录。只看到设备节点或能读取到非零字节，均不足以证明音频质量合格。

## 7. Smart Home 接入边界

板级验收通过后，才在 `demos/smart_home` 中以真实模块替换
`src/voice/smart_home_voice_stub.h`：

- 采集线程负责 Audio API buffer 的入队、回收与 PCM 环形缓冲；不在 LVGL 线程做
  阻塞 I/O。
- PTT 第一期限制为 10–15 秒。16 kHz、单声道、PCM16 的带宽为约 32 KB/s，完整
  缓存 60 秒需要约 1.9 MB；应使用 PSRAM 或有界环形缓冲，不能放在任务栈。
- ASR 完成回调只用 `lv_async_call` 将结果投递到 LVGL 线程，不能直接修改控件。
- TTS 播放和录音先采用互斥模式，防止扬声器回采导致错误识别；全双工、AEC 与
  降噪是后续专项。

## 8. 主要风险与处理原则

| 风险 | 处理原则 |
| --- | --- |
| 板版本或 ES8311 地址不同 | A0/A1 前不写死地址；以实物原理图和 probe 为准。 |
| I2C0 与 GT911 共用 | 使用同一 NuttX I2C master 实例和正常锁/引用计数，不在应用层自行 bitbang。 |
| 通用 I2S 与 codec 同时初始化 | ES8311 接管 I2S0；避免重复 `board_i2s_init()` 与重复节点注册。 |
| PA 直接上电造成噪声/功耗 | GPIO53 保持安全默认状态，仅播放时使能。 |
| PCM 参数看似成功但模型无响应 | 固定 16 kHz、mono、PCM16，导出样本并检查 RMS、削波、静音、声道和字节序。 |
| 将应用问题误判为驱动问题 | 严格按 A0 至 A7 顺序验收；ASR、KWS 和 UI 均不作为 codec bring-up 的前置条件。 |

## 9. 完成定义

本方案完成时，应同时满足：

1. `smart_home` 专用 defconfig 能独立构建，且没有启用 I2S 回环测试作为替代；
2. 板级 ES8311 glue 在 Make 与 CMake 两条构建路径均已登记；
3. 实板稳定提供 `/dev/pcm0` 和 `/dev/pcm_in0`；
4. 16 kHz/16-bit/mono 的扬声器播放、麦克风录音和 PCM 质量报告均通过；
5. PTT 音频采集不会阻塞 LVGL，且 ASR 成功与失败路径均可恢复；
6. 文档、串口日志和可复现 PCM 样本能证明上述结果。

