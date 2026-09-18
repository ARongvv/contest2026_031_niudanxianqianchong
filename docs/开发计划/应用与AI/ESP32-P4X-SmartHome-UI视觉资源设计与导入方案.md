# ESP32-P4X SmartHome UI 视觉资源设计与导入方案

> 状态：方案为主，完整 MiSans 的 LVGL 部署路径已完成 P4X 真机验证；其余图标、插画和
> QuickApp 资源管线仍处于方案阶段。
>
> 目标：让 LVGL 主 UI 与 QuickApp 主 UI 共享同一套设计源资产，而由各自构建链生成可在
> ESP32-P4X 上运行的资源，保障 1024×600 面板的可读性和复杂交互页 30 FPS 目标。

关联文档：

- [智能家居中控面板 UI 设计方案](./ESP32-P4X-智能家居中控面板UI设计方案.md)：页面、卡片和性能目标。
- [快应用 UI 与原生服务分层方案](./ESP32-P4X-SmartHome快应用UI与原生服务分层方案.md)：两种互斥主 UI 模式、QuickApp RPK 与原生能力边界。
- [QuickApp 模拟器最小闭环验证方案](./ESP32-P4X-SmartHome-QuickApp模拟器最小闭环验证方案.md)：Goldfish 上的 RPK 部署和验证门。

## 1. 结论与范围

资源不应按“网页图片直接复制到开发板”的方式管理。应维护**一套有来源、许可和逻辑 ID 的
设计源资源**，再按 LVGL 或 QuickApp 目标导出。源文件可以保留 SVG、分层插画和高分辨率
PNG；真机包只能存放经过裁剪、压缩和预算审核的运行时产物。

本方案覆盖：

- 状态栏、导航、设备、房间、操作、告警和空状态图标；
- 场景卡插画、天气图标、设备/服务封面和少量启动图；
- 中文正文字体、数字字体与图标字体；
- LVGL 的内嵌字体、文件系统 PNG 与 TinyTTF 使用方式；
- QuickApp RPK 中的本地图片、字体和 `image` 组件使用方式；
- 资源预算、校验、更新和真机验收。

不包含：华为或其他第三方产品的图标、壁纸、截图、文案；网络图片下载；把摄像头 RGB565
帧作为 JS 图片资源传递。摄像头缩略图和实时预览属于 Vision Service/原生预览路径，见
关联的快应用分层方案。

## 2. 当前工程事实与约束

| 项目 | 当前情况 | 对资源方案的含义 |
| --- | --- | --- |
| 显示 | ESP32-P4X，1024×600，LVGL 软件渲染 | 一张全屏 RGB565 位图约 1,228,800 bytes（约 1.17 MiB），不能常驻多张。 |
| P4X `smart_home` 配置 | LittleFS MTD 起始偏移 `0x600000`、大小 10 MiB | 当前采用 6 MiB 固件 + 10 MiB 资源布局；完整字体可部署，但 `/data` 中的字体、图标、技能文件仍须共用预算。 |
| 当前产品图标 | `quickapp/smart_home_ui/prototype-web/assets/icons/` 有 61 个 PNG（240/256 px RGBA） | 该目录为唯一视觉源；构建时裁边、统一留白后生成 61 个 32 px、16 个 20 px、8 个 48 px 的内嵌 A8 图标。 |
| 现有 LVGL 字体 | 完整 MiSans 约 7.9 MiB/字重，subset 约 72 KiB/字重 | 完整字体已实机部署，运行时约占 7.58 MiB PSRAM；适合 Agent 动态中文。subset 仍适用于 PSRAM 紧张的受限配置。 |
| 当前 LVGL 配置 | POSIX 文件系统、`LV_USE_LODEPNG`、TinyTTF data API | PNG 继续从文件系统加载；完整 TTF 启动时预加载 PSRAM，并由 `lv_tiny_ttf_create_data()` 使用，不走 TinyTTF 文件流。 |
| QuickApp | 基于 QuickJS、UIKit、Yoga 和 LVGL；RPK 本质是 zip 包 | 图片应随 RPK 打包并由本地 `image` 组件引用；QuickApp 不是网络图片容器，仍要受同一存储和解码预算约束。 |

