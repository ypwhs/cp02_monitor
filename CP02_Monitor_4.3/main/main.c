/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "wifi_manager.h"
#include "power_monitor.h"
#include "settings_ui.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

// 设置更改回调函数
static void settings_change_callback(void)
{
    ESP_LOGI(TAG, "设置已更改，更新监控配置");
    power_monitor_on_settings_change();
}

// WiFi状态更改回调函数
static void wifi_status_callback(wifi_status_t status)
{
    ESP_LOGI(TAG, "WiFi状态已更改: %d", status);
    
    // 可以在这里添加状态变化的处理逻辑
    switch (status) {
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "WiFi已连接");
            break;
            
        case WIFI_STATUS_GOT_IP:
            ESP_LOGI(TAG, "WiFi已获取IP地址");
            break;
            
        case WIFI_STATUS_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi已断开连接");
            break;
            
        case WIFI_STATUS_CONNECTING:
            ESP_LOGI(TAG, "WiFi正在连接中");
            break;
            
        case WIFI_STATUS_CONNECT_FAILED:
            ESP_LOGI(TAG, "WiFi连接失败");
            break;
            
        default:
            break;
    }
}

void app_main()
{
    ESP_LOGI(TAG, "初始化CP02监控系统");
    
    // 初始化Waveshare ESP32-S3 RGB LCD
    waveshare_esp32_s3_rgb_lcd_init();
    
    // 锁定互斥量，因为LVGL API不是线程安全的
    if (lvgl_port_lock(-1)) {
        // 初始化WiFi管理器
        wifi_manager_init();
        
        // 注册WiFi状态变化回调
        wifi_manager_register_cb(wifi_status_callback);
        
        // 初始化设置UI
        settings_ui_init();
        
        // 注册设置改变回调
        settings_ui_register_change_cb(settings_change_callback);
        
        // 初始化电源监控
        power_monitor_init();
        
        // 释放互斥量
        lvgl_port_unlock();
    }
    
    ESP_LOGI(TAG, "CP02监控系统已启动");
}
