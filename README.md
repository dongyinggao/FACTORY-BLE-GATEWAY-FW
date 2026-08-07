# 厂内产品状态监控蓝牙网关

本项目是基于 ESP32-S3 M5Stack CoreS3-SE 的 BLE 网关固件，用于采集
`SM_ICM数字` / `SM_ICD数字` 设备的广播生命周期，并通过 SD 卡和 MQTT
保存、上传数据。

当前固件已经实现：

- NimBLE 主动扫描，按设备名称过滤有效广播；
- 以设备 MAC 地址识别设备，记录广播开始、最后一次广播和结束判定时间；
- 128 台设备容量管理；
- SD 卡 CSV 持久化和断网 Outbox；
- Wi-Fi STA、SNTP 校时、MQTT QoS 1 上报与 PUBACK 确认；
- CoreS3-SE LCD 状态显示、触摸启停扫描与两步 OTA 操作入口；
- USB Serial/JTAG `esp_console` 配置并保存到 NVS。
- 受控 HTTPS OTA：Manifest 检查、SHA-256 校验、双分区切换与启动自检回滚。

## 开发环境

| 项目 | 要求 |
| --- | --- |
| 硬件 | M5Stack CoreS3-SE（ESP32-S3，8 MB PSRAM，16 MB Flash） |
| ESP-IDF | v5.5.5 |
| Python | 使用 ESP-IDF v5.5.5 的虚拟环境 |
| BSP | `espressif/m5stack_core_s3` v4.0.0 |

Linux 环境安装 ESP-IDF 可参考[官方指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/linux-setup.html)。

## 获取代码与构建

```bash
git clone <repository-url>
cd BLEGateway
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
idf.py build
```

依赖已锁定在 `main/idf_component.yml` 和 `dependencies.lock` 中；无需重复执行
`idf.py add-dependency`。

## 构建、烧录与监视

```bash
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Windows 请将端口替换为实际的 `COM` 端口，例如 `COM5`。USB Serial/JTAG
控制台的波特率为 115200。Linux 用户若遇到串口权限错误，请将当前用户加入
`dialout` 组后重新登录。

## USB 配置命令

设备启动后可在 `esp>` 提示符下配置参数。密码不会在 `cfg show` 中显示。

```text
cfg set gateway_id GW-01
cfg set gateway_loc Room101-North
cfg set bcast_end_s 5
cfg set wifi_ssid Factory-IoT
cfg set wifi_password <password>
cfg set mqtt_uri mqtt://192.168.19.21:1883
cfg set mqtt_qos 1
cfg set ntp_server ntp.aliyun.com
cfg set ota_manifest_uri https://ble-gateway-uat.singularmedical.net/ota/manifest.json
cfg commit
cfg show
```

对全新设备，推荐在未打开串口监视器的情况下使用初始化脚本；脚本不会把 Wi-Fi 密码写入仓库，
执行时会隐藏输入：

```bash
./tools/provision_gateway.sh /dev/ttyACM0
```

默认值为本项目 UAT 网关 `GW-01`、`Room101-North`、5 秒广播结束判定、
`singularmedical-guest` 与 `mqtt://192.168.19.21:1883`。产线脚本可通过环境变量覆盖，例如
`GATEWAY_ID=GW-02 GATEWAY_LOCATION=Room102 ./tools/provision_gateway.sh /dev/ttyACM0`。
完成后用 `cfg show` 复核（密码会隐藏）。

`GW-01` 和 `Room101-North` 仅是 UAT 示例。实际安装时必须用唯一的网关 ID 和准确安装位置覆盖；
当前 UAT 使用明文 `mqtt://` 且无账号密码，正式环境应改为具有独立账号、CA 证书校验的
`mqtts://<broker-domain>:8883`。脚本支持通过 `MQTT_USERNAME`、`MQTT_PASSWORD` 写入认证信息；
未提供它们时不会改动当前 NVS 中已有的 MQTT 认证字段。

`bcast_end_s` 范围为 5～300 秒，默认 5 秒；它表示最后一个有效广播包后，
网关等待多久才生成 `BROADCAST_ENDED`。MQTT 主题为：

```text
factory/product-status/gateway/<gateway_id>/events
```

对持续超过 60 秒的同一轮广播，网关额外发送低频 `BROADCAST_ACTIVE`（每 MAC 最多每分钟一条），
用于服务端确认设备仍在广播并识别长时间异常。该消息不写 CSV、不进入 SD Outbox；
`BROADCAST_STARTED` 与 `BROADCAST_ENDED` 仍是本地持久化和断网重传的可靠记录。

全新设备、整片 Flash 擦除、首次 NVS 配置和 `1.0.1 → 1.0.2` OTA 验证的逐步操作见
[OTA 发布制品暂存与 UAT 发布](ota/README.md)。普通 `idf.py build` 的固件版本由根目录
[`version.txt`](version.txt) 定义，当前基线为 `1.0.1`；正式发布脚本会显式覆盖该版本。