`CONFIG_SMART_HOME_DEMO_DATA_ROOT` 当前定义 LVGL 资源根，图标目录由
`CONFIG_SMART_HOME_DEMO_UI_LVGL_ICONS` 指定，默认路径为 `/data/res/icons`。LVGL POSIX
文件驱动的代码会将路径加上 `A:` 前缀后交给 `lv_image_set_src()`；该前缀是 LVGL 文件系统
驱动标识，不能原样用于普通 POSIX `access()` 调用。

## 3. 资源体系与首期清单

### 3.1 分层

| 层 | 典型资源 | 真机表现 | 首期建议 |
| --- | --- | --- | --- |
| S0 系统状态 | Wi-Fi、网络断开、麦克风、摄像头、勿扰、电量、时间旁提示 | 单色、20–24 px、常驻 | 14 个状态/隐私图标及其必要状态。 |
| S1 导航与操作 | 首页、设备、场景、安防、更多、返回、关闭、加减、播放 | 单色、20–28 px | 20 个；优先内嵌图标字体或符号。 |
| S2 设备与房间 | 灯、空调、窗帘、门锁、传感器、音响、客厅、卧室 | 单色或低饱和双色、32–48 px | 24 个设备图标、6 个房间图标。 |
| S3 语义与告警 | 在线、离线、执行中、成功、失败、有人、门窗、烟雾 | 小图标 + 颜色/文字，不能只依赖颜色 | 12 个，配合无障碍文案。 |
| S4 场景插画 | 回家、观影、睡眠、离家、阅读、会客 | 150×190 或 360×190 卡片级，不透明预合成背景 | 6–8 张；一张插画只服务一个卡片比例。 |
| S5 背景与品牌 | 启动画面、待机背景、Agent 光晕 | CSS/LVGL 渐变优先；只保留必要位图 | 启动图 1 张；待机背景最多 1 张，可先不引入。 |
| S6 动态内容 | 摄像头预览、AI 框、专辑封面、天气数据 | 原生预览或远端业务数据，不是静态资产 | 不计入资源包；须设缓存和生命周期规则。 |

首期不设计“每个设备一个彩色大插画”。设备身份由 S2 图标、名称、房间和状态共同表达；
场景氛围才使用 S4 插画。这样既避免资源膨胀，也让设备模型扩展时无需重新绘制大量图片。

### 3.2 图标规范

- 以 24×24 设计网格为基准；产品运行时使用 20 / 32 / 48 px 三档，不对 A8 图像做缩放。
- 图标名称使用小写 `snake_case`，例如 `status_microphone`、`device_air_conditioner`、
  `action_temperature_up`。逻辑 ID 不带分辨率和格式，文件名可追加 `_20`、`_48`。
- 单色图标输出为 LVGL A8 Alpha C 图片；UI 在运行时应用 `image_recolor` 主题色，
  不为亮/暗主题各存一张彩色图。ESP32-P4 的软件渲染器不支持 A4 绘制，不能使用 A4。
- 图标要有可读轮廓和最小 1.5 px 视觉笔画；“摄像头可用”和“摄像头正在预览”、
  “麦克风可用”和“正在录音”必须使用不同语义和文字状态，不能只替换颜色。
- 设计文件必须记录来源、作者、许可证和是否可商用；只接收自制、明确授权或许可证相容的素材。

### 3.3 插画与图片规范

场景插画在设计阶段可为 SVG/高分辨率 PNG，但真机导出时应针对目标卡片裁剪，不在运行时缩放
一张大图。首页卡片可采用下列目标尺寸：

| 用途 | 目标画布 | 运行时格式/策略 |
| --- | ---: | --- |
| 小场景卡 | 150×190 px | 预合成不透明 PNG；不可作为全屏背景复用。 |
| 横向音乐/场景卡 | 360×190 px | 预合成背景 + 极少量前景图标；隐藏时释放引用。 |
| 设备详情封面 | 320×180 px | 按需创建，离开详情页后不保留多个解码副本。 |
| 启动图 | 不超过 512×300 px | 一张、一次显示；优先纯色/渐变替代。 |
| 全天候背景 | 不建议首期使用 | 如必须使用，仅 1024×600 一张，预合成且不能与视频预览同屏。 |

