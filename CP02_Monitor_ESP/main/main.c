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
        ESP_LOGI(TAG, "发现有效IP地址: %s", ip);
        // 保存找到的有效IP
        IP_Scanner_SaveIP(ip);
        
        // 立即触发功率监控更新，确保功率监控使用最新IP
        extern void PowerMonitor_FetchData(void);
        ESP_LOGI(TAG, "在main中发现设备后立即触发功率监控更新");
        PowerMonitor_FetchData();
    } else {
        ESP_LOGD(TAG, "IP地址 %s 不可用或不是目标设备", ip);
    }
}

// 初始化IP扫描器并加载有效IP
static char* initialize_ip_scanner(void) {
    static char saved_ip[32] = {0};
    
    ESP_LOGI(TAG, "初始化IP扫描器...");
    
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

// 连接WiFi并等待连接稳定
static bool ConnectWiFi(const char* ssid, const char* password) {
    extern bool WIFI_Connection;
    extern bool WIFI_GotIP;
    int wait_count = 0;
    const int max_wait = 30; // 最多等待3秒，原来是10秒
    
    ESP_LOGI(TAG, "=====================");
    ESP_LOGI(TAG, "开始连接WiFi: %s", ssid);
    ESP_LOGI(TAG, "=====================");
    
    // 初始化并连接WiFi
    WiFi_Connect(ssid, password);
    
    ESP_LOGI(TAG, "等待WiFi连接并获取IP地址...");
    
    // 等待获取IP地址，100ms检查一次
    while (!WIFI_GotIP && wait_count < max_wait) {
        vTaskDelay(pdMS_TO_TICKS(100)); // 等待100ms
        wait_count++;
        if (wait_count % 10 == 0) { // 每秒只打印一次日志
            ESP_LOGI(TAG, "等待IP地址...(%d/%d) - WiFi状态: %s, IP状态: %s", 
                    wait_count, max_wait, 
                    WIFI_Connection ? "已连接" : "未连接", 
                    WIFI_GotIP ? "已获取" : "未获取");
        }
    }
    
    if (!WIFI_GotIP) {
        ESP_LOGE(TAG, "WiFi连接失败或未能获取IP地址");
        return false;
    }
    
    // 获取当前IP地址
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "设备IP地址: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "子网掩码: " IPSTR, IP2STR(&ip_info.netmask));
            ESP_LOGI(TAG, "网关地址: " IPSTR, IP2STR(&ip_info.gw));
        }
    }
    
    // 移除额外的稳定等待时间
    ESP_LOGI(TAG, "WiFi连接成功，网络就绪");
    return true;
}