## HTTPS OTA 维护

OTA 可由串口维护命令，或由网关专属 MQTT 指令触发。升级前必须满足 Wi-Fi 已连接、SNTP
已同步、SD 卡可写且没有正在进行的设备广播；网关会先停止扫描并等待现有采集/上传队列排空。
网关为外部供电设备，Wi-Fi 在初始化后始终使用 `WIFI_PS_NONE`，优先保障 BLE 采集和
HTTPS/MQTT 传输的稳定性，不以省电为目标。

```text
ota check   # 下载并校验 Manifest，不写入固件
ota status  # 查看当前状态、可用版本和错误码
ota start   # 重新检查 Manifest，满足采集保护条件后下载并重启
ota start --allow-downgrade  # 仅物理串口受控回退，仍执行完整镜像校验
```

LCD 底部的 `Check update` 提供同一维护入口：首次触摸只检查 Manifest；成功后按钮变为
`Start update`，再次触摸才允许进入升级流程。下载、校验和重启期间按钮会禁用。串口命令仍
用于查看详细错误和执行受控回退。

`release_sequence` 会保存在 NVS，手动 USB 烧录旧固件不会降低已确认序号。LCD 只显示当前
运行版本 `FW:<版本>`；OTA 确认序号、可用版本和错误原因以 `ota status` 为准。手动重刷旧版本
后，可通过 `ota start --allow-downgrade` 恢复到已发布镜像，或在确有必要时整片擦除后重新配置。

Manifest 必须通过 HTTPS 提供，并包含 `version`、`image_url`、`image_size` 与镜像
`sha256`，以及硬件型号、芯片目标、分区布局和递增发布序号。镜像下载到非运行 OTA 分区，
校验失败不会切换启动分区；首次启动新镜像后，扫描和 SD 存储服务连续运行 10 秒才确认新
镜像有效，否则由 Bootloader 自动回滚。详细的发布格式、测试步骤与限制见
[OTA 升级与发布操作说明](doc/OTA升级与发布操作说明.md)。
服务器 MQTT 与 OTA 服务的部署、验证和运维命令见
[服务器 MQTT 与 OTA 环境部署操作手册](doc/服务器MQTT与OTA环境部署操作手册.md)。

### MQTT OTA 命令（第一阶段）

网关只订阅自身的命令 Topic，避免广播式指令误升级其他设备：

```text
factory/product-status/gateway/<gateway_id>/commands/ota
```

命令必须使用 `mqtts://` 连接，并携带 HTTPS Manifest、唯一命令 ID 与 Unix 到期时间：

```json
{"message_type":"ota_command","command_id":"cmd-20260803-001","campaign_id":"pilot-01","manifest_url":"https://ble-gateway-uat.singularmedical.net/ota/manifest.json","expires_at_epoch_s":1780000000}
```

网关会以 QoS 1 发布 `accepted`、`waiting_safe_window`、`downloading`、`rebooting` 或
`failed` 等状态到：

```text
factory/product-status/gateway/<gateway_id>/ota/status
```

命令 ID 会保存到 NVS，用于抑制重复投递；同一条远程命令不能允许降级。该阶段没有批量活动
管理、签名验证或持久化状态回传，量产前仍需补齐服务端鉴权与 Manifest 签名。

## 运行诊断命令

诊断命令不会修改配置或影响扫描，可在 `esp>` 提示符下按需执行：

```text
sys status  # BLE、设备/队列、SD、网络、Outbox 状态
sys mem     # 内部内存、DMA 内存与 PSRAM 的余量和最大片段
sysmem      # `sys mem` 的快捷别名
sys tasks   # 可选；输出任务快照、累计 CPU 占比与历史最小剩余栈
```

要启用 `sys tasks`，在 `idf.py menuconfig` 中启用
`Component config -> System diagnostics -> Enable task and CPU diagnostics`；该选项会自动开启
所需的 FreeRTOS Trace Facility 与运行时间统计。CPU 占比为本次启动以来的累计值；
`Stack min free [B]` 是任务创建以来的历史最小剩余栈，而不是命令执行时的瞬时剩余栈。
该项不建议在现场正式版本中默认开启。

通用内存与任务诊断位于 `components/system_diagnostics/`，不依赖 BLE、SD 或 MQTT；其他
ESP-IDF 项目可将该组件直接加入依赖，并自行实现业务状态页。网关特有的 `sys status` 位于
`main/config/gateway_status_console.c`。

### 队列高水位说明

