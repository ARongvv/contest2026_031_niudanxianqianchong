# ESP32-P4X Smart Home HTTP 发送阻塞修复

## 现象与根因

真机已完成 DNS、TCP 和 TLS 握手，HTTP 请求头为 227 B，请求体为
5312 B；日志停在 `phase=http write-body`，界面持续显示 thinking。
将 body 切成 1024 B 后仍可复现。

发送路径的 `esp_hosted_transport_send_packet_locked()` 原来对所有长度
都调用 CMD53 字节模式，而 `esp_hosted_transport_transfer_once()` 拒绝
超过 512 B 的字节模式事务，返回 `-EINVAL`。TLS 请求体产生的较大
以太网帧因此无法发出。此前接收方向已经做了块模式与尾部分段，发送
方向遗漏了这项处理。TX 配额 `available=20` 只证明查询到可用配额，
不证明后续 SDIO 写入成功。

同时，原配置没有启用 `NET_TCP_WRITE_BUFFERS`，本地 NuttX 的
`tcp_send_unbuffered.c` 在发送过程中等待 ACK，并使用 `SO_SNDTIMEO`
限制等待。仅在外层 TLS 循环设置 poll 超时，不能覆盖停留在底层
send 内部的等待。原来的非阻塞 fcntl 调用也没有检查返回值。

## 修复内容

- RX/TX 共用 FIFO 分段函数：整 512 B 部分使用块模式，尾部使用字节
  模式；每次事务最多 4096 B。地址始终为 FIFO_END 减去剩余长度，
  分段仍属于同一包。保留原有尾部 4 B 对齐/PIO 处理。
- 一整个 TX 包成功后才递增发送配额计数；失败保留 ERROR 日志中的
  包长度与错误码，不依赖编译时网络调试宏。
- 启用 `CONFIG_NET_TCP_WRITE_BUFFERS=y`，设置每个 socket 默认发送
  缓冲上限 `CONFIG_NET_SEND_BUFSIZE=8192`，让非阻塞发送使用已有的
  write-buffer/EAGAIN 路径。
- 检查 `F_GETFL`、`F_SETFL` 和 `SO_SNDTIMEO` 的设置结果。
- HTTP header/body 共用写入截止时间；每次写入前检查剩余预算并更新
  socket 发送超时。WANT_READ/WRITE 使用 poll，重试时保持指针和长度
  不变；部分成功后继续，零进展返回错误。
- 保留 1024 B body 分块和逐块进度日志。5312 B 应显示最终进度
  `5312/5312`，随后 `phase=http write-complete`。
- 保留此前 RX 配额 8、IOB 数量 96、链头数量 16 的配置，以及
  `CONFIG_SYSLOG_DEFAULT_MASK=0x7f`。

写入预算覆盖 HTTP header 和 body，不代表 DNS、连接、TLS 握手与
HTTP 响应读取合计仅有一个 30 秒预算。`write-complete` 表示数据已被
TLS/socket 接受；仍须收到 HTTP 响应才能证明请求成功。

FIFO 地址与分段依据：[Espressif SDIO Slave Protocol](https://espressif.github.io/idf-extra-components/latest/esp_serial_slave_link/sdio_slave_protocol.html)。

## 主机验证

从 openvela 根目录激活 `myenv`，在项目目录执行：

```bash
python scripts/test_hosted_rpc.py
python scripts/test_openvela_http_write.py
git diff --check
```

Hosted 测试直接编译生产发送函数，覆盖 1～1536 B 全部长度，检查
CMD53 写方向、模式、长度、地址、数据指针连续性，以及传输失败、
配额等待失败和发送计数。原有 RX/RPC 回归保留。

HTTP 测试提取生产写入和 poll 等待函数，用故障注入验证 HTTP
Content-Length/数据完整性、5312 B 分块、短写、WANT_READ/WRITE、
跨 header/body 的截止时间、socket 错误、零进展和时钟不可用。
这不替代真实 mbedTLS、NuttX TCP 和 SDIO 硬件的集成测试。

## 真机验收（待执行）

编译由开发者执行，从 openvela 根目录运行：

```bash
source myenv/bin/activate
./build.sh contest2026_031_niudanxianqianchong/board/esp32p4/esp32p4-function-ev-board/configs/smart_home_local -j2
```

编译后只检查相关配置，避免输出本地 Wi-Fi 密码：

```bash
rg '^(CONFIG_NET_TCP_WRITE_BUFFERS|CONFIG_NET_SEND_BUFSIZE|CONFIG_IOB_NBUFFERS|CONFIG_IOB_NCHAINS|CONFIG_SYSLOG_DEFAULT_MASK)=' nuttx/.config
```

预期值依次为 `y`、`8192`、`96`、`16`、`0x7f`。

烧录后发送 `hi`，验收标准：

1. TLS 握手成功，body 进度到 `5312/5312`（请求内容变化时长度也变化）。
2. 出现 `write-complete` 和实际 HTTP 状态。401/429 等属于后续 API
   鉴权或服务限制问题，不等于链路仍未发出请求。
3. HTTP 200 后模型回复显示在 UI，thinking 结束。
4. 连续多轮请求与较长输入不出现 TX `-EINVAL`、长期等待或持续丢包。
5. 测试期间断开网络，确认请求在对应阶段超时/失败后能返回 UI，
   并检查恢复联网后的请求。

若 SDIO 事务中途失败，不能仅凭发送计数未增加认定从机 FIFO 可以
安全原样重试；应保留完整错误日志，检查硬件链路和从机状态。
