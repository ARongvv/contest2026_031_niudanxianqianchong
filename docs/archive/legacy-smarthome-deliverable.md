# 历史归档：SmartHome AI Agent — openvela 智能家居控制器

> **归档状态**：本文件记录早期 SmartHome / ESP32-S3 交付构想。相关板级目录和
> manifest 映射已从当前 P4 移植范围移除；文中的目录、构建、烧录和功能完成度均
> 不应作为当前项目状态或实施指引。

## 一、作品简介

SmartHome AI Agent 是一款运行在 openvela（NuttX 实时操作系统）上的 **AI 驱动智能家居控制器**。它将大语言模型（LLM）能力与嵌入式实时控制深度融合，用户通过自然语言对话即可控制灯光、空调等家居设备，并支持场景联动、定时器、天气感知等高级功能。

**核心亮点：**

- **AI Agent 架构**：基于自研 cAGENT 框架实现 ReAct（Reasoning + Acting）推理循环，LLM 自主决定何时调用工具、查询设备状态，实现真正的智能决策。
- **双模 UI**：提供 Console 命令行和 LVGL 图形界面两种交互方式。LVGL UI 包含设备面板、AI 对话、设置三大页面，支持 320×240 小屏设备。
- **多模型热切换**：运行时可切换 DeepSeek、Kimi、通义千问、OpenAI 等后端，无需重新编译。
- **跨平台**：支持 QEMU 模拟器（goldfish-arm64）和 ESP32-S3-BOX-3 真机，一次开发多端部署。
- **产品级 UI**：采用 MiSans 中文字体、25 个设计图标、Material 风格配色，支持触摸交互和动画切换。

---

## 二、选题方向

**AI 硬件产品创新赛道**

本作品将 LLM Agent 能力落地到嵌入式硬件（ESP32-S3-BOX-3），展示了 AI 如何在资源受限的微控制器上实现自然语言交互、自主推理和设备控制。区别于传统智能家居的按键/App 控制方式，本方案通过 AI 理解用户意图并自动编排多设备联动，体现了 AI+IoT 的产品创新方向。

---

## 三、系统架构

```
┌──────────────────────────────────────────────────────┐
│                    用户交互层                         │
│  ┌─────────────┐  ┌─────────────────────────────────┐│
│  │ Console UI  │  │         LVGL UI                 ││
│  │  (REPL)     │  │  Panel │ Chat │ Settings        ││
│  └──────┬──────┘  └────────┴────┬───────────────────┘│
│         │                       │  (pthread + async) │
├─────────┴───────────────────────┴────────────────────┤
│                   AI Agent 层                        │
│  ┌─────────────────────────────────────────────────┐ │
│  │              cAGENT Framework                   │ │
│  │  ┌──────────┐ ┌──────────┐ ┌────────────────┐  │ │
│  │  │ ReAct    │ │ Context  │ │ Event Callback │  │ │
│  │  │ Agent    │ │ Builder  │ │ System         │  │ │
│  │  │ Loop     │ │          │ │                │  │ │
│  │  └──────────┘ └──────────┘ └────────────────┘  │ │
│  └─────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────┤
│                  能力注册层                           │
│  ┌────────────┐ ┌─────────────┐ ┌─────────────────┐ │
│  │ 8 Tools    │ │ 5 Skills    │ │ Context Provider│ │
│  │ 设备控制   │ │ 领域知识    │ │ 设备状态注入    │ │
│  │ 天气查询   │ │ 安全策略    │ │ 环境传感器      │ │
│  │ 场景执行   │ │ 场景定义    │ │                 │ │
│  │ 定时器管理 │ │ 定时策略    │ │                 │ │
│  └────────────┘ └─────────────┘ └─────────────────┘ │
├──────────────────────────────────────────────────────┤
│                   设备抽象层                         │
│  ┌─────────────────────────────────────────────────┐ │
│  │ Virtual Device Table (8 devices)               │ │
│  │  • 灯光: on/off + 亮度(0-100%)                 │ │
│  │  • 空调: on/off + 模式 + 风速 + 温度(16-30°C) │ │
│  │  • 场景: sleep/movie/away/home                 │ │
│  │  • 环境: 温度 / 湿度 / 光照模拟               │ │
│  └─────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────┤
│                   基础设施层                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ │
│  │ WiFi     │ │ TLS/HTTP │ │ JSON解析 │ │ NuttX  │ │
│  │ WPA2连接 │ │ OpenAI   │ │ (内置)   │ │ RTOS   │ │
│  └──────────┘ └──────────┘ └──────────┘ └────────┘ │
└──────────────────────────────────────────────────────┘
```