`sys status` 中的 `Queue depth` 是命令执行时的当前积压量；`Queue high water` 是本次上电以来
出现过的最大积压量。高水位以 `峰值[最大长度]` 显示，例如 `upload=227[256]` 表示上传队列
曾积压 227 条，容量为 256，尚余 29 条缓冲。字段与数据流对应如下：

| 字段 | 队列 | 生产者 → 消费者 | 用途 |
| --- | --- | --- | --- |
| `scan` | `scanner_event_queue` | NimBLE `scanner_gap_event()` → `device_manager_task()` | 已过滤的 BLE 扫描报告和扫描器状态事件。 |
| `ui` | `ui_event_queue` | 设备管理器/存储状态 → `app_ui` | 设备变化、扫描状态和 SD 状态等 UI 刷新事件。 |
| `capture` | `capture_event_queue` | `device_manager` → `csv_logger_task()` | 广播开始、结束事件的 CSV 写入。 |
| `upload` | `upload_event_queue` | `device_manager` → `publisher_task()` | Outbox 持久化和 MQTT 上报。 |

高水位接近队列容量但 `Dropped events=0` 时，说明出现过短暂积压但尚未丢失事件；应结合
`Dropped events`、Outbox 状态和 MQTT/SD 日志判断是否需要优化消费者处理能力。

`sys status` 的 Outbox 行反映当前可用性：`No SD` 表示当前不能持久化、`Full` 表示 SD 文件系统或
Outbox 容量已满、`Ready` 表示当前可读写。SD 满时保持已挂载状态，不会反复重挂载；只有卡移除、
超时等 I/O 故障才进入 `Retry`。`historical_failures` 是本次启动以来的失败累计数；即使 SD/MQTT 已恢复、
Outbox 已清空，该数也会保留，不能单独用于判断当前异常。

### 128 台设备硬件链路压力测试

`stress` 命令仅用于诊断固件。它会把 `SM_ICM9*`、`SM_ICD9*` 的合成广播报告投入与
真实扫描相同的事件队列，因此会产生真实的 CSV、Outbox 与 MQTT 测试数据；不得在现场
生产网关上启用。先在 `idf.py menuconfig` 中启用
`BLE Gateway diagnostics -> Enable the stress console command`，重新编译烧录后执行：

```text
stress run 128 20  # 128 台合成设备，以每秒 20 个报告注入
stress status      # 注入进度及四条队列的高水位
stress stop        # 请求停止尚未注入的报告
sys status         # 队列深度、高水位与丢弃计数
```

注入完成后，等待 `bcast_end_s` 超时以观察 128 个 `BROADCAST_ENDED` 事件。测试前应
确认扫描处于 `Scanning` 状态。完整 128 台测试要求设备表为空：重启网关后不要让真实
目标设备先广播，否则命令会根据剩余容量拒绝测试，避免把第 129 台的失败误判为性能问题。
该命令的低优先级任务不会暂停或替代真实 BLE 扫描。

### 现场运行证据

每 30 秒发布一条 `gateway_health` MQTT 消息。MQTT 已连接时它直接以 QoS 1 发送、不写入
SD；仅在 MQTT 断开时，SD Outbox 覆盖式保存最新一条心跳，待重连后发送。除网络、SNTP、SD
和 Outbox 状态外，消息还包含：

- `registered_devices`、`broadcasting_devices`：当前设备表与广播轮次数；
- `scan_reports_30s`、`filter_matched_30s`：过去 30 秒 NimBLE 已交付的扫描报告数和名称过滤命中数；
- `scan_timing_30s`：最近窗口内扫描回调平均/最大耗时，以及有效目标报告在扫描队列中的平均/最大等待时间；
- `dropped_events`：对应链路的累计丢弃数；应保持为零。
- `delivery`：`volatile_published` 表示 SD 不可写但 MQTT 已连接、仅实时发送的记录数；
  `unrecoverable_dropped` 表示 SD 不可写且 MQTT 不可用时无法恢复的广播事件数，应保持为零。

串口每 30 秒同步输出一条简短的 `publisher: health` 摘要。LCD 的设备行显示
`SD:OK/Retry` 与 `E:<错误码>`；`E:0` 表示当前 SD 状态正常。

LCD 网络行使用 `WiFi:<状态> MQTT:<状态> Outbox:<数量> OTA:<状态>` 格式。其中 `OK` 为已连接、
`Wait` 为正在连接或等待、`Off` 为未配置、`Err` 为错误；`Outbox` 是当前待 MQTT 确认的持久化
消息数量。OTA 检查发现已是当前版本时显示 `Current`，而不是错误；检查、准备、下载、校验和
重启时分别显示 `Check`、`Prep`、`DL`、`Verify`、`Boot`，通常为 `Idle`。

例如：

```text
publisher: health: scan_30s=3073 matched_30s=12 cb_us=18/76 wait_us=12/94/318 devices=127/0 drops=0/0/0/0
```

