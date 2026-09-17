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