---

## 四、目录结构

```
contest2026_031_niudanxianqianchong/
│
├── README.md                          # 本文档（作品说明）
├── docs/                              # 硬件资料
│   └── ESP32S3-BOX3-SCH.MD           # ESP32-S3-BOX-3 原理图说明
├── scripts/                           # 构建辅助脚本
│   ├── fix_box3_mbedtls_*.sh          # mbedTLS 兼容性修复
│   ├── make_box3_littlefs_data_image.sh  # LittleFS 数据镜像制作
│   └── subset_font.sh                 # 字体子集化
├── logs/                              # AI Coding 对话日志
│   └── ARongvv/manifest.json
│
├── demos/smart_home/                  # ★ 主作品：智能家居 AI Agent
│   ├── src/
│   │   ├── app/                       # 程序入口
│   │   │   └── smart_home_main.c
│   │   ├── agent/                     # AI Agent 封装
│   │   │   ├── smart_home_agent.c
│   │   │   └── smart_home_agent.h
│   │   ├── config/                    # 模型配置 & 后端预设
│   │   │   ├── smart_home_config.h
│   │   │   ├── smart_home_backends.c
│   │   │   └── smart_home_backends.h
│   │   ├── device/                    # 虚拟设备层
│   │   │   ├── smart_home_device.c
│   │   │   └── smart_home_device.h
│   │   ├── tools/                     # LLM 工具注册（8 个工具）
│   │   │   ├── smart_home_tools.c
│   │   │   └── smart_home_tools.h
│   │   ├── skills/                    # LLM 技能加载（5 个技能）
│   │   │   ├── smart_home_skills.c
│   │   │   ├── smart_home_skills.h
│   │   │   ├── smart_home_skill_loader.c
│   │   │   └── smart_home_skill_loader.h
│   │   ├── net/                       # WiFi 连接管理
│   │   │   ├── smart_home_wifi.c
│   │   │   └── smart_home_wifi.h
│   │   ├── voice/                     # 语音接口（预留）
│   │   │   └── smart_home_voice_stub.h
│   │   └── ui/
│   │       ├── smart_home_ui.c        # Console UI
│   │       ├── smart_home_ui.h
│   │       └── lvgl/                  # LVGL 图形 UI
│   │           ├── smart_home_lvgl.c         # LVGL 初始化 & 主循环
│   │           ├── smart_home_lvgl_panel.c   # 设备面板页面
│   │           ├── smart_home_lvgl_chat.c    # AI 对话页面
│   │           ├── smart_home_lvgl_settings.c# 设置页面
│   │           ├── smart_home_lvgl_nav.c     # 底部导航栏
│   │           ├── smart_home_lvgl_agent.c   # Agent 异步桥接
│   │           ├── smart_home_lvgl_style.c   # 样式系统
│   │           ├── smart_home_lvgl_style.h
│   │           ├── smart_home_lvgl.h
│   │           ├── smart_home_lvgl_internal.h
│   │           └── images/
│   │               └── smart_home_icons.h
│   ├── res/                           # 运行时资源
│   │   ├── fonts/                     # MiSans 中文字体
│   │   ├── icons/                     # 25 个 PNG 图标
│   │   └── skills/                    # 5 个技能 Markdown
│   ├── Kconfig                        # NuttX 构建配置
│   ├── Makefile / Make.defs           # 构建规则
│   └── CMakeLists.txt                 # CMake 构建规则
│
├── packages/cAGENT/                   # 自研 AI Agent 框架
│   └── src/
│       ├── core/                      # Agent 核心：ReAct 循环、上下文构建
│       ├── llm/                       # LLM 模型适配：OpenAI 兼容协议
│       ├── memory/                    # 会话与记忆管理
│       ├── skills/                    # 技能注册与注入
│       ├── tools/                     # 工具注册、JSON Schema、安全守卫
│       └── runtime/                   # 平台抽象（NuttX/ESP-IDF/STM32）
│
├── board/
│   ├── esp32s3-box-3/                 # ESP32-S3-BOX-3 BSP（主目标）
│   ├── esp32s3-box/                   # ESP32-S3-BOX 变体
│   └── goldfish-arm64/                # QEMU 模拟器配置
│
└── app/
    └── hello_app/                     # 最小示例应用
```

---

## 五、核心功能详述

### 5.1 AI Agent 交互

基于 cAGENT 框架实现 ReAct 推理循环：