透明图层会产生软件 alpha 混合成本。场景图尽量在导出阶段与卡片背景预合成；运行时只叠加
标题、状态和一个小图标。禁止实时 Blur、GIF 背景、滚动大图和同屏多层半透明 PNG。

## 4. 目录、命名和资源清单

建议新建一个**共享设计资源根**，不移动现有已验证的 `demos/smart_home/res/`；在资源管线稳定
后再由构建产物覆盖或同步该目录。

```text
ui-assets/                              # 建议新增：唯一设计源资源根
├── manifest/assets.json                 # 逻辑 ID、尺寸、许可、目标和预算
├── source/
│   ├── icons/                           # SVG：status/ action/ device/ room/
│   ├── illustrations/                   # 可编辑插画源文件
│   └── fonts/                           # 有许可证记录的字体源与 chars.txt
├── generated/
│   ├── lvgl/
│   │   ├── icons/                       # *_20.c 等内嵌 LVGL 字体
│   │   ├── res/icons/                   # 小型 PNG 文件资源
│   │   ├── res/scenes/                  # 场景 PNG 文件资源
│   │   └── res/fonts/                   # 子集字体
│   └── quickapp/
│       └── common/                      # 将进入 RPK 的 icons/ scenes/ fonts/
└── reports/                             # 每次导出的体积、尺寸、哈希报告
```

`generated/` 是可复现构建产物，不允许设计师手工修改；源文件修改后必须同步更新
`manifest/assets.json`。每条资源至少记录：`id`、类别、源文件、许可证、画布、导出目标、
文件大小上限、SHA-256、首期/可选状态和降级资源。例如：

```json
{
  "id": "device_air_conditioner",
  "category": "device_icon",
  "source": "source/icons/device/air_conditioner.svg",
  "license": "Team original",
  "outputs": {
    "lvgl_font": "generated/lvgl/icons/air_conditioner_24.c",
    "quickapp_png": "generated/quickapp/common/icons/device_air_conditioner_48.png"
  },
  "fallback": "device_generic",
  "max_bytes": 4096
}
```

## 5. 存储、内存与性能预算

当前 P4X `smart_home` 的 LittleFS 分区为 10 MiB；完整 MiSans 已占约 7.58 MiB。下表是当前
LVGL 路线的边界，不是允许把 10 MiB 写满的授权；每次实际构建必须以生成分区镜像和
`nuttx/.config` 为准。

| 内容 | LVGL 文件资源预算 | QuickApp RPK/共享数据预算 | 规则 |
| --- | ---: | ---: | --- |
| 中文字体（单一正体） | 完整 MiSans 约 7.58 MiB | 不与当前 LVGL 字体预算合并 | 为 Agent 动态中文预留；第二完整字重须另做 PSRAM/Flash 评审。 |
| 内嵌图标、场景/封面插画 | 图标 A8 像素数据约 87 KiB；插画使用完整字体后的剩余空间 | 按 RPK 独立核算 | 常用单色图标编译进固件；插画继续按卡片尺寸导出。 |
| 启动图 | 非必要，优先纯色与渐变 | 非必要 | 不与摄像头帧缓冲或多张大图同时常驻。 |
| 配置、技能、索引与余量 | 资源镜像总量应保持 ≤ 8 MiB | 与实际挂载拓扑共同核算 | 为 LittleFS 元数据、运行时资源更新保留约 2 MiB；每次生成镜像后复核。 |

QuickApp 的 RPK 可能安装/解压到 `/data/app/<package>`，而运行时 `QUICKAPP_RPK_DIR` 的默认值
是 `/resource/package`。P4X 最终采用哪个挂载点、RPK 是否与 `/data` 的 LittleFS 共用，尚未由
QuickApp P4X 构建实测确认。因此：

1. 在确定 P4X 的最终 `.config`、挂载表和资源镜像前，不承诺上述两栏预算可以简单相加；
2. 当前 QuickApp 尚未在 P4X 上验证，不得把 RPK 默认并入已部署完整 MiSans 的 LittleFS
   分区；如未来确认共用分区，须重新划分 Flash/PSRAM 预算并保留分区余量；
