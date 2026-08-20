# NuttX 显示驱动开发覆盖层

本目录保存竞赛项目维护、但最终将按 NuttX 通用驱动方式集成的源码。
竞赛开发期间，`scripts/link_nuttx_display_drivers.sh` 将这些文件以相对
软链接映射到工作区的 `nuttx/drivers/`，避免同一驱动出现两份可漂移源码。

当前约定如下：

- `drivers/lcd/ek79007.c`、`drivers/lcd/ek79007.h`：EK79007 MIPI-DSI
  面板控制驱动及其当前私有接口；
- `drivers/input/gt911.c`、`drivers/input/gt911.h`：GT911 I2C 触摸驱动
  及其当前私有接口；
- `../../scripts/link_nuttx_display_drivers.sh`：创建或检查上述四个文件
  到 NuttX 工作树的相对软链接。它不会修改 Kconfig、Make.defs 或
  CMakeLists.txt。

目前头文件与驱动源码同目录，仅供驱动和后续 P4X 板级 glue 使用。待接口稳定、
确有其他板卡复用需求后，再将最小公共 API 提升至 `nuttx/include/nuttx/`。

GT911 属于 Goodix GT9XX 系列；实现前应优先评估复用 NuttX 现有 `gt9xx`
通用驱动的可能性。若保留本目录的 `gt911.c`，它必须明确补足现有驱动无法
覆盖的复位、轮询或多点事件需求，不能无理由复制同一协议实现。

## 使用方式

在 openvela 工作区根目录执行：

```bash
# 创建链接；会仅清理本脚本上一版本创建的两个旧 EK79007 悬空链接
contest2026_031_niudanxianqianchong/scripts/link_nuttx_display_drivers.sh

# 检查链接是否完整、是否仍存在旧路径残留
contest2026_031_niudanxianqianchong/scripts/link_nuttx_display_drivers.sh --check
```

软链接用于竞赛期间的快速迭代。完成验证后，应将驱动及构建规则整理成
独立、可审查的 NuttX 补丁，再考虑上游提交。
