# BLE 扫描与设备生命周期管理说明

本文说明当前固件中 BLE 扫描回调、事件队列和设备广播生命周期的实际处理方式，供现场调试和可靠性评审使用。

## 1. 持续扫描模型

NimBLE 主机同步完成后，`main/ble/ble_scanner.c` 的 `scanner_on_sync()` 调用 `scanner_start_continuous()`。扫描使用主动模式和 `BLE_HS_FOREVER`，在用户停止、NimBLE Host 复位或异常结束前持续运行。扫描间隔和窗口参数为 `0`，NimBLE 默认使用 30 ms 间隔和 30 ms 窗口，即连续监听。

当前 ESP-IDF 5.5.5 的 Observer-only 配置不会可靠地对有限扫描时长触发 `BLE_GAP_EVENT_DISC_COMPLETE`，因此正式版本不再以该事件作为正常扫描循环机制。若持续扫描意外结束，`scanner_gap_event()` 会记录警告，并在扫描开关仍启用时尝试恢复扫描。

控制器采用按地址重复过滤，缓存容量为 256、刷新周期为 1 秒。每次刷新后，持续广播的设备可以再次向 Host 上报，从而以约 1 秒的分辨率刷新 `last_seen_at`。

## 2. 广播回调处理

收到广播或主动扫描得到的 Scan Response 后，NimBLE Host 任务调用 `scanner_gap_event()` 的 `BLE_GAP_EVENT_DISC` 分支。回调依次执行：

1. `ble_hs_adv_parse_fields()` 解析广播字段；
2. `device_filter_copy_name()` 提取名称；
3. `device_filter_name_matches()` 判断是否符合不区分大小写的 `SM_ICM数字` 或 `SM_ICD数字`；
4. 复制 MAC、地址类型和 RSSI，生成 `ble_scan_report_t`；
5. 使用非阻塞 `xQueueSend(..., 0)` 投递扫描事件。

回调不执行 SD 卡、MQTT、Outbox 或 LVGL 操作。现场版本已移除逐包 `INFO` 日志和原始广播十六进制打印；扫描队列溢出时仅在首次及每累计 10 次丢弃时记录告警，避免串口输出反过来阻塞 NimBLE Host 任务。

## 3. 并发与队列行为

NimBLE GAP 回调由同一个 Host 任务串行调用，不会出现多个 `scanner_gap_event()` 同时执行。扫描回调与设备管理器之间通过位于 PSRAM 的扫描队列解耦。当前队列容量为 192 个事件；队列满时立即丢弃该报告并进行限流告警，不阻塞无线扫描。

设备管理器任务 `main/device/device_manager.c:device_manager_task()` 持续消费队列。它最多等待 1 秒是空队列时的等待上限，有事件到达会立即处理，不是固定每秒处理一次。

控制器按地址过滤同一秒内的重复报告；缓存每秒刷新一次。128 台设备的目标报告理论上约为 128 条/秒，仍需通过扫描队列丢弃日志和运行时内存日志完成现场验证。

## 4. 设备生命周期

`device_manager_task()` 收到报告后调用 `device_registry_process_report()`：

- MAC 不存在：新增设备，记录首包时间，产生 `BROADCAST_STARTED`；
- MAC 已存在且本轮仍在广播：更新 RSSI 和最后观测时间，不重复产生开始事件；
- MAC 已存在但上一轮已结束：重新开启一轮并产生新的 `BROADCAST_STARTED`。

设备表最多保存 128 个 MAC。`address_type` 只作为事件字段保存，不参与设备去重。

## 5. 广播结束判定

设备广播结束不依赖扫描轮次，而是由最后一次收到广播后的有效扫描时间累计决定。`device_observation_clock_t` 只在扫描状态为 `SCANNING` 时累计；扫描停止时冻结，避免停止扫描期间误判设备结束。

当：

```text
观察累计时间 - 该设备最后一次观测时间 >= bcast_end_s
```

`device_registry_mark_next_broadcast_ended()` 产生 `BROADCAST_ENDED`，并记录：广播开始时间、最后广播包时间、结束判定时间以及最后 RSSI。该事件分别复制到 UI、CSV 和 MQTT/Outbox 队列，慢速存储和网络操作不会阻塞 BLE 回调。

## 6. 现场关注点

现场应重点观察 `scanner event queue full; reports dropped=...` 告警、设备管理器的 capture/upload 丢弃计数以及 Publisher 的内存日志。丢弃计数持续增长时，应优先检查设备广播密度、扫描队列容量和设备管理任务负载；不应恢复逐包原始广播打印。