3. 若使用独立只读资源分区或扩容后的数据分区，仍须记录其镜像大小、flash offset、写入范围；
   不得沿用其他 ESP32 板卡的烧录地址。

内存方面，图片的压缩文件大小不等于显示时占用。1024×600 RGB565 图像约 1.17 MiB，摄像头
三缓冲与推理已会占用 PSRAM；首期约束为：页面同时持有不超过两张卡片级解码图，不缓存隐藏
页面的插画，不把视频帧、缩略图数组或 Base64 图片放进 QuickApp Store。

## 6. LVGL 模式：导入与使用方案

### 6.1 首选路径：内嵌 A8 图标图片

状态栏、导航、操作和设备图标使用同一批原始 PNG 生成 LVGL A8 C 图片。生成脚本为
`scripts/generate_smart_home_lvgl_icons.py`；输出在 `src/ui/lvgl/icons/generated/`，查询入口为
`smart_home_lvgl_png_icons.[ch]`。`Makefile` 使用通配符、`CMakeLists.txt` 使用 GLOB 自动收录所有生成 C 文件。

更新步骤：替换或新增原始 PNG，运行生成脚本，检查 `git diff --check`，再编译固件。页面通过
`smart_home_lvgl_icon_create(parent, "asset:camera", w, h)` 使用逻辑 ID；图标颜色由 A8 重着色样式提供。
该路径无需运行时 PNG 解码，适合常驻图标；代价是图标变更需要重新构建/烧录固件。

### 6.2 文件资源路径：PNG 图标和场景插画

适合主题插画、启动图或需要在资源分区更新的小图。现有工程已约定：

```text
demos/smart_home/res/icons/icon_device_light.png  # 源资源
                  ↓ 资源镜像/ROMFS 预置
/data/res/icons/icon_device_light.png             # 真机 POSIX 路径
                  ↓ LVGL FS POSIX（盘符 A:）
lv_image_set_src(image, "A:/data/res/icons/icon_device_light.png")
```

项目的 `smart_home_lvgl_icon_create()` 已封装上述文件检查、`A:` 前缀与对象销毁时的路径释放。
新增 PNG 图标时，使用逻辑名（如 `"icon_device_light"`）调用该函数，并在
`src/ui/lvgl/images/smart_home_icons.h` 增加相应 `ICON_...` 宏。场景插画应新增一个同风格的
`smart_home_lvgl_image_create()` 包装，而不是在每个页面手写路径、`strdup()` 和删除回调。

外部 PNG 的前提和规则：

- 保持 `CONFIG_LV_USE_FS_POSIX=y`、`CONFIG_LV_FS_POSIX_LETTER=65` 与 PNG 解码器可用；当前
  P4X LVGL 基线使用 `CONFIG_LV_USE_LODEPNG=y`。变更解码器前先在目标 defconfig 上实测。
- 先检查图片是否存在和大小是否在 manifest 限制内；缺失、解码失败或内存不足时显示
  `device_generic`/纯色卡片，页面不能崩溃。
- 图片的原始像素尺寸应接近显示区域；不得把现有 200×200 RGBA 图标缩放后作为 20 px 状态栏图标。
- 未验证资源分区更新流程前，PNG 只能随完整资源镜像预置；更新资源镜像时按 ESP32 工作流检查
  最终分区大小、offset 和与固件镜像的不重叠关系。

### 6.3 中文字体

P4X LVGL 当前将 `/data/res/fonts/MiSans-Normal.ttf` 的完整 MiSans（约 7.9 MiB）在启动时
一次性预加载到 PSRAM，并以 `lv_tiny_ttf_create_data()` 创建 12/14/16/20/32 px 字体实例。
该路径已完成实机验证，能够覆盖 Agent 的动态中文回复；不得退回 TinyTTF 从 LittleFS 文件流
随机读取完整字体的路径。详细日志见
[完整 MiSans PSRAM 预加载修复](../../开发日志/ESP32-P4X-SmartHome-完整MiSans-PSRAM预加载修复.md)。

