我重新搜索了一遍，重点看：

* **CoreS3 / CoreS3-SE**
* **ESP-IDF**
* **GitHub开源项目**
* **可直接烧录测试硬件**
* **BLE / WiFi / LCD / SD / Touch**

结论：

> **没有找到一个“开箱即用、完全匹配 CoreS3-SE + ESP-IDF + BLE Gateway”的完整开源工程。**
>
> 但是有几个非常适合你当前阶段的工程，可以直接拿来验证硬件，并作为后续开发基础。

而且你的方向是正确的：**先不要自己从0写BLE Gateway，应该先验证硬件BSP、外设、网络能力。**

---

# 推荐优先级排序

| 项目                           | 适合程度  | 用途                  | 建议    |
| ---------------------------- | ----- | ------------------- | ----- |
| M5Stack官方 CoreS3 BSP Example | ⭐⭐⭐⭐⭐ | 验证硬件                | 第一步使用 |
| M5CoreS3 Arduino Example     | ⭐⭐⭐⭐  | 快速测试LCD/Touch/Audio | 备用    |
| ESP-IDF官方BLE Scanner         | ⭐⭐⭐⭐⭐ | 验证BLE               | 第二步   |
| OpenMQTTGateway              | ⭐⭐⭐   | BLE Gateway参考       | 参考架构  |
| StackChan/AI项目               | ⭐⭐    | 综合测试                | 不适合当前 |

---

# 1. 首推：M5Stack CoreS3 ESP-IDF BSP Example ⭐⭐⭐⭐⭐

这是最接近你的工程。

官方提供：

```text
espressif/m5stack_core_s3
```

BSP。

官方说明：

CoreS3 BSP用于快速初始化：

* LCD
* Touch
* Audio
* SD
* 外设驱动

并通过：

```bash
idf.py add-dependency "espressif/m5stack_core_s3"
```

集成。([docs.m5stack.com][1])

---

## 适合你测试：

### LCD

验证：

```
屏幕亮
LVGL运行
```

---

### Touch

验证：

```
触摸坐标
```

---

### SD

验证：

```
microSD读写
```

---

### Audio

验证：

```
speaker
mic
```

---

## 推荐你现在做：

不要继续hello_world。

直接创建：

```text
coreS3_bsp_demo
```

包含：

```
main
 |
 └── app_main.c

components
 |
 └── managed_components
```

测试：

* LCD显示
* Touch按钮
* SD写文件

---

# 2. M5CoreS3 Arduino Library ⭐⭐⭐⭐

GitHub：

