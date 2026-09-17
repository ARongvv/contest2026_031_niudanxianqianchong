# ESP HAL 第三方源码补丁

`0001-gdma-use-heap-caps-free.patch` 修复 ESP32-P4 的 AHB-GDMA 在 NuttX
独立内核堆配置下的跨堆释放问题：`heap_caps_calloc()` 分配的 GDMA 对象必须
用 `heap_caps_free()` 释放，不能用 libc `free()`。

本机 `chips/esp32p4/esp-hal-3rdparty` 工作树已应用该补丁。对未修改的 HAL
工作树执行：

```sh
git apply --check ../../patches/esp-hal-3rdparty/0001-gdma-use-heap-caps-free.patch
git apply ../../patches/esp-hal-3rdparty/0001-gdma-use-heap-caps-free.patch
```

该 HAL 工作树由芯片构建流程管理，不在 contest 仓库中提交。