这不是无成本方案：TTF 缓冲区会在 LVGL 生命周期内常驻 PSRAM，另有字形缓存和页面对象开销。
若摄像头、图片或多字重同时扩展导致 PSRAM 预算不足，应选择经字集验证的 subset 字体或预生成
LVGL 字库，并同步维护 `chars.txt` 和资源 manifest；不能在线下载字体。

首期应覆盖：界面固定文案、城市/天气、设备类型、常见设备名称、常见告警、数字、英文缩写与
必要标点。新增设备名/场景名进入产品文案前，先运行字集检查；缺字应显示可识别降级字符并在
测试报告中记录，不能在线下载字体。

## 7. QuickApp 模式：导入与使用方案

### 7.1 资源随 RPK 打包

QuickApp 的资源属于应用包，而不是 C 源代码中的数组。官方示例 RPK 已采用
`common/logo.png`、`common/nav.png` 等本地文件，并在 `manifest.json` 中以
`"icon": "/common/logo.png"` 引用。SmartHome QuickApp 建议的包内结构为：

```text
quickapp/smart_home_ui/
├── manifest.json
├── app.js
├── pages/
│   ├── home/index.ux
│   ├── devices/index.ux
│   └── security/index.ux
└── common/
    ├── icons/                       # 导出的 PNG 图标
    ├── scenes/                      # 导出的卡片插画
    ├── images/                      # 启动/空状态图片
    └── fonts/                       # 经验证后才放入的 subset 字体
```

`generated/quickapp/common/` 的内容在打包阶段复制到上述 `common/`，然后由 QuickApp IDE/
工具链生成 debug 或 release RPK。页面用本地绝对资源路径引用，示意如下（具体属性以最终所用
QuickApp SDK 版本为准）：

```html
<image src="/common/icons/device_air_conditioner_48.png"></image>
<image src="/common/scenes/scene_movie_150x190.png"></image>
```

图片路径必须是 manifest 记录的包内静态路径，不可由设备名、Agent 返回文本或网络 URL 直接
拼接。RPK 的 debug 包仅用于调试，交付和资源体积验收使用 release RPK。

### 7.2 部署和资源更新

在 Goldfish 验证阶段，可按 QuickApp 教程解压 RPK，推送到
`/data/app/<package>/`，再从模拟器串口使用 `vapp hap://app/<package>` 启动。该流程说明
RPK 包内资源可被页面读取，但不证明 P4X 的资源分区、字体和性能已达标。

P4X QuickApp 模式的资源更新应采用一次完整、可回滚的 RPK 发布：

1. 构建 release RPK，并生成文件列表、包大小、版本号、哈希和 manifest 资源报告；
2. 检查 RPK 安装目录/资源分区的实际余量和最终 flash 写入计划；
3. 安装新包后先校验 `manifest.json`、首页图标、中文字体和场景图，再切换为活动包；
4. 启动失败、缺图或版本不兼容时，回退到上一份已验证 RPK 或原生 LVGL 回退界面。

QuickApp 模式仍依赖 LVGL/UIKit 渲染，不能因为资源在 RPK 内就忽略 PNG 解码、alpha 混合、
JS 堆和文件系统开销。`QUICKAPP_RPK_DIR`、应用数据目录和 P4X 挂载点的对应关系必须在 P0
真机构建后记录，不能直接复用 Goldfish 的 `/data/app` 路径。

### 7.3 字体和摄像头边界

QuickApp 首期优先复用系统已经验证的 subset 中文字体；若 RPK 需要自带字体，字体文件同样
进入 RPK 预算，不得复制完整 MiSans。具体字体声明、字体回退与组件支持需在 Goldfish/P4X
上以当前 QuickApp SDK 实测后固化，不能只凭网页 CSS 假定可用。

`image` 组件只能显示包内静态图或受控的小型本地图片。摄像头卡首期只显示在线状态、事件和
预制占位/最后确认缩略图；实时画面使用同根 `CameraPreview` 原生 Widget，或进入独立 Native
Monitor 页面。QuickApp Store 只保存 `cameraId`、状态和事件元数据，绝不保存帧字节或 Base64。

## 8. 资源导出、审核与验收流程