| 参数 | 含义 |
| --- | --- |
| `scan_30s` | 最近约 30 秒 NimBLE 交付给 `scanner_gap_event()` 的所有发现报告数量；包含周围设备的广播、Scan Response 及去重缓存刷新后的重复报告，不等于目标设备数或 CSV 记录数。`3073` 约为 102 报告/秒。 |
| `matched_30s` | 最近约 30 秒名称符合 `SM_ICM数字` / `SM_ICD数字` 规则的报告数量；这些报告才会进入设备管理器。 |
| `cb_us=avg/max` | 最近 30 秒所有 NimBLE 发现回调的平均/最大执行时间，单位微秒；包含广播字段解析、名称过滤和目标报告入队。它用于观察回调本身是否成为高负载瓶颈。 |
| `wait_us=samples/avg/max` | 最近 30 秒成功进入扫描队列的目标报告数、从入队到设备管理器取出的平均等待时间和最大等待时间，单位微秒；用于观察设备管理器是否跟得上扫描输入。 |
| `devices=A/B` | `A` 为当前设备表累计登记的目标 MAC 数，`B` 为当前正在一轮广播生命周期中的设备数。已结束的设备仍保留在设备表，直到重启。 |
| `drops=scan/ui/capture/upload` | 本次启动以来扫描、UI、CSV、上传四条事件队列的累计丢弃数；四项均为 `0` 表示未因队列满载而丢失应用事件。 |

在 `scan_30s` 较高时，应优先观察 `cb_us`、`wait_us`、`drops`、`Queue high water`、SD/Outbox
状态，而不是仅以扫描报告总数判断系统负载是否异常。回调或等待时间持续上升、同时队列高水位
接近容量，才是消费者处理能力不足的直接信号。

## 主机单元测试

无需硬件即可运行纯逻辑测试：

```bash
./tests/run_host_tests.sh
```

测试覆盖设备名称过滤、广播生命周期、CSV/JSON 编码和设备容量边界。`128_device_stress`
会构造 128 个不同 MAC 的有效广播，验证开始/结束事件、相同 MAC 的地址类型变化、
第 129 台拒绝、Outbox/PUBACK 语义，以及 UI（32）和 CSV/上传（256）队列的满载边界。
它是主机侧逻辑压力测试；真实 BLE 射频、SD 吞吐与 MQTT Broker 重传仍需硬件验收。

## 项目结构

```text
main/app/              应用入口、LCD 和 Touch UI
main/ble/              NimBLE 扫描和名称过滤
main/device/           设备表与广播生命周期
main/storage/          CSV 日志与 SD Outbox
main/config/           NVS 与 USB 串口配置
main/network/          Wi-Fi、SNTP、MQTT 和发布器
tests/                 主机侧单元测试
partitions/v1/16m.csv  16 MB Flash 分区表
doc/                   方案、架构、扫描说明、会议纪要和 OTA 设计文档
ota/                   本地 OTA 制品暂存与 UAT 操作说明（制品不提交 Git）
tools/                 构建、Manifest 生成与 UAT 发布脚本
sdkconfig.defaults     配置默认值
version.txt             普通构建的固件基线版本
```

主要模块：`ble_scanner`（扫描）、`device_filter`（名称过滤）、
`device_manager`（设备生命周期）、`csv_logger`（SD CSV）、`outbox`（断网缓存）、
`network_manager`（Wi-Fi）、`mqtt_service`（MQTT）、`gateway_publisher`（消息发布）
和 `app_ui`（LCD/Touch）。NimBLE 回调不直接操作 LVGL，UI 更新统一在应用任务中完成。

## 相关文档

- [厂内产品状态监控蓝牙网关方案](doc/厂内产品状态监控蓝牙网关方案.md)
- [嵌入式网关架构](doc/嵌入式网关架构.md)
- [BLE 扫描与设备生命周期管理说明](doc/BLE扫描与设备生命周期管理说明.md)
- [OTA 升级与发布操作说明](doc/OTA升级与发布操作说明.md)
- [OTA 发布制品暂存与 UAT 发布](ota/README.md)
- [厂内产品状态监控蓝牙网关方案评审会议纪要](doc/厂内产品状态监控蓝牙网关方案评审会议纪要.md)
- [M5Stack CoreS3 BSP 文档](https://docs.m5stack.com/zh_CN/esp_idf/m5cores3/bsp)
- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/)

## 当前限制

基础 HTTPS OTA、单网关 MQTT OTA 命令、断网 Outbox 重传和运行诊断已实现。MQTT TLS 证书
校验、OTA Manifest 签名、批量灰度编排、远程配置下发和多网关业务融合仍不属于当前交付范围。
交付前仍需完成 128 台设备并发广播、断网/SD 异常、OTA 中断回滚和长时间运行验证。
