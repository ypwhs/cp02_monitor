/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ST7789.h"
#include "Wireless.h"
#include "LVGL_Driver.h"
#include "Power_Monitor.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "CP02_MAIN";

// WiFi配置
const char* WIFI_SSID = "Apple";          // 填写你的WiFi名称
const char* WIFI_PASSWORD = "88888888";   // 填写你的WiFi密码

// 电源监控配置
const int MAX_POWER_WATTS = 160;    // 最大总功率 160W
const int MAX_PORT_WATTS = 140;     // 每个端口最大功率 140W
const char* DATA_URL = "http://192.168.1.19/metrics";  // 修改成你的小电拼IP
const int REFRESH_INTERVAL = 500;   // 刷新间隔 (ms)

void app_main(void)
{
    ESP_LOGI(TAG, "CP02 Monitor application starting...");
    
    // 连接WiFi - 此函数内部会确保WiFi初始化
    ESP_LOGI(TAG, "Connecting to WiFi...");
    WiFi_Connect(WIFI_SSID, WIFI_PASSWORD);
    
    // 初始化LCD显示器
    ESP_LOGI(TAG, "Initializing LCD display");
    LCD_Init();
    BK_Light(90);  // 设置背光亮度
    
    // 初始化LVGL
    ESP_LOGI(TAG, "Initializing LVGL");
    LVGL_Init();
    
    // 初始化功率监控
    ESP_LOGI(TAG, "Initializing Power Monitor");
    PowerMonitor_Init();

    #if CONFIG_PM_ENABLE
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
    esp_pm_config_t pm_config = {
            .max_freq_mhz = 240,
            .min_freq_mhz = 80,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true
#endif
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );
#endif // CONFIG_PM_ENABLE

    ESP_LOGI(TAG, "Initialization complete");
    // 主循环
    while (1) {
        // LVGL定时器处理函数，需要定期调用
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
