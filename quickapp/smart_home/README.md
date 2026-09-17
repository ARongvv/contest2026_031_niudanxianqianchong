# SmartHome QuickApp（U1 / 完整 UI 骨架）

此目录是 ESP32-P4X SmartHome 的实际 QuickApp 工程，不是 `prototype-web/` 网页原型。

## 当前范围

页面已覆盖屏保、首页、空间设备、设备详情、场景、安防、更多与系统设置的完整面板骨架。
其中 P0.0 仅验证 `system.smarthome@1.0` 的最小真实闭环：页面读取原生服务提供的
`sim-living-room-light` 状态，并通过 `controlDevice(setPower)` 获得已确认的控制结果。页面不
保存设备状态副本作为真值，也不在 Feature 缺失时使用 JavaScript mock 数据。

P0.0 尚未声明状态订阅 API。当前原生 `DeviceService` 只有单监听者，必须先实现事件扇出，
才会在 P1 增加 `subscribeState()`；页面不会用定时轮询伪造实时更新。

其余设备、摄像头、场景、LLM、工具和 Skill 卡片均明确标为“待接入”。完整页面与数据边界
见 [QuickApp 完整 UI 迁移设计](../../docs/开发计划/应用与AI/ESP32-P4X-SmartHome-QuickApp完整UI迁移设计.md)。

接口不可用、设备未返回、版本跳变或命令失败时，页面必须明确显示失败状态；后续的
Native Feature 和模拟设备服务完成后，才能在 Goldfish 上进行端到端验证。

## 工程结构

```text
smart_home/
├── package.json
└── src/
    ├── app.ux
    ├── manifest.json
    └── pages/home/index.ux
```

`manifest.json` 声明 `system.smarthome`。打包使用 AIoT-IDE 或已安装的 `aiot-toolkit`：

```bash
npm install
npm run build
```

生成的 RPK 仅在包含 `system.smarthome` Feature 的 Goldfish 固件上部署。具体构建、推送和
串口启动步骤见
[QuickApp 模拟器最小闭环验证方案](../../docs/开发计划/应用与AI/ESP32-P4X-SmartHome-QuickApp模拟器最小闭环验证方案.md)。