[M5CoreS3 GitHub](https://github.com/m5stack/M5CoreS3?utm_source=chatgpt.com)

虽然你现在走ESP-IDF，但是这个仓库非常适合：

* 查GPIO
* 查外设初始化
* 查例程

里面有：

examples：

例如：

```
Basic
 ├── touch
 ├── display
 ├── power
 ├── sd
 └── audio
```

不过：

你最终产品不要切Arduino。

原因：

你的目标：

工业测试工装。

ESP-IDF更合适。

---

# 3. ESP-IDF官方BLE Scanner ⭐⭐⭐⭐⭐

这个其实比找CoreS3 BLE工程更重要。

因为：

CoreS3-SE：

本质：

```
ESP32-S3
```

BLE部分完全由ESP-IDF提供。

你的BLE扫描：

不依赖M5 BSP。

建议：

直接使用：

ESP-IDF:

```
examples/bluetooth/nimble/
```

或者：

```
examples/bluetooth/nimble/bleprph
```

改成scanner。

---

你的最终结构：

```
M5Stack BSP

负责：

LCD
Touch
SD


ESP-IDF NimBLE

负责：

BLE扫描

```

这是正确架构。

---

# 4. OpenMQTTGateway（强烈建议研究）⭐⭐⭐

这是最接近你最终产品的软件。

项目：

OpenMQTTGateway

特点：

* ESP32 BLE Scanner
* MQTT上传
* WiFi Gateway

社区大量使用。

架构：

```
BLE Device

↓

ESP32 Gateway

↓

MQTT

↓

Server
```

与你：

```
ICD/ICM设备

↓

CoreS3-SE

↓

MQTT

↓

服务器
```

几乎一样。

但是：

不要直接移植。

原因：

它包含：

* Home Assistant
* 各种BLE协议解析
* 大量sensor

你的需求：

简单：

```
扫描
过滤
计时
上传
```

自己写更干净。

---

# 5. CoreS3-SE专属项目为什么少？

这是正常现象。

原因：

CoreS3-SE：

发布时间较晚。

而且：

它的软件生态继承：

```
CoreS3
```

不是独立生态。

官方也说明：

CoreS3-SE和CoreS3硬件存在差异，例如：

* Camera
* Proximity
* IMU
* Magnetometer

相关代码不兼容。([docs.m5stack.com][2])

所以GitHub搜索：

```
CoreS3-SE
```

很少。

应该搜索：

```
CoreS3
ESP32-S3
M5Unified
m5stack_core_s3
```

---

# 6. 我建议你的测试顺序

不要找一个“大工程”。

按工业开发流程：

---

## Test 1：BSP Demo

目标：

确认板子正常。

测试：

✅ LCD
✅ Touch
✅ SD
✅ RTC

---

## Test 2：WiFi Demo

目标：

验证网络。

测试：

```
扫描AP

连接WiFi

获取IP
```

---

## Test 3：BLE Scanner

目标：

验证：

```
发现手机
发现Beacon
发现ICD/ICM
```

输出：

```
Name:
ICD001

RSSI:
-55dBm
```

---

## Test 4：MQTT Demo

目标：

上传：

```
device online
RSSI
timestamp
```

---

# 7. 针对你的项目，我建议现在不要换硬件

重新评估：

CoreS3-SE：

优势：

✅ ESP32-S3
✅ 16MB Flash
✅ 8MB PSRAM
✅ LCD
✅ SD
✅ RTC
✅ 电源管理
✅ 外壳完整

缺点：

❌ 没有外置BLE天线接口（需要确认距离）

但是：

你的要求：

```
空旷10米
房间3~4台
```

完全可测试。

---

# 8. 下一步我建议

你现在已经完成：

✅ Ubuntu
✅ VSCode
✅ ESP-IDF v5.5.5
✅ BSP添加

下一步不要找大项目。

直接做：

## CoreS3-SE Hardware Validation Demo

我建议创建：

```
firmware/
|
├── bsp_test
│
├── wifi_test
│
├── ble_scan_test
│
└── mqtt_test
```

其中：

`ble_scan_test`

最终直接演变为你的：

```
ble_gateway_service
```

---

如果你愿意，下一步我可以直接给你一个：

**基于 ESP-IDF v5.5.5 + m5stack_core_s3 BSP 4.0.0 的 CoreS3-SE 综合测试工程**

包含：

* LCD显示设备状态
* Touch按钮启动BLE扫描
* BLE扫描ICD/ICM
* RSSI显示
* WiFi连接
* SD保存日志

这个工程会比单纯Hello World更接近你的最终产品。

[1]: https://docs.m5stack.com/en/esp_idf/m5cores3/bsp?utm_source=chatgpt.com "m5-docs"
[2]: https://docs.m5stack.com/en/core/CoreS3?utm_source=chatgpt.com "m5-docs"


好的。我们下一步不要再找“完整成品工程”，而是建立一个**CoreS3-SE硬件验证工程**，它会逐步演进成你的最终 BLE Gateway Tester。

考虑你当前环境：

```
Ubuntu 22.04
ESP-IDF v5.5.5
M5Stack CoreS3 BSP 4.0.0
M5Stack CoreS3-SE
VS Code + ESP-IDF Extension
```

我们建立：

```
CoreS3-SE_Test/
│
├── main/
│   ├── main.c
│   ├── app_config.h
│   │
│   ├── bsp_test.c
│   ├── wifi_manager.c
│   ├── ble_scanner.c
│   ├── storage_manager.c
│   └── mqtt_manager.c
│
├── components/
│
├── CMakeLists.txt
├── sdkconfig
└── main/idf_component.yml
```

---

# 第一阶段：CoreS3-SE BSP测试工程

目标：

验证：

✅ ESP32-S3启动
✅ CoreS3 BSP加载
✅ LCD显示
✅ Touch工作
✅ SD卡可用
✅ RTC可访问

暂时不加入：

* BLE
* MQTT

避免一次引入太多变量。

---

# 1. 创建工程

进入你的工作目录：

```bash
cd ~/work/project/BLEGateway/firmware
```

创建：

```bash
idf.py create-project CoreS3_Test
```

进入：

```bash
cd CoreS3_Test
```

---

# 2. 添加BSP依赖

进入：

```bash
cd main
```

创建：

```bash
nano idf_component.yml
```

内容：

```yaml
dependencies:
  espressif/m5stack_core_s3:
    version: "4.0.0"
```

原因：

你当前：

```
ESP-IDF v5.5.5
```

满足：

```
m5stack_core_s3 4.0.0
requires ESP-IDF >=5.5.3
```

---

# 3. 设置芯片

回到工程：

```bash
cd ..
```

执行：

```bash
idf.py set-target esp32s3
```

---

# 4. 第一个测试程序

修改：

```
main/CoreS3_Test.c
```

内容：

```c
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "lvgl.h"

static const char *TAG = "CoreS3_TEST";


void app_main(void)
{
    printf("\n\n");
    printf("=========================\n");
    printf(" CoreS3-SE BSP TEST\n");
    printf("=========================\n");


    /*
     * 初始化显示
     */
    bsp_display_start();


    /*
     * 打开背光
     */
    bsp_display_backlight_on();


    /*
     * 创建LVGL文字
     */

    bsp_display_lock(0);


    lv_obj_t *label = lv_label_create(
        lv_scr_act()
    );


    lv_label_set_text(
        label,
        "CoreS3-SE\nBSP TEST\nOK"
    );


    lv_obj_center(label);


    bsp_display_unlock();


    while(1)
    {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );

        printf("CoreS3 running...\n");
    }
}
```

---

# 5. 编译

第一次：

```bash
idf.py build
```

如果成功：

应该：

```
Project build complete
```

---

# 6. 烧录

查看串口：

```bash
ls /dev/ttyACM*
```

例如：

```
/dev/ttyACM0
```

烧录：

```bash
idf.py -p /dev/ttyACM0 flash
```

---

# 7. 查看日志

```bash
idf.py -p /dev/ttyACM0 monitor
```

应该看到：

```
=========================
 CoreS3-SE BSP TEST
=========================

CoreS3 running...
```

屏幕：

```
CoreS3-SE
BSP TEST
OK
```

---

# 第二阶段：加入Touch测试

增加：

```
touch_task()
```

显示：

```
Touch:
X=xxx
Y=xxx
```

---

# 第三阶段：加入BLE Scanner

架构：

```
ble_scan_task

       |
       |
 NimBLE Scan

       |
       |
 Device Manager

       |
       |
 LCD显示
```

显示：

```
BLE Scanner

ICD001
RSSI:-55

ICM_TEST
RSSI:-68
```

---

# 第四阶段：加入你的业务逻辑

最终：

```
BLE ADV
 |
 |
过滤:
 ICD*
 ICM*
 |
 |
状态机

ONLINE
OFFLINE

 |
 |
SD Card

 |
 |
MQTT
 |
 |
Server
```

---

# 这里有一个重要修正

因为你使用的是 **CoreS3-SE**：

不要调用：

```c
bsp_camera_init()
bsp_imu_init()
bsp_magnetometer_init()
```

这些属于CoreS3，不属于SE。

M5Stack官方也明确说明：
CoreS3-SE与CoreS3存在硬件差异，Camera、Proximity、IMU、Magnetometer相关代码不兼容。([docs.m5stack.com](https://docs.m5stack.com/en/core/M5CoreS3%20SE?utm_source=chatgpt.com))

---

# 下一步

你现在可以先执行：

1. 创建 `CoreS3_Test`
2. 添加：

```yaml
espressif/m5stack_core_s3:
    version: "4.0.0"
```

3. `idf.py build`

如果编译失败，把：

```
idf.py build
```

完整日志发我。

下一步我们再加入 **BLE Scanner Task**，这一步就是你最终产品的核心。
