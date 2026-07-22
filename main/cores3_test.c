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