1. 用户输入自然语言指令（如"我要睡觉了"）
2. Agent 构建上下文：系统提示词 + 设备状态 + 技能知识 + 对话历史
3. LLM 返回推理结果：直接回复 或 调用工具
4. 若调用工具 → 执行 → 将结果注入上下文 → 再次推理（最多 N 轮）
5. 最终返回自然语言回复

**支持的 8 个工具：**

| 工具名 | 功能 | 参数 |
|--------|------|------|
| `get_home_status` | 查询全部设备和环境状态 | 无 |
| `get_weather` | 查询模拟天气数据 | `location` |
| `set_light` | 控制灯光 | `room`, `on`, `brightness` |
| `set_ac` | 控制空调 | `room`, `on`, `mode`, `temperature`, `fan_speed` |
| `run_scene` | 执行预设场景 | `scene` (sleep/movie/away/home) |
| `set_timer` | 创建定时器/提醒 | `name`, `seconds` |
| `list_timers` | 列出活跃定时器 | 无 |
| `cancel_timer` | 取消定时器 | `id` |

**5 个领域技能（Markdown + YAML 前置元数据）：**

- **设备控制策略**：亮度默认值、空调模式/风速规则、环境感知规则（温度>28°C 建议制冷，光照<80lx 建议开灯）
- **安全规则**：必须通过工具操作设备、尊重参数边界（亮度 0-100，温度 16-30°C）
- **场景定义**：睡眠/观影/离家/回家四场景的设备预设
- **定时器策略**：定时器仅内存存储，重启后丢失
- **天气策略**：使用模拟天气数据，基于天气条件智能建议空调设置

### 5.2 LVGL 图形界面

针对 320×240 像素小屏优化的三页面设计：

**Panel 页面（设备面板）：**
- 2 列设备卡片网格，支持房间筛选（全部/客厅/卧室）
- 每张卡片显示：设备图标、名称、状态（开关/亮度%/空调模式温度）
- 点击卡片弹出控制面板：开关切换、亮度/温度滑块、空调模式/风速下拉选择
- 设备编辑：添加/删除设备，自定义名称、房间、类型
- 底部传感器栏：温度/湿度/光照/空调状态，点击可模拟环境数据

**Chat 页面（AI 对话）：**
- 聊天气泡布局：用户消息（右侧蓝色）、AI 回复（左侧白色）
- 工具调用追踪卡片：可展开查看每个工具的调用状态（运行中/成功/失败）
- 错误提示气泡（黄色左边框）
- 底部输入栏 + 屏幕键盘
- 状态提示："正在询问 cAGENT..." / "完成"

**Settings 页面（设置）：**
- 模型 API 配置：后端预设下拉（DeepSeek/Kimi/通义千问/OpenAI/自定义）
- 文本输入：主机地址、路径、端口、模型名称、API Key（密码模式）、超时时间、最大输出 Token
- 应用按钮：验证配置并热切换，无需重启

### 5.3 多模型后端支持

| 后端 | API 地址 | 默认模型 |
|------|----------|----------|
| DeepSeek | api.deepseek.com/v1 | deepseek-v4-flash |
| Kimi（月之暗面） | api.moonshot.cn/v1 | moonshot-v1-8k |
| 通义千问 | dashscope.aliyuncs.com/compatible-mode/v1 | qwen-plus |
| OpenAI | api.openai.com/v1 | gpt-4o-mini |
| 自定义 | 用户指定 | 用户指定 |

### 5.4 网络管理

- 自动 WiFi 连接（WPA2），支持重试（默认 3 次）
- 配置来源：`/data/wapi.conf` 文件 > Kconfig 编译时默认值
- DHCP 自动获取 IP + DNS 解析验证
- 无 WiFi 平台（如 QEMU 模拟器）自动降级使用现有网络

---

## 六、运行方式

### 6.1 环境准备

```bash
# 1. 拉取完整工程
repo init -u https://github.com/open-vela/contest2026_031_niudanxianqianchong \
  -b dev-ai-contest-2026 -m contest2026_031_niudanxianqianchong.xml
repo sync -c -j8

# 2. 进入工作区根目录（仓的上一级）
cd ..
```

### 6.2 QEMU 模拟器运行（开发调试）

```bash
# 编译 goldfish-arm64 模拟器版本（Console UI）
./build.sh contest2026_031_niudanxianqianchong/board/goldfish-arm64/configs/smart_home -j8

# 启动模拟器
./nuttx/tools/pynuttx/nuttx_sim_qemu.sh

# 在 NSH 中运行
nsh> smart_home "打开客厅灯"
nsh> smart_home                    # 进入交互模式
```

