# ESP32-P4X ES8311 Codec 适配与 GDMA 问题复盘

## 目标与范围

在 ESP32-P4 Function EV Board 上完成 ES8311 单声道 codec 的基础
bring-up：I2C0 控制、I2S0 全双工 DMA、播放节点
`/dev/audio/pcm0`、采集节点 `/dev/audio/pcm_in0`，以及独立的
`audio_smoke` 验收命令。

首轮范围仅覆盖 16 kHz、16-bit、单声道 PCM 的录放验证；不包含 Smart
Home 的 ASR/TTS、LVGL 语音交互或模型服务接入。

板级连接假设如下：

| 功能 | 资源 |
| --- | --- |
| ES8311 控制总线 | I2C0，SCL GPIO8，SDA GPIO7，地址 `0x18` |
| I2S0 时钟与数据 | BCLK GPIO12，MCLK GPIO13，WS GPIO10，DOUT GPIO9，DIN GPIO11 |
| 功放使能 | GPIO53，高电平使能 NS4150B |

## 实施内容

- 修正 ES8311 单字节寄存器读的 I2C 接收长度，避免以错误长度读入单字节
  `data`。
- 增加 Function EV Board 的 ES8311 glue：初始化 I2C0/I2S0，注册播放和
  录音节点；两个节点注册成功后才拉高 GPIO53。
- 配置 I2S0 master RX/TX、MCLK、16 kHz、PCM16，并保持 GT911 与 ES8311
  对 I2C0 的正常共享。
- 增加 `audio_smoke`：`play [seconds]` 输出低幅度 1 kHz 音调，
  `record [seconds] [path]` 写入原始 PCM 文件。

## 遇到的问题与处理

### 1. Kconfig 选择了 codec，但音频框架依赖未满足

首次在板级 Kconfig 中直接选择 `AUDIO_ES8311` 时，`olddefconfig` 报告
`AUDIO_ES8311` 的直接依赖 `AUDIO && DRIVERS_AUDIO` 为 `n`。这说明“选中
codec 驱动”不等于其所属的音频框架已经启用。

处理方式是补齐音频框架、I2S0 RX/TX 与对应板级选项的依赖，不再依赖对
choice symbol 的无效 `select`。之后再检查最终 `.config`，而不是仅检查
defconfig 中是否出现某一行。

### 2. 链接阶段找不到 `es8311_initialize`

日志：

```text
undefined reference to `es8311_initialize'
```

原因是 board glue 已调用 ES8311 lower-half 初始化入口，但 ES8311 驱动对象
未随音频框架进入 `libdrivers.a`。这仍是 Kconfig/构建集成问题，不是函数名或
板级 include 路径的问题。

处理方式是补齐 ES8311 驱动与音频框架配置，确认链接产物包含 codec 驱动后再
继续真机验证。

### 3. I2S0 的 RX GDMA 创建失败，错误码为 257

初始真机日志：

```text
ERROR: I2S0 GDMA RX create failed: 257
ERROR: I2S0 DMA setup failed: -12
Failed to initialize ES8311 audio: -19
```

`257` 是 ESP-IDF 的 `ESP_ERR_NO_MEM`，在 NuttX 侧被映射为 `-ENOMEM`
（`-12`）。但同时记录的内核堆仍有约 360 KB 最大连续块，因此不能简单判定
为“普通内存耗尽”。

问题出现的调用顺序为：

```text
ES8311 bring-up
  -> I2S0 初始化
     -> gdma_new_ahb_channel(TX)
     -> TX connect / callback
     -> gdma_new_ahb_channel(RX)   // 返回 257
