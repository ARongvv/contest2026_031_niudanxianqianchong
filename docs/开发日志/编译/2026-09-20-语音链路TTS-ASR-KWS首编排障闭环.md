# 语音链路（TTS/ASR/KWS）首次构建排障闭环

排障日期：2026-09-19 ~ 2026-09-20。
适用对象：`configs/smart_home_voice`（三开：VOICE_TTS / VOICE_ASR / KWS）、
`demos/smart_home/src/voice/`、以及一切首次引入 C++ / TFLite Micro 依赖的配置。

## 背景与结论

语音链路五阶段合入后（`2390838`…`520c02b`），`smart_home_voice` 配置
首次构建，历经 **5 轮错误、11 个修复提交**，最终构建成功：
`nuttx.bin` 1.45 MB，六个 NSH 命令（smart_home / audio_smoke /
gt911_probe / tts_smoke / asr_smoke / kws_smoke）全部注册，
内存占用安全（irom 932 KB / 64 MB，sram 275 KB / 978 KB = 28%）。

核心教训一句话：**"从未被编译过的代码"不可信任——无论它来自别的
分支、别的 worktree，还是看起来正规的依赖配置集。**

## 排障时间线

### 第 1 轮：tflite-micro 找不到 `<new>`（Kconfig 静默丢弃）

```
flatbuffer_conversions.h:23:10: fatal error: new: No such file or directory
```

- **现象**：tflite-micro 自身的 C++ 源编译失败，`<new>`（C++ 标准库
  头）不存在。
- **定位**：检查实际生效的 `nuttx/.config`（而非 defconfig）：

  ```text
  CONFIG_TLS_NELEM=0
  CONFIG_LIBCXXNONE=y      ← defconfig 写的 LIBCXX=y 被悄悄改掉
  CONFIG_LIBMINIABI=y      ← 只有运行时符号，没有任何标准库头
  ```

- **根因**：`LIBCXX`（LLVM libc++）有隐藏依赖 `depends on
  TLS_NELEM > 0`（nuttx/libs/libxx/Kconfig），基线配置 TLS_NELEM=0。
  Kconfig 对依赖不满足的 `=y` 行为是**静默丢弃并回退 choice 默认值**
  （LIBCXXNONE），不报任何错。此依赖集抄自 kws 分支 defconfig——而
  kws 分支自己的文档写明"未完成编译"，依赖从未被验证过。
- **修复**（`78a7c85`）：defconfig 补 `CONFIG_TLS_NELEM=2`。
- **教训**：defconfig 写 `=y` ≠ 生效。新增依赖重的配置后，第一件事
  `grep` 实际 `.config` 确认符号真正打开。

### 第 2 轮：libcxx thread.cpp 的第二个 TLS 开关 + tflite 双层目录

```
thread.cpp:123: #error "Thread.cpp needs to enable CONFIG_TLS_TASK_NELEM..."
thread.cpp:136: 'task_tls_alloc' was not declared in this scope
kws_infer.cc:22: tensorflow/lite/micro/micro_interpreter.h: No such file or directory
```

- **根因 1**：本 fork 的 NuttX 有**两套** TLS 计数——`TLS_NELEM`
  （线程 TLS，LIBCXX 的 Kconfig 门控）与 `TLS_TASK_NELEM`（任务 TLS，
  `tls_task.h` 中 `task_tls_alloc/set/get` 三件套的开关）。
  libcxx thread.cpp 的 `__thread_local_data()` 依赖后者，只修第一个
  不够。
- **根因 2**：tflite-micro 实际源码在
  `apps/mlearning/tflite-micro/tflite-micro/`——repo 清单把项目挂载
  在双层同名目录（与 `mbedtls/mbedtls`、`libcxx/libcxx` 完全同模式），
  Makefile 的 `-I` 少一层。
- **修复**（`c139a7b`）：补 `CONFIG_TLS_TASK_NELEM=2`；
  `TFLM_ROOT := $(APPDIR)/mlearning/tflite-micro/tflite-micro`。
- **教训**：openvela 的 repo 清单大量使用"包目录/项目同名目录"双层
  嵌套。引用第三方包源码路径时，先看该包自己 Makefile 怎么定义
  （tflite 的 `TFLM_DIR := .../tflite-micro/$(TFLM_UNPACK)`）。

### 第 3 轮：三个纯代码错误（我自己的）

```
smart_home_kws_service.c:315: 'AGENT_OK' undeclared
kws_infer.cc:105: standard attributes in middle of decl-specifiers
kws_infer.cc:108: invalid conversion from 'void*' to 'uint8_t*'
```

- **根因 1**：kws_service.c 的配置校验用 `AGENT_OK`/`AGENT_ERROR_*`
  但未包含定义头（`cagent/types.h`；tts.c/asr.c 经 `cagent/runtime.h`
  间接获得，kws_service 不走网络只需要轻量类型头）。
- **根因 2**：`static alignas(8) uint8_t` 属性中置写法被 riscv g++
  拒绝；规范写法是属性前置 `alignas(8) static uint8_t`（与 model.h
  里已通过编译的写法一致）。
- **根因 3**：`MicroInterpreter` 构造第三参是 `uint8_t*`，C++ 不允许
  `void*` 隐式转换，需显式 `(uint8_t *)`。
- **修复**（`57ffd8d`）。

### 第 4 轮：结构体名对着别的 NuttX 版本写的

```
smart_home_voice_capture.c:78: field 'buffer_info' has incomplete type
```

