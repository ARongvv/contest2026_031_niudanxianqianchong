# ES8311 本地 NuttX 补丁

这些补丁针对本项目使用的 NuttX `drivers/audio/es8311.c`，按顺序应用：

1. `0001-es8311-single-byte-register-read.patch`：修复单字节寄存器读取的缓冲区越界。
2. `0002-es8311-mutex-and-log-formats.patch`：补充 `nuttx/mutex.h`，修复 `nxmutex_*` 未声明和链接失败，并使用 `PRIu32` 修复 MCLK、采样率的日志格式。

在尚未应用补丁的 NuttX 仓库中执行（本机工作区已经应用，不要重复执行）：

```sh
git apply --check ../contest2026_031_niudanxianqianchong/patches/nuttx/0001-es8311-single-byte-register-read.patch
git apply ../contest2026_031_niudanxianqianchong/patches/nuttx/0001-es8311-single-byte-register-read.patch
git apply --check ../contest2026_031_niudanxianqianchong/patches/nuttx/0002-es8311-mutex-and-log-formats.patch
git apply ../contest2026_031_niudanxianqianchong/patches/nuttx/0002-es8311-mutex-and-log-formats.patch
```

当前配置未启用 `CONFIG_LIBC_SEM_MUTEX_NOINLINE`，`nxmutex_init/lock/unlock` 是头文件中的内联实现。缺少该头文件时，编译器按隐式外部函数处理，随后链接失败；不需要新增互斥锁库或更改同步实现。

已在临时目录对基线文件顺序应用两个补丁，并与本机已修改源码逐字节比较一致。固件编译及板端录放音仍需验证。
