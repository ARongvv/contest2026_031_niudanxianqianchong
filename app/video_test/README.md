# SC2336 连续取帧验收

问题演进和原始日志见 [2026-09-10 问题归档](../../docs/开发日志/应用/2026-09-10-SC2336-video_test问题与解决方案.md)。
最新真机验收连续交付 300 帧、无交付槽位丢弃，最终汇总为
`app_fps=30.02`、`sequence_gaps=0`、`acceptance=30fps-app`。

本地 NuttX V4L2 上层需要应用
`drivers/nuttx/patches/0001-v4l2-number-completed-frames.patch`（路径相对竞赛
仓库）。该补丁已应用到当前工作区；迁移到其他工作区时，在 openvela 根目录
使用 `git -C nuttx apply --unidiff-zero --check ../contest2026_031_niudanxianqianchong/drivers/nuttx/patches/0001-v4l2-number-completed-frames.patch`
检查后应用。它将序号赋值统一移到完成回调，修复首两帧都返回 sequence=0
的问题。不要通过放宽测试中的重复序号检查来绕过。

同目录的 `0002-imgdata-set-buf-return-error.patch` 修正 `IMGDATA_SET_BUF`
缺省返回值的整数/指针类型混用；使用 GCC 14 重新编译 V4L2 时也需要此补丁。

```text
video_test 300
video_test 10 --full
```

默认执行 100 帧吞吐测试，最多支持 1000 帧。每帧从首部到尾部均匀选取
64 个 64 字节窗口，共检查 4 KiB；完成检查立即 QBUF。逐帧元数据保存在
独立内存中，STREAMOFF、关闭设备后才打印，避免串口输出阻塞取帧。
sample_crc32 仅代表这些窗口，不能当作整帧 CRC。QBUF 后不再读取图像。

`--full` 检查完整 1228800 字节并计算整帧 CRC，用于内容诊断；它可能降低
应用帧率，不用于证明 30 fps。少于 30 帧也只报告内容验收。

吞吐模式用首末 DQBUF 的单调时钟差计算 `(frames-1)/elapsed`，排除启动
等待和结束后的日志时间。至少 30 帧、应用平均帧率在 28.5～31.5 fps
之间且 `sequence_gaps=0` 才报告 `acceptance=30fps-app`。建议运行 300
帧，结合驱动统计验收；平均帧率不保证每帧延迟均小于 33.3 ms。

## 丢帧位置的判读

NuttX `complete_capture()` 在完成回调内立即给 imgdata 设置下一块缓冲，
无需等待应用归还刚取出的缓冲。单个 `v4l2_buffer` 字段代表当前接收目标，
不表示整个 V4L2 队列仅有一块缓冲。测试使用 RING 模式；消费过慢会使
上层覆盖尚未 DQBUF 的帧，因此序号跳变不能直接证明芯片层无缓冲丢弃。

关闭设备时输出 `CSI delivery`：

- `delivered`：向 V4L2 发起完成回调的帧数。
- `no_buffer`：采集仍在运行，但没有交付目标或回调而丢弃的帧数。
- `requeue_errors`：DMA 缓冲归还失败次数。
- `copy_avg_us/copy_max_us`：整帧 memcpy 的平均/最大耗时，受系统时钟精度限制。

应用吞吐通过后还应确认 `no_buffer=0`、`requeue_errors=0`，并结合 CSI/GDMA
诊断判断硬件丢帧。V4L2 sequence 是上层交付序号，不涵盖交付前的硬件丢帧；
`delivered` 可略多于应用请求帧数，因为 STREAMOFF 前采集仍在继续。

若轻量模式仍达不到 30 fps，优先检查复制耗时、工作队列延迟及 DMA 完成
统计，再决定是否实现 DMA 直接写 V4L2 缓冲。仅增加队列深度不能解决
消费者长期慢于生产者的问题。
