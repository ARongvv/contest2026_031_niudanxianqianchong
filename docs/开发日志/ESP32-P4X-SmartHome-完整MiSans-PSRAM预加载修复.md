# ESP32-P4X SmartHome 完整 MiSans PSRAM 预加载修复

## 1. 结论

ESP32-P4X SmartHome 已在真机完成完整 `MiSans-Normal.ttf` 的加载和 LVGL
页面初始化验证。字体文件大小为 **7,943,504 B**，启动时从 LittleFS 的
`/data/res/fonts/MiSans-Normal.ttf` 一次性读取到 P4 用户堆；当前
`CONFIG_MM_KERNEL_HEAP=y` 与 `CONFIG_ESPRESSIF_SPIRAM_USER_HEAP=y` 使该用户堆
位于 PSRAM。LVGL 以 `lv_tiny_ttf_create_data()` 基于同一份内存创建
12/14/16/20/32 px 五个字体实例。

因此，静态页面、设备名称和 cAGENT 的任意中文回复不再受 UI 字体 subset
字集限制；完整字体不再通过 TinyTTF 文件流按字形随机读取。

## 2. 故障现象与定位过程

### 2.1 路径问题

最初 TinyTTF 以普通 POSIX 路径调用 `lv_tiny_ttf_create_file()`，LVGL 的
POSIX 文件系统会把无盘符的 `/data/...` 解释为 LVGL 驱动名，字体对象创建结果为
`0`，页面退回 Montserrat，中文显示为缺字方框。

修复为 LVGL 文件路径 `A:/data/res/fonts/MiSans-Normal.ttf` 后，五个对象可创建，
但使用完整字体会停在：

```text
[smart_home_lvgl] build screensaver begin
```

首个屏保中文标签触发字形定位与栅格化。通过 `pyftsubset` 制作只包含屏保文案的
小字体后，全部页面可完成构建，证明显示、触摸、页面结构和资源挂载链路正常，问题集中在
完整 TTF 的文件流访问方式。

### 2.2 根因

TinyTTF 文件模式在完整 CJK 字体中会产生大量细粒度的 seek/read。完整 MiSans 有约
2.9 万字形，CJK 字形的定位与轮廓读取并不是连续读取；LittleFS 文件流在首个中文字形
渲染时出现不可接受的阻塞。将 `CONFIG_LV_FS_POSIX_CACHE_SIZE` 增至 `4096` 后仍复现，
因此该缓存不是根治方案。

## 3. 实现

实现位于 `demos/smart_home/src/ui/lvgl/smart_home_lvgl_style.c`：

```text
LittleFS /data/res/fonts/MiSans-Normal.ttf
  → open + fstat
  → smart_home_bulk_alloc(7,943,504)
  → 循环 read，处理短读与 EINTR
  → PSRAM 字体缓冲区
  → lv_tiny_ttf_create_data(buffer, size, 12/14/16/20/32)
```

`smart_home_bulk_alloc()` 复用项目的统一大块内存接口；在当前 P4 配置中，普通用户堆
就是外部 PSRAM。五个字体实例只引用同一份只读 TTF 数据，不能在任何一个实例仍存活时释放
该缓冲区。退出顺序固定为：销毁五个 `lv_font_t` → 释放字体缓冲区。

同时移除 `CONFIG_LV_TINY_TTF_FILE_SUPPORT`。PNG 图标仍使用 LVGL POSIX 文件系统，
因此 `CONFIG_LV_USE_FS_POSIX=y` 和盘符 `A:` 仍然保留；变化仅限 TTF 不再使用其文件流接口。

文件不存在、`fstat` 失败、PSRAM 分配失败或读取不完整时，日志会给出失败原因，界面回退到
内置 Montserrat，保证不会因资源故障卡住启动流程。

## 4. 真机验证证据

本次真机启动日志如下（地址为本次启动的 PSRAM 分配结果）：

