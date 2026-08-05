# OTA 升级与发布操作说明

## 范围与安全边界

本项目采用 ESP-IDF 双 OTA 分区。维护人员可通过 USB Serial/JTAG 串口命令升级；第一阶段
也支持网关专属 MQTT 命令触发，命令必须来自 `mqtts://` 连接。Manifest 和固件均要求 HTTPS，
使用 ESP-IDF 内置根证书包验证服务器证书；下载后的镜像还必须匹配 Manifest 中的 SHA-256。

当前版本尚未实现 Manifest 数字签名、批量活动编排、灰度发布或 SD 卡离线升级。发布服务器
的 HTTPS 证书、MQTT 命令发布权限和 SHA-256 值必须由发布负责人复核。当前使用公共 CA
证书包；厂内 OTA 服务稳定后，应评审是否改为固化厂内 CA 根证书。

## MQTT 远程命令（第一阶段）

网关只订阅自身的命令 Topic：

```text
factory/product-status/gateway/<gateway_id>/commands/ota
```

命令以 QoS 1 发布，JSON 必须包含：

```json
{"message_type":"ota_command","command_id":"cmd-20260803-001","campaign_id":"pilot-01","manifest_url":"https://ota.example.com/manifest.json","expires_at_epoch_s":1780000000}
```

`command_id` 和 `campaign_id` 仅可使用字母、数字、`-`、`_`、`.`、`:`；网关在 SNTP 已同步后
校验到期时间，并把最近成功接收的 `command_id` 保存到 NVS，重复投递不会再次升级。`mqtts://`
连接使用 ESP-IDF 证书包校验 Broker 的服务器证书；开发用明文 Mosquitto 只能验证常规事件上报，
不能用于远程 OTA 命令。命令状态
以 QoS 1 发送到 `factory/product-status/gateway/<gateway_id>/ota/status`。远程请求最多等待
15 分钟无广播窗口，之后执行与串口 `ota start` 相同的镜像校验、切换与回滚流程；不允许远程
降级。状态消息首期不进入 SD Outbox，服务端应按 `command_id` 保存活动状态并在超时后告警。

## 发布文件

发布构建必须固定 `PROJECT_VER`，不能将 `git describe` 的 `-dirty` 开发版本作为发布版本。可用
以下脚本构建镜像并生成 Manifest：

```bash
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
./tools/build_ota_release.sh 1.0.1 101 \
  https://ota.example.com/ble-gateway/ble_gateway-1.0.1.bin \
  release/manifest-1.0.1.json
```

脚本会读取镜像内的 ESP-IDF 应用描述，确认其版本为 `1.0.1`、项目名为 `ble_gateway`，再生成
大小和 SHA-256。发布服务器提供生成的静态 JSON Manifest，例如：

```json
{
  "schema_version": 1,
  "version": "1.0.1",
  "release_sequence": 101,
  "hardware_model": "m5stack-cores3-se",
  "idf_target": "esp32s3",
  "partition_layout": "ble-gateway-16m-v1",
  "image_url": "https://ota.example.com/ble-gateway/ble_gateway-1.0.1.bin",
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
cfg set ota_manifest_uri https://ota.example.com/ble-gateway/manifest.json
cfg commit
ota check
ota status
ota start
```

`ota check` 只获取并校验 Manifest。`ota start` 会再次获取 Manifest；仅当 Wi-Fi 已连接、SNTP
已同步、SD 卡处于可写且未满状态、没有活动广播轮次时，才暂停扫描并等待采集/上传队列排空。
任何前置条件、网络、长度或 SHA-256 校验失败都会保留当前固件并恢复扫描。

如质量人员需要执行回退验证，只能在物理串口现场明确输入：

```text
ota start --allow-downgrade
```

该命令绕过发布序号检查，但不会绕过 HTTPS、硬件兼容、SHA-256、镜像版本和采集保护检查；
不得用于日常部署。

完成写入后设备重启到新分区。新固件必须在 10 秒内恢复 BLE 扫描和 SD 存储；否则，或首次
启动期间断电/崩溃，Bootloader 会回滚到上一有效镜像。NVS 中的网关 ID、位置、网络参数和
扫描参数不会被 OTA 覆盖。

## 发布验收

1. 在测试网关先执行 `ota check`，确认可用版本、URL 和大小正确。
2. 执行 `ota start`，确认设备重启、`ota status` 的运行版本变化，且扫描、CSV、MQTT 均恢复。
3. 制造错误 SHA-256、截断镜像和首次启动重启三种场景，确认没有切换或能自动回滚。
4. 断网、SD 满、正在广播时执行 `ota start`，确认升级被拒绝且采集不中断。
5. 使用错误硬件型号、目标芯片、分区布局或低于已确认序号的 Manifest，确认网关拒绝升级；再用
   `ota start --allow-downgrade` 仅验证受控回退流程。
