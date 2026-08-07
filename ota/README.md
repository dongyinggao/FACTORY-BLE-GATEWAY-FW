# OTA 发布制品暂存与 UAT 发布

本目录仅用于本机暂存待发布的 OTA 文件，**不纳入源代码 Git 管理**。固件镜像
(`ble_gateway-<version>.bin`) 和 Manifest (`manifest-<version>.json`、`manifest.json`)
的受控保存位置是 UAT OTA 服务器：

```text
/srv/ble-gateway-data/ota
```

当前 UAT 访问地址为：

```text
https://ble-gateway-uat.singularmedical.net/ota/
```

HTTPS OTA 访问使用域名；服务器维护 SSH 使用 UAT 内网地址 `192.168.19.21`，避免 VPN 或
分流 DNS 将域名解析到错误地址。因此使用 `ssh ble_gateway@192.168.19.21` 登录后，`ls ~`
看不到 `ota/`；请使用以下命令查看已发布文件：

```bash
sudo ls -lah /srv/ble-gateway-data/ota
```

不要将私钥、账号密码或生产证书放入本目录。

项目根目录的 [`version.txt`](../version.txt) 定义普通 `idf.py build` 的当前基线版本；当前值为
`1.0.0`。因此日常烧录的 `build/ble_gateway.bin` 会显示 `running_version=1.0.0`，不再使用
Git 哈希作为版本号。正式发布脚本仍通过 `-DPROJECT_VER=<version>` 覆盖该基线版本。

## 一键构建、打包与发布

发布前先确认版本号和发布序号均为新值。例如当前已确认序号为 `106` 时，下一次可发布
`1.0.1` / `107`：

```bash
./tools/publish_ota_uat.sh 1.0.1 107
```

脚本会依次完成：

1. 加载 ESP-IDF 5.5.5 环境，以固定 `PROJECT_VER=1.0.1` 构建 `build/release/ble_gateway.bin`。
2. 从实际镜像生成 `ota/manifest-1.0.1.json`，其中包含镜像大小与 SHA-256。
3. 将镜像复制至本目录作为暂存文件。
4. 通过 SSH/SCP 上传到服务器临时目录，再安装到
   `/srv/ble-gateway-data/ota/ble_gateway-1.0.1.bin` 和版本化 Manifest 文件。
5. **最后**用相同的版本化 Manifest 更新服务器的 `manifest.json`，使网关可发现新版本。

脚本不会保存 SSH 或 `sudo` 密码；根据服务器策略，运行时会要求交互输入。默认 SSH 目标为
`ble_gateway@192.168.19.21`，而 Manifest 和镜像 URL 仍为
`ble-gateway-uat.singularmedical.net`。脚本会在开始构建前先校验 SSH 连通性；如需更换账号
或目录，可在运行前设置：

```bash
OTA_SSH_TARGET=user@host OTA_REMOTE_DIR=/srv/ota ./tools/publish_ota_uat.sh 1.0.1 107 host
```

发布后验证：

```bash
curl -fsS https://ble-gateway-uat.singularmedical.net/ota/manifest.json
curl -I https://ble-gateway-uat.singularmedical.net/ota/ble_gateway-1.0.1.bin
```

网关执行 `ota check`、`ota status` 后，应显示 `available_version=1.0.1` 和
`available:107`。

## 回退与版本测试

### 受控 OTA 回退（推荐）

保留服务器上的历史镜像与历史 Manifest。要从新版本测试回 `1.0.0`，通过物理串口将
Manifest 临时指向历史文件，再明确允许降级：

```text
cfg set ota_manifest_uri https://ble-gateway-uat.singularmedical.net/ota/manifest-1.0.0.json
cfg commit
ota check
ota start --allow-downgrade
```

`--allow-downgrade` 只能在物理串口使用，仍会校验 HTTPS、硬件型号、分区布局、镜像大小和
SHA-256。它不会降低 NVS 中已确认的最高发布序号；因此回退后再次发布的新版本时，必须使用
大于历史最高值的序号，例如已确认 `106` 时下一版至少为 `107`。

### USB 串口直接恢复历史镜像

当旧固件不支持 OTA、或需要从固定 `1.0.0` 基线重新开始验证时，可在断开调试器后用 USB
串口将历史应用镜像写回 factory 分区。先从服务器取回保存的历史镜像：

```bash
scp ble_gateway@192.168.19.21:/srv/ble-gateway-data/ota/ble_gateway-1.0.0.bin ota/
```

然后在本机执行（此操作保留 NVS 中的网关配置，但清除 OTA 分区选择，使 Bootloader 从
factory 分区 `0x20000` 启动）：

```bash
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 erase_region 0xf000 0x2000
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x20000 ota/ble_gateway-1.0.0.bin
```

如果还要使发布序号策略也回到“出厂未确认”状态，只能擦除整个 NVS 分区或整片 Flash；这会同时
清除网关 ID、Wi-Fi、MQTT 与其他配置，通常不建议用于现场设备。测试时保留 NVS 后，升级到
序号不高于原已确认值的镜像应使用 `ota start --allow-downgrade`。