```text
[smart_home_lvgl] font preload begin path=/data/res/fonts/MiSans-Normal.ttf
[smart_home_lvgl] font preload done size=7943504 buffer=0x4831b348 region=PSRAM(user-heap)
[smart_home_lvgl] font instances source=PSRAM-data size=7943504 font12=0x48316b28 font14=0x4831b2e0 font16=0x4831b308 font20=0x48aaea80 font32=0x48aaeb48
[smart_home_lvgl] build screensaver begin
[smart_home_lvgl] build screensaver done
[smart_home_lvgl] build home begin
[smart_home_lvgl] build home done
[smart_home_lvgl] build panel begin
[smart_home_lvgl] build panel done
[smart_home_lvgl] build scenes begin
[smart_home_lvgl] build scenes done
[smart_home_lvgl] build security begin
[smart_home_lvgl] build security done
[smart_home_lvgl] build more begin
[smart_home_lvgl] build more done
[smart_home_lvgl] build chat begin
[smart_home_lvgl] build chat done
[smart_home_lvgl] build settings begin
[smart_home_lvgl] build settings done
[lvgl] ui init done
[lvgl] show done, entering run loop
```

该结果证明：

1. 完整字体确实从 LittleFS 读完，且缓冲区来自当前 P4 PSRAM 用户堆。
2. 五个目标字号均已创建，不存在字体对象创建失败后的静默回退。
3. 屏保、首页、设备、场景、安防、更多、Agent 对话和系统设置均完成构建，未再卡在第一个
   中文字形。
4. 问题修复的是字体加载与首帧初始化；网络 `renew wlan0` 的 DHCP 失败与该问题无关。

## 5. 存储与内存边界

当前 Flash 布局为 **6 MiB 固件 + 10 MiB LittleFS 资源**：

| 项目 | 值 |
| --- | ---: |
| 固件写入起点 | `0x2000` |
| LittleFS 偏移 | `0x600000` |
| LittleFS 大小 | `0xa00000`（10 MiB） |
| 完整 MiSans 文件 | 7,943,504 B（约 7.58 MiB） |
| 字体常驻 PSRAM | 至少 7,943,504 B，另加 TinyTTF 字形缓存和 LVGL 对象开销 |

完整字体能解决动态中文，但它不是“零成本资源”。后续加入摄像头帧缓冲、较大 PNG、网络响应缓冲
或更多字体字重前，必须在同一功能组合下采集 PSRAM 剩余量、最大连续块和页面切换后的回收情况。
不得再假定完整字体适合 1 MiB 资源分区，也不得在退出 LVGL 前释放其数据缓冲区。

## 6. 后续验收

1. 在 Agent 页面连续展示长中文回复、罕见汉字、中文标点和中英混排，确认没有缺字、卡顿或
   字形缓存泄漏。
2. 往返屏保、首页、设备详情、设置和 Agent 页面至少 20 次，观察触摸、页面切换与 PSRAM。
3. 在启用网络、音频和后续摄像头链路的组合固件上重新测量内存；视频预览不得与多个全屏图片
   或额外完整字重无约束地同时常驻。
4. 若产品最终的 PSRAM 预算不足，再评估按需字集、预生成 LVGL 字库或第二字体资源；不能退回
   完整 TTF 的 LittleFS 文件流路径。

## 7. 关联文件

| 文件 | 作用 |
| --- | --- |
| `demos/smart_home/src/ui/lvgl/smart_home_lvgl_style.c` | 字体预加载、字体实例创建与释放。 |
| `demos/smart_home/src/smart_home_memory.[ch]` | SmartHome 大块内存接口。 |
| `board/esp32p4/esp32p4-function-ev-board/configs/smart_home/defconfig` | PSRAM 用户堆、TinyTTF、10 MiB LittleFS 配置。 |
| `scripts/make_p4x_littlefs_data_image.sh` | 默认将完整 MiSans 打包到资源镜像。 |
