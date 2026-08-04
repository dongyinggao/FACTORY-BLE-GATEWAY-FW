# OTA 升级与发布操作说明

## 范围与安全边界

本项目采用 ESP-IDF 双 OTA 分区。维护人员可通过 USB Serial/JTAG 串口命令升级；第一阶段
也支持网关专属 MQTT 命令触发，命令必须来自 `mqtts://` 连接。Manifest 和固件均要求 HTTPS，
使用 ESP-IDF 内置根证书包验证服务器证书；下载后的镜像还必须匹配 Manifest 中的 SHA-256。

当前版本尚未实现 Manifest 数字签名、批量活动编排、灰度发布或 SD 卡离线升级。发布服务器
的 HTTPS 证书、MQTT 命令发布权限和 SHA-256 值必须由发布负责人复核。UAT 固件已嵌入厂内
测试服务器的公开 PEM 证书；其他 HTTPS 地址仍使用 ESP-IDF 证书包。正式环境应采用受控 CA
或固化的正式根证书，并完成证书轮换方案评审。

## MQTT 远程命令（第一阶段）

网关只订阅自身的命令 Topic：

```text
factory/product-status/gateway/<gateway_id>/commands/ota
```

命令以 QoS 1 发布，JSON 必须包含：

```json
{"message_type":"ota_command","command_id":"cmd-20260803-001","campaign_id":"pilot-01","manifest_url":"https://ble-gateway-uat.singularmedical.net/ota/manifest.json","expires_at_epoch_s":1780000000}
```

`command_id` 和 `campaign_id` 仅可使用字母、数字、`-`、`_`、`.`、`:`；网关在 SNTP 已同步后
校验到期时间，并把最近成功接收的 `command_id` 保存到 NVS，重复投递不会再次升级。`mqtts://`
连接使用 ESP-IDF 证书包校验 Broker 的服务器证书；开发用明文 Mosquitto 只能验证常规事件上报，
不能用于远程 OTA 命令。命令状态
以 QoS 1 发送到 `factory/product-status/gateway/<gateway_id>/ota/status`。远程请求最多等待
15 分钟无广播窗口，之后执行与串口 `ota start` 相同的镜像校验、切换与回滚流程；不允许远程
降级。状态消息首期不进入 SD Outbox，服务端应按 `command_id` 保存活动状态并在超时后告警。

## 发布模型

发布构建必须固定 `PROJECT_VER`。普通构建版本由仓库根目录 `version.txt` 定义（当前为
`1.0.1`）；正式发布使用 `tools/publish_ota_uat.sh` 显式设置版本与发布序号，不能使用开发
构建的 Git 哈希或 `-dirty` 字样。UAT 一键发布示例：

```bash
./tools/publish_ota_uat.sh 1.0.2 102
```

脚本会先检查 SSH 到 UAT 管理地址的连通性，再构建镜像、生成 SHA-256 Manifest、上传版本化
镜像与 Manifest，最后才切换服务器的活动 `manifest.json`。完整操作、服务器路径、全擦除和
回退步骤见 [ota/README.md](../ota/README.md)。

仓库根目录的 `ota/` 仅用于**本机暂存**待上传的 `.bin`、Manifest 和可选页面，相关文件已被
`.gitignore` 排除；正式发布内容由受控 OTA 文件服务器独立保存、备份和授权。提交固件源代码时
只提交发布工具、流程说明和必要的公开根证书，禁止提交发布镜像、私钥、密码或生产证书。

```json
{
  "schema_version": 1,
  "version": "1.0.2",
  "release_sequence": 102,
  "hardware_model": "m5stack-cores3-se",
  "idf_target": "esp32s3",
  "partition_layout": "ble-gateway-16m-v1",
  "image_url": "https://ble-gateway-uat.singularmedical.net/ota/ble_gateway-1.0.2.bin",
  "image_size": 1600000,
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

`image_size` 必须等于镜像的字节数，`sha256` 是镜像文件的 64 位十六进制 SHA-256。实际镜像
版本必须与 Manifest 的 `version` 一致。`release_sequence` 是全局单调递增的正整数，正常升级
只能接收大于最后确认序号的镜像；`hardware_model`、`idf_target`、`partition_layout` 必须与
网关固件固定值完全一致。分区布局变更不是 OTA 兼容升级，必须使用 USB 全量烧录。

## 网关操作

先配置并保存 Manifest 地址：

```text
cfg set ota_manifest_uri https://ble-gateway-uat.singularmedical.net/ota/manifest.json
cfg commit
ota check
ota status
ota start
```

`ota check` 只获取并校验 Manifest。`ota start` 会再次获取 Manifest；仅当 Wi-Fi 已连接、SNTP
已同步、SD 卡处于可写且未满状态、没有活动广播轮次时，才暂停扫描并等待采集/上传队列排空。
任何前置条件、网络、长度或 SHA-256 校验失败都会保留当前固件并恢复扫描。

现场也可使用 LCD 底部的 OTA 按钮：`Check update` 首先执行与 `ota check` 相同的检查，只有
成功后才显示 `Start update`；第二次触摸才执行与 `ota start` 相同的升级。下载、校验和重启时
按钮不可点击。该两步交互用于避免误触升级；受控降级仍只能使用物理串口命令。

如质量人员需要执行回退验证，只能在物理串口现场明确输入：

```text
ota start --allow-downgrade
```

该命令绕过发布序号检查，但不会绕过 HTTPS、硬件兼容、SHA-256、镜像版本和采集保护检查；
不得用于日常部署。

`release_sequence` 保存于 NVS，用于阻止普通 OTA 回退。因此 USB 手动烧录较旧镜像但未擦除
NVS 时，普通 `ota start` 仍会拒绝序号小于或等于已确认序号的 Manifest。`ota status` 显示
运行版本、可用版本与已确认序号，是 OTA 策略判断的依据；LCD 仅显示当前运行版本，避免将
NVS 序列号误解为当前镜像已确认状态。

完成写入后设备重启到新分区。新固件必须在 10 秒内恢复 BLE 扫描和 SD 存储；否则，或首次
启动期间断电/崩溃，Bootloader 会回滚到上一有效镜像。NVS 中的网关 ID、位置、网络参数和
扫描参数不会被 OTA 覆盖。

网关使用外部电源，Wi-Fi 初始化后固定设为 `WIFI_PS_NONE`；升级期间无需再切换或恢复省电
模式。该取舍增加了常态功耗，但可降低 Wi-Fi 休眠对连续 BLE 采集、MQTT 和 HTTPS OTA 传输
的影响。

## 发布验收

1. 在测试网关先执行 `ota check`，确认可用版本、URL 和大小正确。
2. 执行 `ota start`，确认设备重启、`ota status` 的运行版本变化，且扫描、CSV、MQTT 均恢复。
3. 制造错误 SHA-256、截断镜像和首次启动重启三种场景，确认没有切换或能自动回滚。
4. 断网、SD 满、正在广播时执行 `ota start`，确认升级被拒绝且采集不中断。
5. 使用错误硬件型号、目标芯片、分区布局或低于已确认序号的 Manifest，确认网关拒绝升级；再用
   `ota start --allow-downgrade` 仅验证受控回退流程。
