# CoreS3-SE BLE Gateway Tester

## Project Specification v1.0

---

# 1. 项目概述

## 1.1 项目名称

BLE Gateway Tester

基于 M5Stack CoreS3-SE 的蓝牙网关测试工装。

---

## 1.2 项目目标

开发一款低成本、快速部署的蓝牙设备在线状态测试设备。

设备部署在测试环境中：

* 自动扫描周围指定 BLE 设备
* 识别目标设备
* 记录设备在线/离线状态
* 统计在线时间
* 本地保存测试数据
* WiFi上传云端服务器
* PC端工具查询历史数据

---

# 2. 硬件平台

## 2.1 主控

产品：

```
M5Stack CoreS3-SE
```

核心芯片：

```
ESP32-S3
```

资源：

| 资源        | 规格                   |
| --------- | -------------------- |
| CPU       | Xtensa LX7 Dual Core |
| 主频        | 240MHz               |
| RAM       | 512KB                |
| PSRAM     | 8MB                  |
| Flash     | 16MB                 |
| WiFi      | 2.4GHz               |
| Bluetooth | BLE 5                |
| Display   | 2.0 inch LCD         |
| Storage   | microSD              |

---

# 3. 软件环境

## 3.1 开发环境

操作系统：

```
Ubuntu 22.04 LTS
```

IDE:

```
VS Code
```

插件：

```
ESP-IDF Extension
```

---

## 3.2 ESP-IDF版本

固定：

```
ESP-IDF v5.4.4
```

原因：

* M5Stack CoreS3 BSP官方基于5.4.x验证
* 稳定性优先
* 适合工业长期运行

---

## 3.3 Framework

采用：

```
ESP-IDF Native
```

不使用：

```
Arduino Framework
```

原因：

* FreeRTOS控制能力更强
* 更适合工业设备
* 方便后期产品化

---

# 4. 软件总体架构

```
+--------------------------------+
|          Qt PC Tool             |
|                                |
| - Device List                  |
| - Log Download                 |
| - Data Export                  |
+---------------+----------------+
                |
                |
             WiFi MQTT
                |
+---------------v----------------+
|        CoreS3-SE Gateway        |
|                                |
| +----------------------------+ |
| | Application Layer          | |
| +----------------------------+ |
|                                |
| Device Manager                |
| Storage Manager               |
| MQTT Manager                  |
|                                |
+--------------------------------+
                |
+---------------v----------------+
|        ESP-IDF Layer            |
|                                |
| NimBLE                         |
| WiFi                           |
| FATFS                          |
| NVS                            |
| FreeRTOS                      |
+--------------------------------+
                |
+---------------v----------------+
|          ESP32-S3              |
+--------------------------------+

```

---

# 5. FreeRTOS任务设计

## 5.1 BLE Scan Task

任务：

```
ble_scan_task
```

职责：

* 周期扫描BLE广播
* 解析ADV数据
* 获取：

```
Device Name
MAC
RSSI
Manufacturer Data
```

扫描周期：

默认：

```
5s
```

---

## 5.2 Device Manager Task

任务：

```
device_manager_task
```

职责：

管理目标设备状态。

状态：

```
UNKNOWN

    ↓

ONLINE

    ↓

OFFLINE

```

判断：

例如：

```
last_seen > 60s

=> OFFLINE
```

---

## 5.3 Storage Task

任务：

```
storage_task
```

职责：

保存：

* 上线时间
* 下线时间
* 在线持续时间

存储：

优先：

```
FATFS + CSV
```

格式：

```csv
timestamp,
device,
event,
duration
```

例如：

```csv
2026-07-21 10:20:30,
ICD001,
ONLINE,
```

---

## 5.4 MQTT Task

任务：

```
mqtt_task
```

职责：

上传云端。

Topic:

```
ble_gateway/device_status
```

数据：

JSON:

```json
{
"id":"ICD001",
"status":"online",
"time":"2026-07-21 10:20:30",
"rssi":-55
}
```

---

## 5.5 UI Task

任务：

```
ui_task
```

显示：

首页：

```
BLE Gateway

Devices: 12

Online: 8

WiFi: OK

Cloud: OK
```

---

# 6. BLE扫描设计

## 6.1 BLE协议栈

采用：

```
ESP-IDF NimBLE
```

不用：

```
Bluedroid
```

原因：

* 资源占用低
* 扫描稳定
* 更适合网关

---

# 6.2 设备过滤

目标：

设备名称：

```
ICD*
ICM*
```

示例：

```
ICD001
ICD_SENSOR01

ICM_A01
```

过滤：

```c
strncmp(name,"ICD",3)

strncmp(name,"ICM",3)
```

---

# 6.3 RSSI管理

保存：

```
RSSI
```

用途：

判断：

* 距离
* 信号稳定性

---

# 7. 数据结构

## BLE设备结构

```c
typedef struct
{

char name[32];

uint8_t mac[6];


int rssi;


uint32_t first_seen;


uint32_t last_seen;


uint32_t online_time;


bool online;


}ble_device_t;

```

---

# 8. 数据存储设计

## 本地文件

目录：

```
/sdcard/log/
```

文件：

```
device_status.csv
```

---

示例：

```
time,name,event,duration


10:20:01,ICD001,ONLINE,0


10:50:10,ICD001,OFFLINE,1809

```

---

# 9. WiFi设计

模式：

```
STA Mode
```

配置：

NVS保存：

```
SSID

PASSWORD

SERVER IP

PORT
```

---

# 10. MQTT设计

协议：

```
MQTT v3.1.1
```

QoS:

```
QoS1
```

保证：

设备状态不丢失。

---

# 11. PC工具

开发：

```
Qt6/C++
```

功能：

## 设备管理

显示：

```
Device
MAC
RSSI
Status
```

---

## 数据下载

功能：

```
Get History Log
```

---

## 数据导出

格式：

```
CSV
Excel
```

---

# 12. 开发阶段计划

## Week 1

环境 + BSP

完成：

* ESP-IDF
* CoreS3 BSP
* LCD
* WiFi

---

## Week 2

BLE核心

完成：

* BLE扫描
* ICD/ICM过滤
* 状态机

---

## Week 3

数据系统

完成：

* 本地存储
* MQTT
* 云端接口

---

## Week 4

测试工具

完成：

* Qt工具
* 多设备测试
* 长时间稳定性测试

---

# 13. 编码规范

语言：

```
C99
```

命名：

函数：

```
xxx_manager_xxx()
```

任务：

```
xxx_task()
```

结构体：

```
xxx_t
```

---

# 14. Codex开发规则

生成代码时：

必须遵循：

1. ESP-IDF Native
2. FreeRTOS任务模型
3. 不使用Arduino API
4. 模块化设计
5. 所有任务必须有错误处理
6. 所有资源必须释放
7. 避免阻塞主任务

---

# 当前开发状态

```
[完成]

✓ Ubuntu 22.04
✓ VS Code
✓ ESP-IDF v5.4.4


[进行中]

→ CoreS3-SE BSP Demo


[下一步]

→ BLE Scanner Demo

```

---