```

此时启用 ES8311 会同时打开 I2S TX 和 RX；此前 P4 自有
`chips/esp32p4/common/espressif/esp_i2s.c` 将这两个方向拆成两次 GDMA
申请。第二次申请会进入已有 GDMA group/pair 的路径。当前 pinned HAL 的
实现会在该路径进行预分配和释放；而本配置启用了
`CONFIG_MM_KERNEL_HEAP=y`，`heap_caps_calloc()` 走内核堆，存在跨堆释放
兼容性风险。

失败前后内核堆的已用量从 5408 B 变为 5488 B，也说明失败清理没有恢复到
原始状态。随后 WLAN 注册、DSI framebuffer、GT911 和 MTD 分配都出现
`-12` 或初始化失败；这是 I2S 失败路径影响全局内存状态后的连锁现象，不应
误判成 Wi-Fi、触摸或显示引脚冲突。

### 4. 修复原则：不修改 `esp-hal-3rdparty`

`esp-hal-3rdparty` 是构建流程清理、checkout 和编译的第三方工作树。直接
修改其中的 `gdma.c` 即使暂时有效，也会在下次构建时丢失，且无法保证与上游
版本一致。

项目中曾短暂保存过 GDMA 源码补丁作为排查假设；该补丁资产已通过提交
`c38c949` 移除。最终修复不改 HAL 源码，而只改 P4 自有 I2S 驱动。

### 5. 最终修复：全双工一次申请同一 GDMA pair

在 `i2s_dma_setup()` 中，当 TX、RX 同时启用时改为：

```c
gdma_new_ahb_channel(&handle,
                     &priv->dma_channel_tx,
                     &priv->dma_channel_rx);
