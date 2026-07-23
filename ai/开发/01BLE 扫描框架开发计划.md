# 无硬件阶段：BLE 扫描框架开发计划

## Summary

将现有 CoreS3-SE 测试工程扩展为可编译的 NimBLE 扫描器：以主动扫描获取设备名称，按 `ICD*`/`ICM*` 过滤广播，输出名称、MAC、RSSI 与原始广播信息。保留最小 LCD/Touch 验证入口，但不实现在线/离线状态机、SD 存储或 MQTT。

## Key Changes

- 启用 ESP-IDF NimBLE 与 BLE-only 控制器配置；将 `bt` 加入 `main` 的构建依赖，并将项目最低 IDF 版本约束更新为 `>=5.5.5`。
- 将单文件测试代码拆分为：
  - `ble_scanner`：NimBLE 控制器/主机初始化、5 秒主动扫描、扫描完成后自动开始下一轮、错误恢复。
  - `device_filter`：解析广播与 Scan Response 中的名称，匹配 `ICD`、`ICM` 前缀；该模块不依赖 NimBLE，便于单独验证。
  - `app_ui`：保留 LCD 状态、触摸按钮和最近一台匹配设备信息。
- 定义稳定的扫描报告结构，包含设备名、BLE 地址与类型、RSSI、广播数据长度；扫描器仅向队列发布“已过滤的报告”，不直接更新 LVGL。
- 在应用任务中消费扫描状态与设备报告队列；通过 `bsp_display_lock()` 更新界面，避免 NimBLE 主机任务直接操作 LVGL。
- Touch 区域改为“开始/停止扫描”控制；LCD 显示 `BLE: Idle/Scanning/Error` 与最近一台 `ICD/ICM` 设备的名称、MAC、RSSI。
- 串口保留完整诊断日志：主机同步、扫描启动/结束、过滤命中、错误码。后续设备管理模块将直接订阅相同报告结构。

## Test Plan

- 无硬件：执行 `idf.py build`，确认 NimBLE、Wi‑Fi 共存配置和应用链接均成功。
- 为 `device_filter` 增加主机侧测试用例：匹配 `ICD001`、`ICM_A01`；拒绝空名称、相近前缀和无名称广播。
- 用构造的扫描报告验证 UI 消息路径：状态切换、最近设备字段更新、队列满时丢弃策略与日志。
- 有硬件后：触摸启停扫描；确认能看到目标设备名称/MAC/RSSI；确认连续扫描运行且 Wi‑Fi 启用时 BLE 不异常。

## Assumptions

- 采用主动扫描，以获得可能位于 Scan Response 中的设备名称。
- 扫描以连续的 5 秒会话循环运行；在线/离线的 60 秒判定留给下一阶段设备管理器。
- 首版只依据设备名称过滤 `ICD*` 与 `ICM*`，不解析厂商数据或连接设备。
- 无硬件期间不执行烧录、真实 BLE 验证或 RSSI 校准。