- **根因**：本树 `nuttx/audio/audio.h` **根本没有** `audio_buf_info_s`
  这个结构；正确名称是 `ap_buffer_info_s`（audio_smoke 在用，字段
  同名同义）。TTS worktree 的播放器代码是对着别的 NuttX 版本写的、
  从未编译过——这是本次移植中第三处"worktree 代码对不上本树 API"。
- **修复**（`b699d96`）：两个文件替换结构名；顺带把 tts.c 的
  auth 头缓冲从 key+32 扩到 key+64，消除 `-Wformat-truncation`
  （Bearer 头最长 312 字节 > 原 288）。
- **教训**：移植从未编译过的代码时，所有对外部 API 的引用都要对照
  本树头文件核对，编译器是最快的核对工具。

### 第 5 轮：链接期 C/C++ 边界（nm 定位）

```
undefined reference to `smart_home_bulk_alloc(unsigned int)'   ← C++ mangled
undefined reference to `MicroPrintf(char const*, ...)'         ← C++ mangled
```

- **根因 1（extern "C" 缺失）**：`smart_home_memory.h` 没有
  `extern "C"` 守卫，kws_smoke_main.cc（C++）把 `smart_home_bulk_alloc`
  按 C++ 规则 mangle 成带参数类型的符号，而实现在 C 文件里是未
  mangle 的——两边对不上。
- **根因 2（编译宏不一致导致库符号被剥离）**：这是本轮最隐蔽的
  问题。TFLM 包以 `-DTF_LITE_STRIP_ERROR_STRINGS` 编译自己的源码
  （其 Makefile:64），该宏使 `micro_log.h` 把 `MicroPrintf` 变成
  no-op 宏、`micro_log.cc` 的实现被整体剥离——**库里根本没有这个
  符号**。而 smart_home 编译 kws_infer.cc 时没有这个宏，模板实例化
  出对真函数的调用（mangled `_Z11MicroPrintfPKcz`）。用工具链 `nm`
  实测两侧对象文件确认：一侧 `U _Z11MicroPrintfPKcz`，另一侧零符号。
- **修复**（`02d771a`）：smart_home_memory.h 补 `extern "C"` 守卫；
  Makefile KWS 块的 CFLAGS/CXXFLAGS 加
  `-DTF_LITE_STRIP_ERROR_STRINGS` 与 TFLM 包对齐。
- **附：陈旧对象强制清理**——Makefile 里的宏/路径变更**不会**触发
  make 重编（make 只看文件时间戳），修复后必须删除
  `demos/smart_home` 下全部 `*.o` + `Make.dep` 再构建，否则链接的
  还是旧对象（与 2026-09-19 陈旧对象蓝屏同一教训的构建期版本）。
- **教训**：链接自己不拥有的库时，编译宏必须与库的构建完全一致；
  `nm` 是验证"引用与定义是否真的能对上"的最直接工具
  （注意用 `prebuilts/gcc/linux-x86_64/` 的工具链，linux-aarch64
  的无法在本机执行）。

## 根因归类

| 类别 | 数量 | 案例 |
| --- | --- | --- |
| Kconfig 静默行为 | 2 | LIBCXX 依赖丢弃；两套 TLS 计数 |
| repo 双层目录嵌套 | 1 | tflite-micro/tflite-micro |
| 移植代码 API 对不上本树 | 1 | audio_buf_info_s → ap_buffer_info_s |
| 纯代码小错 | 3 | 头文件缺失、alignas 位置、void* 转换 |
| C/C++ 混编边界 | 2 | extern "C" 缺失；strip 宏不一致 |

## 静态扫雷方法论（哪些雷被提前排掉）

构建间隙做了一轮只读审计，以下风险被**静态排除**，未占用构建轮次：

1. ruy / gemmlowp / flatbuffers 三个依赖全是**纯头文件库**
   （Makefile 只下载源码并导出 -I，不编译任何 .cc）——RISC-V 平台
   特化编译风险不存在；
2. libcxx src 全部 `.cpp` 引用的 `CONFIG_*` 宏只有 2 个且都存在
   ——"宏名对不上"类陷阱做了系统扫描；
3. libcxx 的 5 个补丁文件齐备、源码已含修复痕迹；
4. TFLM 的 esp32s3 特化源码（esp_nn、CCOUNT）都有配置守卫；
5. 多程序 PROGNAME / MAINSRC / STACKSIZE 位置配对复核
   （C mains 在前、唯一 C++ main 排最后的巧合成立）。

最终第 3~5 轮的实际错误全部落在静态审计**覆盖不到**的三个位置
（本树 API 差异、C++ 语法细节、链接符号），符合预期：静态排雷
排除系统性风险，零星错误靠构建迭代收敛。

## 最终验证信号

```text
BUILD_EXIT=0
Generated: nuttx.bin                          # 1,448,432 B
strings nuttx.bin | grep -x tts_smoke ...     # 六命令全部注册
Memory region Used Size:
  irom_seg:   932336 B / 64 MB
  sram_seg:   275636 B / 978880 B (28.16%)
  drom_seg:  1439740 B / 64 MB
```

相关提交：`78a7c85`（TLS_NELEM）、`c139a7b`（TLS_TASK_NELEM + tflite
路径）、`57ffd8d`（三小修）、`b699d96`（结构名）、`02d771a`
（extern "C" + strip 宏）。真机验证清单见
`docs/开发计划/应用与AI/ESP32-P4X-SmartHome-语音链路TTS-ASR-KWS集成方案.md`。