```

该 API 的语义就是将同时请求的 TX/RX 分配到同一个 GDMA pair。申请成功后，
驱动再分别执行 TX/RX 的 `gdma_connect()` 与回调注册；TX-only 和 RX-only
仍使用原单通道路径。

这个调整避免了第二次申请触发的已有 pair 路径，并与 HAL 的 GDMA 测试用法
一致。修复提交：

```text
8201995 fix(esp32p4): allocate duplex I2S GDMA pair atomically
```

### 6. 节点已注册，但 `audio_smoke play` 在配置阶段返回 `-ERANGE`

在 GDMA 问题解决后，设备已能看到两个音频节点，但执行：

```text
nsh> audio_smoke play 3
audio_smoke: play failed: -34 (Math result not representable)
```

`-34` 是 `-ERANGE`。`audio_smoke` 请求的是当前首版明确支持的
16 kHz、16-bit、单声道 PCM；错误发生在 `AUDIOIOC_CONFIGURE`，尚未进入
播放缓冲提交或 I2S DMA 传输。因此它既不是扬声器接线问题，也不能据此判定
GDMA 修复失效。

根因在通用 NuttX ES8311 lower-half 的输入和输出配置分支：函数先将 `ret`
设为 `-ERANGE`，用于表示格式校验失败；随后旧代码只在下层函数返回
`-ENOTTY` 时改写为成功，而下层函数正常返回 `OK` 时仍保留了旧的
`-ERANGE`。因此支持的采样率和位宽也会被误报为范围错误。

处理方式是把 `es8311_setsamplerate()` 和
`es8311_setbitspersample()` 的返回值直接赋给 `ret`，仅将 `-ENOTTY`
兼容为 `OK`；任何其他负值原样返回。该修复同时覆盖输入、输出路径，固化为：

```text
patches/nuttx/0003-es8311-propagate-configure-errors.patch
```

这样，若后续 MCLK/采样率组合不支持或 I2S 配置真实失败，NSH 将显示真实的
错误码，而不会再统一伪装成 `-ERANGE`。该补丁仅修改 NuttX 的通用 codec
逻辑，不涉及 `esp-hal-3rdparty`。

### 7. `audio_smoke play` 无输出卡在准备阶段

修复配置返回值后，真机执行 `audio_smoke play 1` 仍可能没有任何后续输出。
应用的“开始播放”提示在 `audio_smoke_prepare()` 成功之后才打印，因此仅从
命令回显后无输出可以确认：阻塞发生在打开设备、reserve、configure、获取
缓冲信息、创建/注册消息队列或分配音频缓冲的准备链路，而不是 DMA 完成回调
等待阶段。

为避免继续依据屏幕颜色或无输出猜测，新增临时 bring-up 日志：

- `audio_smoke` 用 `printf` 标记每个准备阶段及每个缓冲分配结果；
- ES8311 使用可见的 `syslog(LOG_INFO)` 标记输出模式复位、采样率/位宽的
  I2S 配置边界；
- 每一笔 ES8311 I2C 读写在进入总线调用前打印寄存器地址。若 I2C 调用阻塞，
  最后一行日志即为阻塞前进入的寄存器事务。

相关补丁为：

```text
patches/nuttx/0004-es8311-audio-smoke-diagnostics.patch
```

下一次验证只需重启后执行 `audio_smoke play 1`，保留从
`[audio_smoke] prepare` 开始到串口静止为止的全部输出。真机定位完成后，应将
逐笔 I2C `INFO` 日志删除或降级，避免长期占用串口带宽。

首轮启动日志已确认配置、缓冲和 I2C 时钟寄存器写入可以完成，但串口在
ES8311 start 的最后控制寄存器写附近断开，尚不能区分“该笔写未返回”、
worker 消息队列/线程创建失败或首个缓冲提交触发的复位。因此追加：

```text
patches/nuttx/0005-es8311-start-boundary-diagnostics.patch
```

该补丁记录 I2C 写返回值、codec 控制寄存器完成、worker 消息队列创建、
worker 创建，以及 `audio_smoke` 的 `AUDIOIOC_START` 和两个初始缓冲提交边界。

后续真机日志确认 `AUDIOIOC_START` 已返回，但在第一个应用缓冲提交前卡住。
因此增加 `0006-es8311-worker-boundary-diagnostics.patch`：记录 worker 是否进入、
首次 `es8311_processbegin()` 的返回值、首次消息队列等待，以及首个连续消息队列
接收错误。该错误日志限速为每轮连续错误的第一条，避免高优先级 worker 诊断时刷屏。

为避免逐笔 I2C 成功日志占满 UART，又保留“最后进入哪一笔事务”的阻塞证据，
`0007-es8311-reduce-i2c-success-log.patch` 将 `I2C write done` 降为 `DEBUG`，
而 I2C 写入开始和失败日志仍保持可见。共享 `smart_home/defconfig` 同时将
`CONFIG_ES8311_WORKER_STACKSIZE` 从默认 2048 提升到临时诊断值 4096；
`smart_home_local` 通过 include 继承此项，不单独写入其含凭据的 defconfig。

### 8. 启动后首个裸 PCM buffer 尚未进入 I2S，且 worker 错误路径不完整

启动边界日志已证明：ES8311 的控制寄存器初始化完成，worker 已创建、首次
`es8311_processbegin()` 在没有待处理 buffer 时返回 `OK`，随后正常阻塞在其
消息队列上。也就是说，问题不在 codec 启动、GDMA 初始化或 worker 创建；首个
应用 buffer 还未被确认提交到 I2S。

代码复核发现两个独立缺陷：

1. `pcm0` 注册为 NuttX 的 PCM decoder，但 `audio_smoke` 写入的是无 WAV
   header 的裸 PCM16。默认 decoder 会把第一个音频 buffer 当 WAV header
   解析，并拒绝它；共享 defconfig 现启用 `CONFIG_AUDIO_FORMAT_RAW=y`，使
   `pcm_decode` 直接把 smoke 的裸 PCM 交给 ES8311 lower-half。
2. `file_mq_receive()` 失败时返回负 errno；旧代码将 `int msglen` 与
   `sizeof(...)`（无符号）直接比较，负值会被转换为很大的无符号数，进而读取
   未初始化的 `msg.msg_id`。`0008` 先判断负值，再严格检查消息长度。

同一补丁还修复了 I2S 提交失败的所有权路径：驱动在提交前为避免 ISR race
先增加 `inflight`，但若 `I2S_SEND` / `I2S_RECEIVE` 立即失败，旧代码只跳出
循环，导致 buffer 已离开 `pendq`、`inflight` 未回退。后续 `STOP` 或 drain
会等待一个永远不会发生的 DMA completion。新路径会回退计数、释放
lower-half 引用、以 `AUDIO_CALLBACK_IOERR` 报告真实 errno，并以
`AUDIO_CALLBACK_DEQUEUE` 将 buffer 交还应用。

该修复保存为：

```text
patches/nuttx/0008-es8311-recover-from-submit-and-mq-errors.patch
```

另外修复 `audio_smoke` 中误写为 `\\n` 的用户提示，使其真正换行，避免串口
日志拼接干扰判断。

本轮真机日志进一步确认 `AUDIOIOC_START` 已成功返回；最后可见的是应用的
`[audio_smoke] start OK` 被截断为 `[audio_`，而 `enqueue initial buffer` 尚未
出现。因此首包没有进入 PCM decoder/I2S，不能把这次现象归因于 DMA 或 codec。
系统的 `CONFIG_SYSLOG_BUFFER` 未启用，ES8311 仍有大量同步 `INFO` bring-up
日志；`0009-es8311-demote-bringup-diagnostics.patch` 将所有成功路径边界日志
降为 `DEBUG`，保留错误日志及应用层阶段日志，避免串口写路径遮蔽首包提交。

再次测试后，ES8311 success-path 日志已经不再输出，但应用仍在同一位置截断。
这说明此前不能直接把问题归结为 USB Serial-JTAG 驱动；`audio_smoke` 自身的
准备、缓冲分配和启动边界 `printf` 仍在首包提交前输出十余行。它们只服务于
bring-up 诊断，却会改变串口发送队列和调度时序。现已将所有带
`[audio_smoke]` 前缀的阶段日志改为默认屏蔽的 `LOG_DEBUG`，只保留命令开始、
完成和错误结果。这样 `AUDIOIOC_START` 返回后会直接执行
`AUDIOIOC_ENQUEUEBUFFER`，下一轮结果才能有效判断 PCM decoder 与 I2S DMA。

### 9. `audio_smoke play 1` 仅打印开始提示后不返回

最新真机复现命令与现象：

```text
nsh> audio_smoke play 1
audio_smoke: play 1 s, 16 kHz mono PCM16
```

此后没有 `play complete`、错误码或 NSH 提示符。重启日志同时确认以下模块
在同一轮启动中正常工作：

- ES8311 已注册 `/dev/audio/pcm0`、`/dev/audio/pcm_in0`；
- I2S0 GDMA 初始化完成，且没有再次出现 `257` / `-ENOMEM`；
- WLAN、MIPI-DSI、GT911、LittleFS 均完成初始化。

因此当前问题的范围已经从“板级资源或 codec 初始化失败”收敛为播放命令的
运行期路径：`AUDIOIOC_START`、`AUDIOIOC_ENQUEUEBUFFER`、PCM decoder、
ES8311 worker、`I2S_SEND` 以及 DMA 完成回调中的某一环。仅凭“开始提示后无
输出”无法区分这些环节，也不能据此认定为 DMA 死锁。

已按顺序尝试的方案如下：

| 尝试 | 目的 | 结果与结论 |
| --- | --- | --- |
| 提升 `CONFIG_ES8311_WORKER_STACKSIZE` 至 4096 | 排除 worker 栈过小 | worker 可创建并进入首次消息队列等待；未消除播放卡住。 |
| 增加 codec、worker、应用层启动边界日志 | 区分配置、启动、首包提交 | 曾证明 codec 控制寄存器初始化与 worker 创建可以完成，但串口输出会与命令交错。 |
| 将逐笔 I2C 写成功日志降为 `DEBUG` | 降低 UART 压力 | 保留必要错误信息，但不足以消除卡住。 |
| 将全部 ES8311 成功路径诊断降为 `DEBUG` | 排除 codec bring-up 日志扰动 | 配置阶段已静默；命令仍只输出开始提示。 |
| 将 `audio_smoke` 阶段 `printf` 降为 `LOG_DEBUG` | 避免诊断输出改变串口队列和调度时序 | 已完成源码修改；下一轮真机验证将首次不依赖阶段串口日志。 |
| 启用 `CONFIG_AUDIO_FORMAT_RAW=y` | 使 `pcm0` 接受无 WAV header 的 PCM16 正弦样本 | 配置已写入共享 defconfig，待本轮运行期阻塞定位后验证首包能够直达 lower-half。 |
| 修复 ES8311 worker 的负消息长度判断与 I2S 立即失败回收 | 防止错误消息被当作有效消息，以及 buffer/in-flight 泄漏导致 drain 永久等待 | 修复已保存在 `0008`；它保障失败可见且可恢复，但尚不能证明本次正常播放路径是否走到 DMA。 |

当前不再继续增加普通串口日志。原因是 USB Serial-JTAG 控制台和 syslog 没有
启用 `CONFIG_SYSLOG_BUFFER`，且历史日志已经发生过与 NSH 输入交错；额外输出
会干扰待测的实时路径，降低结论可信度。

下一步采用非侵入式任务栈取证：将命令放到后台，使 NSH 保持可用，再采集
`audio_smoke` 与 ES8311 worker 的栈。

```sh
audio_smoke play 1 &
ps
# 记录 audio_smoke 与 es8311 的 PID，等待约 2 秒后：
dumpstack <audio_smoke_pid>
dumpstack <es8311_pid>
```

本配置已启用 `CONFIG_NSH_DISABLEBG=n`、`CONFIG_SCHED_BACKTRACE=y` 和
`CONFIG_SYSTEM_DUMPSTACK=y`，上述命令无需额外改配置。根据栈位置再实施最小
修复：

- 应用栈停在 `audio_start`：检查 ES8311 启动或 worker 创建；
- 停在 `audio_enqueuebuffer` / `pcm_enqueuebuffer`：检查裸 PCM decoder 与
  lower-half 消息投递；
- worker 停在 `i2s_send` / `i2s_txdma_setup`：检查 P4 I2S 提交与 GDMA；
- worker 停在 `file_mq_receive` 而应用停在 enqueue：检查消息队列、锁顺序或
  调度；
- 应用停在 `mq_receive` 而 worker 进入 I2S：检查 DMA completion、中断和
  HPWORK 回调。

## 真机结果

修复后启动日志显示：

```text
INFO: ES8311 ready: i2c=0 addr=0x18 i2s=0 \
      out=/dev/audio/pcm0 in=/dev/audio/pcm_in0
INFO: ESP-Hosted C6 WLAN registered: wlan0 ...
INFO: P4X MIPI-DSI framebuffer registered: /dev/fb0 ...
GT911 registered at /dev/input0: I2C0 address=0x5d ...
P4X LittleFS mounted: source=/dev/espflash mount=/data
INFO: ESP-Hosted C6 STA connected; wlan0 carrier on
```

结论：ES8311、Wi-Fi、显示、触摸和 LittleFS 已可在同一轮启动中完成初始化；
此前的 GDMA `257`、WLAN/DSI/存储连锁 `-12` 均未再出现。录放流的配置返回值
问题已经完成源码修复，仍待重新编译并在真机确认。

启动日志与 NSH 输入偶尔交错，可能出现 `nsh: INFO:: command not found`。
这是串口输出与正在输入的命令混合所致，等待日志稳定后再输入命令即可，并非
ES8311 驱动错误。

## 后续验收

在 NSH 中执行：

```sh
ls /dev/audio
audio_smoke play 3
audio_smoke record 10 /data/es8311_16k_mono.pcm
ls -l /data/es8311_16k_mono.pcm
```

验收标准：

1. `pcm0` 与 `pcm_in0` 均存在。
2. `audio_smoke play 3` 连续播放，无明显爆音或 DMA 中断错误。
3. 录音命令成功退出；10 秒、16 kHz、单声道、PCM16 文件约为
   `320000 B`。
4. 导出 PCM 后检查静音段、削波、底噪和周期性 DMA 失真。

若后续录放阶段才发生错误，应保留 I2S/GDMA 错误码、录音文件大小和复现步骤，
先区分“初始化/资源申请”与“数据传输/codec 时序”两个层面，再决定是否需要
升级上游 HAL 版本。