### 6.3 ESP32-S3-BOX-3 真机部署

```bash
# 编译 BOX-3 版本（LVGL UI）
./build.sh contest2026_031_niudanxianqianchong/board/esp32s3-box-3/configs/smart_home -j8

# 烧录到设备
# 方法一：esptool
esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
  write_flash 0x0 nuttx.bin

# 方法二：使用 board/esp32s3-box-3/scripts/ 下的烧录脚本

# 制作 LittleFS 数据镜像（含字体、图标、技能文件）
bash scripts/make_box3_littlefs_data_image.sh

# 烧录数据分区
esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
  write_flash <data-partition-offset> data.img
```

### 6.4 WiFi 配置

连接 WiFi 有两种方式：

**方式一：修改 defconfig**
```
# board/esp32s3-box-3/configs/smart_home/defconfig 中设置
CONFIG_SMART_HOME_WIFI_SSID="你的WiFi名称"
CONFIG_SMART_HOME_WIFI_PASSWORD="你的WiFi密码"
```

**方式二：运行时配置文件**
```bash
# 在设备上写入 /data/wapi.conf
echo '{"ssid":"你的WiFi名称","password":"你的WiFi密码"}' > /data/wapi.conf
```

### 6.5 API Key 配置

在 `demos/smart_home/src/config/smart_home_config.h` 中修改：

```c
#define SMART_HOME_DEMO_API_KEY  "sk-your-api-key-here"
```

或在 LVGL UI 的 Settings 页面中运行时修改。

---

## 七、AI Coding 使用说明

### 7.1 AI 辅助开发流程

本作品的开发全过程深度使用 AI 辅助编程，覆盖以下环节：

| 开发环节 | AI 辅助方式 | 产出 |
|----------|------------|------|
| **架构设计** | 与 AI 讨论嵌入式 Agent 架构方案，确定分层设计 | cAGENT 框架 + 分层模块设计 |
| **代码生成** | AI 生成各模块骨架代码，人工审查和调整 | 16 个 C 源文件 + 7 个 LVGL UI 文件 |
| **LVGL UI** | AI 生成 320×240 小屏布局代码、样式系统、动画逻辑 | 完整三页面图形界面 |
| **调试排错** | AI 分析编译错误、运行时崩溃，定位 mbedTLS/内存问题 | 修复脚本（scripts/ 目录） |
| **文档编写** | AI 辅助撰写 README、技能 Markdown、Kconfig 注释 | 完整项目文档 |

### 7.2 AI 带来的效率提升

- **原型速度**：从零到可运行的 AI Agent Demo 仅用数小时，AI 负责大量样板代码生成
- **UI 开发**：LVGL 图形界面代码量约 2000+ 行，AI 生成后人工微调，效率提升约 3-5 倍
- **跨平台适配**：AI 辅助处理 NuttX 特有的构建系统（Kconfig、Make.defs、manifest 链接），降低新手门槛
- **知识整合**：AI 快速查阅 NuttX/LVGL/cAGENT API 文档，减少查阅时间

完整对话日志见 `logs/` 目录。

---

## 八、技术栈

| 层次 | 技术 |
|------|------|
| 操作系统 | openvela (NuttX RTOS) |
| AI 框架 | cAGENT（自研，ReAct Agent） |
| LLM 接口 | OpenAI Compatible API (HTTP + TLS) |
| 默认模型 | DeepSeek V4 Flash |
| 图形界面 | LVGL 9.x |
| 字体渲染 | FreeType + MiSans |
| 网络协议 | WPA2 + DHCP + DNS + HTTPS |
| 硬件目标 | ESP32-S3-BOX-3 (240MHz, 8MB PSRAM, 320×240 LCD) |
| 模拟器 | QEMU goldfish-arm64 |

---

## 九、已知限制与后续计划

### 当前限制

- 虚拟设备层为内存模拟，不连接真实硬件外设
- 定时器为内存存储，设备重启后丢失
- 天气数据为模拟数据，未接入真实天气 API
- 语音功能为桩接口（Stub），未实现 ASR/TTS

### 后续计划

- 接入真实传感器（温湿度、光照）和执行器（继电器、红外）
- 集成语音识别（ASR）和语音合成（TTS），实现语音交互
- 支持 MQTT/CoAP 协议接入主流智能家居平台（HomeAssistant、涂鸦等）
- 增加更多设备类型（窗帘、扫地机、摄像头）
- 实现持久化存储（设备状态、定时器、用户偏好）
