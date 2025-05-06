/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ST7789.h"
#include "Wireless.h"
#include "LVGL_Driver.h"
#include "Power_Monitor.h"
#include "IP_Scanner.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "CP02_MAIN";

// WiFi配置
const char* WIFI_SSID = "Apple";          // 填写你的WiFi名称
const char* WIFI_PASSWORD = "88888888";   // 填写你的WiFi密码

// 电源监控配置
const int MAX_POWER_WATTS = 160;    // 最大总功率 160W
const int MAX_PORT_WATTS = 140;     // 每个端口最大功率 140W
const char* DATA_URL = "http://192.168.1.1/metrics";  // 修改成你的小电拼IP
const char base_ip[32] = "192.168.1.";  // 默认网段
const int REFRESH_INTERVAL = 500;   // 刷新间隔 (ms)

// IP扫描回调函数
static void ip_scan_callback(const char* ip, bool success) {
    if (success) {
        ESP_LOGI(TAG, "发现有效IP地址: %s，可用于通信", ip);
        // 在找到有效IP时打印出一个明显的标记
        ESP_LOGI(TAG, "============ 发现小电拼设备: %s ============", ip);
    } else {
        ESP_LOGD(TAG, "IP地址 %s 不可用或不是目标设备", ip);
    }
}

// 初始化IP扫描器并尝试加载保存的IP地址
static char* initialize_and_load_ip(void) {
    static char saved_ip[32] = {0};
    bool ip_valid = false;
    
    ESP_LOGI(TAG, "初始化IP扫描器并加载保存的IP地址...");
    
    // 初始化IP扫描器
    if (IP_Scanner_Init() != ESP_OK) {
        ESP_LOGE(TAG, "IP扫描器初始化失败");
        return NULL;
    }
    
    ESP_LOGI(TAG, "IP扫描器初始化成功，尝试从NVS加载保存的IP");
    
    // 尝试从NVS加载保存的IP
    if (IP_Scanner_LoadIP(saved_ip, sizeof(saved_ip))) {
        ESP_LOGI(TAG, "从NVS加载IP成功: %s", saved_ip);
        return saved_ip;
    } else {
        ESP_LOGW(TAG, "从NVS加载IP失败，未找到保存的IP或格式错误");
        return NULL;
    }
}

// 验证IP地址有效性
static bool validate_ip(const char* ip) {
    if (!ip || !ip[0]) {
        ESP_LOGW(TAG, "IP地址为空，无法验证");
        return false;
    }
    
    ESP_LOGI(TAG, "正在验证IP地址有效性: %s", ip);
    
    // 检查保存的IP是否仍然有效
    if (IP_Scanner_CheckIP(ip)) {
        ESP_LOGI(TAG, "IP地址 %s 验证有效，可以使用", ip);
        return true;
    } else {
        ESP_LOGW(TAG, "IP地址 %s 验证无效，需要重新扫描", ip);
        return false;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CP02 Monitor application starting...");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化IP扫描器，并加载之前保存的IP
    char* saved_ip = initialize_and_load_ip();
    
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
    
    // 如果有保存的IP，验证它是否有效
    if (saved_ip && saved_ip[0]) {
        ESP_LOGI(TAG, "有保存的设备IP: %s，将其用于电源监控", saved_ip);
        char full_url[64];
        snprintf(full_url, sizeof(full_url), "http://%s/metrics", saved_ip);
        ESP_LOGI(TAG, "初始化电源监控，使用URL: %s", full_url);
    } else {
        ESP_LOGI(TAG, "没有有效的设备IP，将使用默认URL: %s", DATA_URL);
    }
    
    // 初始化功率监控 - 内部会加载IP并自动在WiFi连接后进行扫描
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