```text
设计源（SVG/插画/字体）
        ↓ 许可、命名、尺寸、逻辑 ID 审核
assets.json + chars.txt
        ↓ 可复现导出
LVGL C 图标 / LVGL 文件资源     QuickApp common/ 资源
        ↓ 编译或 RPK 打包          ↓ release RPK 打包
固件/资源镜像大小检查             RPK/安装目录大小检查
        ↓                                ↓
P4X LVGL 真机视觉与 30 FPS        Goldfish → P4X QuickApp 验证
```

每次新增或替换资源必须通过以下门禁：

| 门禁 | 检查内容 | 通过条件 |
| --- | --- | --- |
| 合规 | 来源、许可证、作者和修改记录 | 不含未授权品牌素材、截图或网络抓取资源。 |
| 静态 | 命名、逻辑 ID、目标尺寸、透明通道、哈希 | 与 manifest 一致；没有重复或孤儿资源。 |
| 体积 | 单文件、资源目录、资源镜像/RPK | 未超过本方案预算，且保留分区余量。 |
| 显示 | 1024×600 深/浅主题、中文、离线降级 | 无拉伸、裁切、缺字或不可读的低对比度。 |
| 性能 | 首页切换、场景页滚动、图标批量创建 | 复杂交互页稳定 30 FPS；没有连续 PNG 解码和内存增长。 |
| 回归 | 资源缺失、解码失败、RPK 降级 | 显示默认图标/纯色占位；不阻塞设备控制和安防告警。 |

## 9. 分期实施

### R0：资源底座

建立 `ui-assets/manifest/assets.json`、`chars.txt`、命名表和设计许可证台账；将
`prototype-web/assets/icons/` 的 61 个单色 PNG 录入清单，并以 PNG 文件名作为稳定逻辑 ID。
LVGL 侧由 `generate_smart_home_lvgl_icons.py` 生成 32px 全量及按需 20px/48px 的 A8 C 图片；
不再将图标编码为字体字形。

### R1：首页最小资源包

完成状态栏图标、底部导航、灯/空调/窗帘/门锁/音响、天气、6 个场景插画和 subset 字体。
先在 LVGL 静态首页上测量资源镜像、启动耗时、首帧和 30 FPS；未通过前不增加壁纸和动态图。

### R2：双目标导出

将同一批逻辑 ID 分别导出为 LVGL 内嵌 C 图标/文件资源，以及 QuickApp `common/` 资源；
Goldfish RPK 验证所有静态路径、中文与缺图降级。LVGL 与 QuickApp 的视觉可有实现差异，
但图标语义、色彩 token、名称和状态定义必须相同。

### R3：P4X QuickApp 与发布

确认 P4X QuickApp 的真实 RPK 挂载位置、资源分区、字体和内存曲线；再决定资源是否能
独立更新。通过后才引入可选场景插画或启动图；摄像头仍按原生预览边界单独验收。

## 10. 待决项

1. P4X QuickApp RPK 在量产/比赛镜像中的实际存储路径、分区大小和升级机制。
2. 采用 TinyTTF subset 还是预生成 LVGL 中文字库作为 P4X LVGL 的首期字体实现。
3. 图标源的设计工具与许可证登记责任人；是否需要多语言文字和对应字集。
4. 场景插画采用自制矢量、授权插画库还是纯渐变/几何风格；首期不可使用外部产品视觉素材。
5. 是否在 R3 后为资源包建立签名、版本兼容和 A/B 回退策略。

## 维护记录

| 日期 | 变更 |
| --- | --- |
| 2026-09-14 | 初版：定义 SmartHome 图标、插画、字体与动态视觉资源清单；基于现有 LVGL 文件资源/内嵌字体路径和 QuickApp RPK 机制，给出双 UI 模式的导入、预算、更新和验收方案。 |
| 2026-09-18 | 将原型网页的 61 个单色 PNG 全量纳入 LVGL 图标库：全量生成 32px，状态栏/导航按需生成 20px，首页重点卡片按需生成 48px；ESP32-P4 软件渲染路径采用可见且可重着色的 A8 C 图片，并以生成结果替换主页、导航与设备页的旧图标引用。 |