// 验证IP地址有效性
static bool validate_ip(const char* ip) {
    if (!ip || !ip[0]) {
        ESP_LOGW(TAG, "IP地址为空，无法验证");
        return false;
    }
    
    ESP_LOGI(TAG, "正在验证IP地址有效性: %s", ip);
    
    // 检查保存的IP是否有效
    bool check_result = IP_Scanner_CheckIP(ip);
    
    if (check_result) {
        ESP_LOGI(TAG, "IP地址 %s 验证有效，可以使用", ip);
        return true;
    }
    
    // 仅在第一次验证失败时快速重试一次
    ESP_LOGI(TAG, "IP验证失败，快速重试一次...");
    vTaskDelay(pdMS_TO_TICKS(200)); // 等待200ms后重试
    check_result = IP_Scanner_CheckIP(ip);
    
    if (check_result) {
        ESP_LOGI(TAG, "重试验证成功，IP地址 %s 可用", ip);
        return true;
    }
    
    ESP_LOGW(TAG, "IP地址 %s 验证无效，需要重新扫描", ip);
    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "   CP02 Monitor application starting...     ");
    ESP_LOGI(TAG, "============================================");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS需要擦除并重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS初始化成功");
    
    // 初始化LCD显示器
    ESP_LOGI(TAG, "初始化LCD显示器...");
    LCD_Init();
    BK_Light(90);  // 设置背光亮度
    ESP_LOGI(TAG, "LCD显示器初始化成功，背光亮度设置为90%%");
    
    // 初始化LVGL
    ESP_LOGI(TAG, "初始化LVGL图形库...");
    LVGL_Init();
    ESP_LOGI(TAG, "LVGL图形库初始化成功");
    
    // 连接WiFi并等待连接稳定
    if (!ConnectWiFi(WIFI_SSID, WIFI_PASSWORD)) {
        ESP_LOGE(TAG, "WiFi连接失败，继续执行但网络功能可能不可用");
    }
    
    // 初始化IP扫描器并加载已保存的IP
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "    开始小电拼IP加载和验证流程              ");
    ESP_LOGI(TAG, "============================================");
    
    // 初始化IP扫描器
    char* saved_ip = initialize_ip_scanner();
    bool ip_valid = false;
    
    // 只有在WiFi连接成功后才进行IP验证
    extern bool WIFI_GotIP;
    if (WIFI_GotIP) {
        // 验证已保存的IP是否有效
        if (saved_ip && saved_ip[0]) {
            ESP_LOGI(TAG, "在NVS中发现保存的设备IP: %s，即将验证是否有效", saved_ip);
            ip_valid = validate_ip(saved_ip);
            ESP_LOGI(TAG, "验证结果: %s", ip_valid ? "有效" : "无效");
        } else {
            ESP_LOGI(TAG, "未在NVS中找到保存的设备IP");
        }
        
        // 如果没有有效IP，执行网络扫描
        if (!ip_valid) {
            // 获取本机IP地址所在网段
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            char current_base_ip[32] = {0};
            
            if (netif != NULL) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    // 提取网段，格式如"192.168.1."
                    char full_ip[16];
                    sprintf(full_ip, IPSTR, IP2STR(&ip_info.ip));
                    
                    // 查找最后一个点的位置
                    char *last_dot = strrchr(full_ip, '.');
                    if (last_dot) {
                        // 计算到最后一个点的长度
                        size_t prefix_len = last_dot - full_ip + 1;
                        // 复制网段部分（包括最后一个点）
                        strncpy(current_base_ip, full_ip, prefix_len);
                        current_base_ip[prefix_len] = '\0';
                    }
                    
                    ESP_LOGI(TAG, "当前设备IP网段: %s", current_base_ip);
                }
            }
            
            // 使用当前网段或默认网段
            const char* scan_base_ip = (current_base_ip[0] != 0) ? current_base_ip : base_ip;
            
            ESP_LOGI(TAG, "开始网络扫描，寻找小电拼设备，扫描网段: %s*", scan_base_ip);
            
            // 执行网络扫描，寻找有效设备
            esp_err_t scan_err = IP_Scanner_ScanNetwork(scan_base_ip, ip_scan_callback, true);
            if (scan_err != ESP_OK) {
                ESP_LOGE(TAG, "网络扫描失败，错误码: 0x%x", scan_err);
            } else {
                ESP_LOGI(TAG, "网络扫描完成");
            }
            
            // 扫描完成后重新加载IP
            ESP_LOGI(TAG, "加载扫描结果...");
            if (saved_ip) {
                memset(saved_ip, 0, 32); // 清空之前的IP
            }
            
            // 重新加载保存的IP
            if (IP_Scanner_LoadIP(saved_ip, 32)) {
                ESP_LOGI(TAG, "发现新IP: %s，验证中", saved_ip);
                ip_valid = validate_ip(saved_ip);
            } else {
                ESP_LOGW(TAG, "未发现有效IP，将使用默认URL");
            }
        }
    } else {
        ESP_LOGW(TAG, "未获取到IP地址，跳过IP验证和扫描步骤");
    }
    
    // 配置电源监控URL
    char full_url[64] = {0};
    if (saved_ip && saved_ip[0] && ip_valid) {
        snprintf(full_url, sizeof(full_url), "http://%s/metrics", saved_ip);
        ESP_LOGI(TAG, "使用小电拼设备IP: %s", saved_ip);
    } else {
        strcpy(full_url, DATA_URL);
        ESP_LOGI(TAG, "使用默认URL: %s", DATA_URL);
    }
    
    // 确保全局WiFi状态已正确设置（在ConnectWiFi函数中已设置）
    extern bool WIFI_Connection;
    
    // 设置一个环境变量，标明IP已经验证过，避免Power_Monitor中再次触发扫描
    extern bool IP_Valid_In_Main;
    IP_Valid_In_Main = saved_ip && saved_ip[0] && ip_valid;
    ESP_LOGI(TAG, "IP验证状态标志: %s", IP_Valid_In_Main ? "已验证" : "未验证");
    
    // 初始化功率监控模块
    ESP_LOGI(TAG, "初始化功率监控模块...");
    PowerMonitor_Init();
    ESP_LOGI(TAG, "功率监控模块初始化完成");
    
    #if CONFIG_PM_ENABLE
    // 配置动态频率调整
    esp_pm_config_t pm_config = {
            .max_freq_mhz = 240,
            .min_freq_mhz = 80,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true
#endif
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    #endif // CONFIG_PM_ENABLE

    ESP_LOGI(TAG, "初始化完成，进入主循环");
    
    // 主循环
    while (1) {
        // LVGL定时器处理函数，需要定期调用
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
