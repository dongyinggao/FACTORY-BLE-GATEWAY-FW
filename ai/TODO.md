下一步建议不要再纠结版本，可以直接开始：

安装 ESP-IDF v5.5.5
VS Code ESP-IDF Extension 配置
创建 CoreS3 BSP Hello World
验证 LCD / WiFi / BLE Scanner

然后进入你的 BLE Gateway Tester 软件架构设计。


很好，现在进入 **Step 3：创建 CoreS3-SE BSP Hello World 工程并验证硬件环境**。

目标：

> **先确认 ESP-IDF v5.5.5 + VS Code + M5Stack CoreS3-SE 硬件链路完全正常。**

这一阶段不要急着写 BLE 网关业务，先完成：

✅ 烧录
✅ 串口通信
✅ LCD显示
✅ Touch测试
✅ WiFi基础验证
✅ BSP组件加载

确认环境后，再进入 BLE Scanner。

---

# Step 3.1 创建工程目录

建议建立自己的工作空间：

```bash
mkdir -p ~/workspace/m5stack
cd ~/workspace/m5stack
```

---

# Step 3.2 创建 ESP-IDF 工程

使用官方模板：

```bash
idf.py create-project coreS3_demo
```

进入：

```bash
cd coreS3_demo
```

目录：

```
coreS3_demo
│
├── CMakeLists.txt
├── sdkconfig
│
└── main
    ├── CMakeLists.txt
    └── coreS3_demo.c
```

---

# Step 3.3 添加 M5Stack CoreS3 BSP

ESP-IDF 5.x 推荐使用 Component Manager。

进入：

```bash
cd main
```

创建：

```bash
touch idf_component.yml
```

内容：

```yaml
dependencies:
  espressif/m5stack_core_s3:
    version: "^4.0.0"
```

最终：

```
main
│
├── coreS3_demo.c
├── CMakeLists.txt
└── idf_component.yml
```

---

# Step 3.4 修改main代码

编辑：

```
main/coreS3_demo.c
```

替换为：

```c
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "lvgl.h"


void app_main(void)
{
    printf("CoreS3-SE BSP Demo Start\n");


    /*
     * 初始化M5Stack CoreS3 BSP
     */
    bsp_display_start();


    /*
     * 获取LVGL屏幕
     */
    lv_obj_t *label = lv_label_create(
            lv_scr_act()
    );


    lv_label_set_text(
            label,
            "CoreS3-SE BLE Gateway\nHello World!"
    );


    lv_obj_center(label);


    while(1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

# Step 3.5 设置芯片型号

执行：

```bash
idf.py set-target esp32s3
```

输出：

```
Set Target to: esp32s3
```

---

# Step 3.6 编译

第一次会下载组件：

```bash
idf.py build
```

第一次耗时：

约：

5~15分钟

正常。

成功：

```
Project build complete
```

---

# Step 3.7 连接CoreS3-SE

USB连接：

CoreS3-SE

↓

USB-C

↓

Ubuntu虚拟机

检查：

```bash
ls /dev/ttyACM*
```

正常：

```
/dev/ttyACM0
```

如果没有：

```bash
ls /dev/ttyUSB*
```

---

# Step 3.8 添加串口权限

如果出现：

```
Permission denied
```

执行：

```bash
sudo usermod -aG dialout $USER
```

然后注销重新登录。

---

# Step 3.9 烧录

执行：

```bash
idf.py -p /dev/ttyACM0 flash
```

成功：

```
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

---

# Step 3.10 查看运行日志

执行：

```bash
idf.py -p /dev/ttyACM0 monitor
```

看到：

```
CoreS3-SE BSP Demo Start
```

并且屏幕显示：

```
CoreS3-SE BLE Gateway
Hello World!
```

说明：

🎉 环境成功。

退出：

```
Ctrl + ]
```

---

# Step 3.11 如果LCD没有显示

CoreS3-SE最常见问题：

## 问题1：BSP版本

检查：

```bash
idf.py list-dependencies
```

应该看到：

```
espressif/m5stack_core_s3
```

---

## 问题2：PSRAM

打开：

```bash
idf.py menuconfig
```

进入：

```
Component config
    ↓
ESP PSRAM
```

确认：

```
[X] Support for external, SPI-connected RAM
```

---

## 问题3：Flash模式

menuconfig：

```
Serial flasher config
```

设置：

```
Flash size:
16MB

Flash mode:
QIO
```

---

# Step 3完成标准

你的CoreS3-SE应该达到：

| 功能        | 状态 |
| --------- | -- |
| ESP-IDF编译 | ✅  |
| 烧录        | ✅  |
| 串口日志      | ✅  |
| PSRAM     | ✅  |
| LCD显示     | ✅  |
| LVGL运行    | ✅  |

---

# 下一步 Step 4

完成Hello World后，我们进入你的真实项目：

## Step 4：BLE Scanner Demo

目标：

扫描附近BLE设备：

输出：

```
BLE Scan Start

Device:
Name: ICD001
MAC:
RSSI:-55

Device:
Name: ICM_TEST
RSSI:-62
```

并实现：

* 过滤 ICD/ICM
* 记录首次发现时间
* RSSI统计
* 扫描周期控制

这个就是你蓝牙网关测试工装的核心。

---

你现在可以先执行 **Step 3.1~3.6**。

如果编译过程中出现错误，把：

1. `idf.py build`完整日志
2. `idf.py --version`
3. `idf.py list-dependencies`

发我，我们继续。你现在已经进入真正的CoreS3开发阶段。