### 清空整片 Flash（含 NVS）

仅在准备从干净出厂状态重新测试时使用。该操作不可恢复，会清除 16 MiB Flash 中的 bootloader、
分区表、factory/OTA 固件、`otadata`、NVS 配置、Wi-Fi 凭据和内部存储分区；**不会清除外置
SD 卡**。先确认串口没有被监视器或串口助手占用，再执行：

```bash
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash
```

第二条命令会烧录当前 `build/` 中的 bootloader、分区表、初始 OTA 数据和
`build/ble_gateway.bin`。本项目当前普通构建版本由 `version.txt` 固定为 `1.0.0`；如刚修改
源代码，请先执行 `idf.py build`。擦除完成后必须重新通过串口配置网关。

## 全新或全擦除设备首次投用

以下流程适用于全新网关，或已经执行 `erase-flash` 的设备。全擦除后 NVS 没有任何网络、网关
身份和 OTA 发布序号；首次烧录的普通构建版本为 `1.0.0`。

### 1. 构建、烧录并打开串口

```bash
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

关闭串口监视器后，推荐执行初始化脚本。它使用本项目 UAT 默认配置，并在运行时隐藏输入 Wi-Fi
密码；可通过环境变量覆盖网关 ID、位置和网络参数，详见 `--help`。

```bash
./tools/provision_gateway.sh /dev/ttyACM0
```

如需手动配置，请重新打开串口监视器。请将下列示例值替换成现场实际值；`<...>` 仅表示待替换的
内容，输入时不要包含尖括号。

### 2. 写入基础配置并保存到 NVS

```text
cfg set gateway_id GW-01
cfg set gateway_loc Room101-North
cfg set bcast_end_s 5
cfg set wifi_ssid <现场Wi-Fi名称>
cfg set wifi_password <现场Wi-Fi密码>
cfg set mqtt_uri mqtt://<Broker-IP>:1883
cfg set mqtt_qos 1
cfg set ntp_server ntp.aliyun.com
cfg set timezone CST-8
cfg set ota_manifest_uri https://ble-gateway-uat.singularmedical.net/ota/manifest.json
cfg commit
```

`gateway_id`、`gateway_loc`、Wi-Fi、NTP 和 `ota_manifest_uri` 是首次 OTA 测试所需配置；
`mqtt_uri` 是正常数据上报所需配置。若测试 Broker 没有认证，不要设置 `mqtt_username` 和
`mqtt_password`；如现场 Broker 开启认证，再额外输入：

```text
cfg set mqtt_username <用户名>
cfg set mqtt_password <密码>
cfg commit
```

每次 `cfg commit` 会原子写入 NVS 并自动触发网络服务重新加载。密码在 `cfg show` 中显示为
`<hidden>`。

### 3. 确认网关已恢复运行

等待 Wi-Fi 获得 IP、SNTP 完成校时、MQTT 连接后，执行：

```text
cfg show
sys status
ota status
```

预期 `sys status` 包含 `Wi-Fi=Connected`、`SNTP=Synced`；配置了可访问 Broker 时还应显示
`MQTT=Connected`。BLE 默认会自动开始扫描，也可使用 LCD 左下角按钮启停扫描。

### 4. 从 1.0.0 验证首次 OTA

先在服务器发布更高版本和序号，例如 `1.0.1 / 107`：

```bash
./tools/publish_ota_uat.sh 1.0.1 107
```

然后在网关 LCD 触摸 `Check update`，确认显示 `Start update` 后再次触摸；或通过串口执行：

```text
ota check
ota status
ota start
```

升级成功重启后，`ota status` 应显示 `running_version=1.0.1`、`confirmed:107`，LCD 顶部显示
`FW:1.0.1`。

若通过 USB 手动烧录了较旧镜像但保留 NVS，NVS 中的已确认发布序号不会自动降低。普通 OTA 仍会
拒绝小于或等于已确认序号的 Manifest；以 `ota status` 中的版本与序号作为判断依据。要恢复至
更高的已发布镜像，可在物理串口执行
`ota start --allow-downgrade`；该命令仅放宽序号比较，仍会执行 HTTPS、兼容性、长度与 SHA-256
校验。首次测试应保留 `1.0.0` 的版本化镜像和 Manifest，便于后续回退验证。

### 可选配置

- `cfg set name_rules <规则>`：仅在后续启用自定义设备名称规则时配置；未设置时使用默认
  `SM_ICM数字` / `SM_ICD数字` 过滤规则。
- `cfg set bcast_end_s <5-300>`：广播结束判定超时，当前默认值为 `5` 秒。

以上“首次投用”配置是 NVS 的唯一来源；重新执行 `erase-flash` 后必须再次配置。

完整接口和验收要求见
[`doc/OTA升级与发布操作说明.md`](../doc/OTA升级与发布操作说明.md)。
