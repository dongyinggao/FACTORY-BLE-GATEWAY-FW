# 服务器 MQTT 与 OTA 环境部署操作手册

## 当前 UAT 环境

| 项目 | 当前值 |
|---|---|
| 服务器 | `blegatewayserver`，Ubuntu 24.04.4，`192.168.19.21` |
| OTA 域名 | `ble-gateway-uat.singularmedical.net` |
| HTTPS OTA | Nginx，TCP `443`，`/srv/ble-gateway-data/ota/` |
| MQTT Broker | Mosquitto，TCP `1883`，监听 `192.168.19.21` |
| MQTT UAT 策略 | 明文 `mqtt://`、匿名、QoS 1、持久化开启 |

2026-08-05 已验证：`nginx`、`mosquitto` 服务均为 `enabled`/`active`；服务器本机 QoS 1
发布/订阅回环通过，开发机到 `192.168.19.21:1883` 的 TCP 连通性通过。当前 UAT 明文 MQTT
只能用于厂内隔离测试；正式环境必须迁移至 TLS、账号和 ACL。

当前 HTTPS 证书是自签名证书，CN/SAN 为 `ble-gateway-uat.singularmedical.net`，有效期至
2027-08-04。服务器本机使用该 PEM 作为信任根可获取 `manifest.json`。当前 `main` 分支仅使用
ESP-IDF 公共根证书包，不能信任该自签名证书；在该分支完成 OTA 前，必须二选一：由 IT 更换为
受 ESP-IDF 根证书包信任的 CA 证书，或将 UAT 公共 PEM 以内嵌证书方式纳入固件并重新烧录。

网关配置：

```text
cfg set mqtt_uri mqtt://192.168.19.21:1883
cfg set mqtt_qos 1
cfg set ota_manifest_uri https://ble-gateway-uat.singularmedical.net/ota/manifest.json
cfg commit
```

普通广播与健康消息可使用 `1883`。固件只接受 `mqtts://` 的远程 OTA 命令，因此当前环境可验证
MQTT 上报、Outbox 重传与 HTTPS OTA，不能验证 MQTT 下发 OTA。

## 从零部署

以下命令由拥有 sudo 权限的服务器维护人员执行。证书私钥、密码和 OTA 镜像不得提交 Git。

```bash
sudo apt update
sudo apt install -y nginx mosquitto mosquitto-clients
sudo systemctl enable --now nginx mosquitto
sudo install -d -o ble_gateway -g www-data -m 0750 /srv/ble-gateway-data/ota
```

创建 `/etc/mosquitto/conf.d/ble-gateway-uat.conf`：

```conf
# UAT only. Production must use TLS, credentials and per-gateway ACLs.
listener 1883 192.168.19.21
allow_anonymous true
autosave_interval 60
```

确认 `/etc/mosquitto/mosquitto.conf` 包含：

```conf
persistence true
persistence_location /var/lib/mosquitto/
log_dest file /var/log/mosquitto/mosquitto.log
include_dir /etc/mosquitto/conf.d
```

应用配置：

```bash
sudo systemctl restart mosquitto
sudo systemctl enable mosquitto
sudo systemctl status mosquitto --no-pager
sudo ss -ltnp | grep ':1883'
sudo tail -n 50 /var/log/mosquitto/mosquitto.log
```

Nginx 的 HTTPS 虚拟主机将 `/ota/` 映射到 OTA 文件目录。UAT 可使用自签名证书，但必须同步
处理上一节的固件信任链；正式环境应使用 IT/公共 CA 签发的证书。

```nginx
server {
    listen 80;
    server_name ble-gateway-uat.singularmedical.net;
    return 301 https://$host$request_uri;
}
server {
    listen 443 ssl;
    server_name ble-gateway-uat.singularmedical.net;
    ssl_certificate     /srv/ble-gateway-data/certs/ble-gateway-uat.singularmedical.net.crt;
    ssl_certificate_key /srv/ble-gateway-data/certs/ble-gateway-uat.singularmedical.net.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    location /ota/ {
        alias /srv/ble-gateway-data/ota/;
        autoindex off;
        add_header Cache-Control "no-store" always;
    }
}
```

```bash
sudo nginx -t
sudo systemctl reload nginx
sudo curl --cacert /srv/ble-gateway-data/certs/ble-gateway-uat.singularmedical.net.crt \
  --resolve ble-gateway-uat.singularmedical.net:443:192.168.19.21 \
  -I https://ble-gateway-uat.singularmedical.net/ota/manifest.json
```

当前 UFW 未启用。是否启用主机防火墙须由 IT 根据实际网段确认；启用前必须先放行 SSH、HTTPS
及网关网段到 `1883/TCP`，否则会中断远程维护或网关上报。

## MQTT 验证

在服务器第一终端订阅：

```bash
mosquitto_sub -h 192.168.19.21 -p 1883 -q 1 \
  -t 'factory/product-status/gateway/+/events' -v
```

在第二终端发布测试消息：

```bash
mosquitto_pub -h 192.168.19.21 -p 1883 -q 1 \
  -t 'factory/product-status/gateway/test-gateway/events' \
  -m '{"message_type":"broker_test"}'
```

开发机检查端口：

```bash
nc -zvw 5 192.168.19.21 1883
```

MQTTX 使用 Host=`192.168.19.21`、Port=`1883`、Protocol=`mqtt`、无用户名密码；订阅主题：

```text
factory/product-status/gateway/+/events
```

网关验收要求：串口出现 `mqtt_service: MQTT connected`，LCD 显示 `MQTT:OK`，MQTTX 收到
QoS 1 的 `broadcast` 与每 30 秒一条 `gateway_health`。关闭 Broker 后应看到 Outbox 积压；
重启 Broker 后消息按顺序重传，服务端按 `gateway_id + event_id` 去重。

## OTA 发布与运维

OTA 制品先上传版本化镜像与 Manifest，最后才替换活动 `manifest.json`；发布、回退与全擦除操作见
[OTA 升级与发布操作说明](OTA升级与发布操作说明.md)。当前 OTA 文件目录为
`/srv/ble-gateway-data/ota/`，活动地址为：

```text
https://ble-gateway-uat.singularmedical.net/ota/manifest.json
```

日常巡检：

```bash
systemctl is-active nginx mosquitto
sudo journalctl -u mosquitto -n 100 --no-pager
sudo tail -n 100 /var/log/mosquitto/mosquitto.log
df -h / /srv/ble-gateway-data
sudo ss -ltnp | grep -E ':(1883|443)'
```

正式环境必须增加 MQTT TLS `8883`、服务器证书校验、独立账号、按网关 Topic 的 ACL、日志轮转、
数据与 OTA 制品备份；届时网关将 MQTT URI 改为 `mqtts://...`，才能启用远程 MQTT OTA 命令。
