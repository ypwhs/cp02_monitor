/**
 * @file     wifi_manager.c
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

// 定义事件位
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_GOT_IP_BIT     BIT1
#define WIFI_FAIL_BIT       BIT2

// 全局变量
bool WIFI_Connection = false;
bool WIFI_GotIP = false;
static wifi_config_t current_config = {0};
static EventGroupHandle_t wifi_event_group = NULL;
static wifi_status_cb_t status_callback = NULL;
static wifi_status_t current_status = WIFI_STATUS_DISCONNECTED;

// NVS名称空间和键
#define WIFI_NVS_NAMESPACE  "wifi_config"
#define WIFI_NVS_KEY        "wifi_settings"

// 连接尝试次数
#define WIFI_CONNECT_RETRY_MAX  5
static int s_retry_num = 0;

// ESP-IDF事件处理函数
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA启动，准备连接...");
                current_status = WIFI_STATUS_CONNECTING;
                esp_wifi_connect();
                if (status_callback) status_callback(current_status);
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi已连接");
                WIFI_Connection = true;
                current_status = WIFI_STATUS_CONNECTED;
                if (status_callback) status_callback(current_status);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                WIFI_Connection = false;
                WIFI_GotIP = false;
                ESP_LOGI(TAG, "WiFi断开连接 (%d)", s_retry_num);
                if (s_retry_num < WIFI_CONNECT_RETRY_MAX) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "正在尝试重连...");
                } else {
                    ESP_LOGI(TAG, "WiFi连接失败");
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                    current_status = WIFI_STATUS_CONNECT_FAILED;
                    if (status_callback) status_callback(current_status);
                }
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "获取IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            WIFI_GotIP = true;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_GOT_IP_BIT);
            current_status = WIFI_STATUS_GOT_IP;
            if (status_callback) status_callback(current_status);
        }
    }
}

// 初始化WiFi管理器
esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "初始化WiFi管理器");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 创建WiFi事件组
    wifi_event_group = xEventGroupCreate();
    
    // 初始化TCP/IP协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 创建默认WiFi station
    esp_netif_create_default_wifi_sta();
    
    // 配置WiFi初始化
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 注册事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    
    // 配置WiFi为Station模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // 尝试加载之前的WiFi配置
    if (wifi_manager_load_config(&current_config) != ESP_OK) {
        // 如果加载失败，使用默认值
        strcpy(current_config.ssid, "");
        strcpy(current_config.password, "");
        strcpy(current_config.device_ip, "192.168.1.100");  // 默认设备IP
        current_config.auto_connect = false;
    }
    
    // 如果配置为自动连接，则启动WiFi
    if (current_config.auto_connect && strlen(current_config.ssid) > 0) {
        wifi_manager_connect();
    }
    
    return ESP_OK;
}

// 启动WiFi连接
esp_err_t wifi_manager_connect(void)
{
    ESP_LOGI(TAG, "连接到WiFi: %s", current_config.ssid);
    
    if (strlen(current_config.ssid) == 0) {
        ESP_LOGE(TAG, "未设置WiFi SSID，无法连接");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 配置WiFi参数
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, current_config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, current_config.password, sizeof(wifi_config.sta.password) - 1);
    
    // 配置高级参数
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // 设置WiFi配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 重置重试计数器
    s_retry_num = 0;
    
    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // 等待连接或失败
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    
    // 检查连接结果
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "已连接到WiFi网络: %s", current_config.ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "连接到WiFi网络失败: %s", current_config.ssid);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 断开WiFi连接
esp_err_t wifi_manager_disconnect(void)
{
    ESP_LOGI(TAG, "断开WiFi连接");
    
    // 停止WiFi
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        WIFI_Connection = false;
        WIFI_GotIP = false;
        current_status = WIFI_STATUS_DISCONNECTED;
        if (status_callback) status_callback(current_status);
    }
    
    return err;
}

// 保存WiFi配置
esp_err_t wifi_manager_save_config(wifi_config_t *config)
{
    ESP_LOGI(TAG, "保存WiFi配置");
    
    // 检查参数
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 打开NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(err));
        return err;
    }
    
    // 保存配置
    err = nvs_set_blob(nvs_handle, WIFI_NVS_KEY, config, sizeof(wifi_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存WiFi配置失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS更改失败: %s", esp_err_to_name(err));
    }
    
    // 关闭NVS
    nvs_close(nvs_handle);
    
    return err;
}

// 加载WiFi配置
esp_err_t wifi_manager_load_config(wifi_config_t *config)
{
    ESP_LOGI(TAG, "加载WiFi配置");
    
    // 检查参数
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 打开NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(err));
        return err;
    }
    
    // 获取数据大小
    size_t size = sizeof(wifi_config_t);
    err = nvs_get_blob(nvs_handle, WIFI_NVS_KEY, config, &size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "加载WiFi配置失败: %s", esp_err_to_name(err));
    }
    
    // 关闭NVS
    nvs_close(nvs_handle);
    
    return err;
}

// 设置WiFi配置
esp_err_t wifi_manager_set_config(wifi_config_t *config)
{
    ESP_LOGI(TAG, "设置WiFi配置: SSID=%s, Auto=%d", config->ssid, config->auto_connect);
    
    // 检查参数
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存配置
    memcpy(&current_config, config, sizeof(wifi_config_t));
    
    // 保存到NVS
    esp_err_t err = wifi_manager_save_config(&current_config);
    
    return err;
}

// 获取WiFi配置
esp_err_t wifi_manager_get_config(wifi_config_t *config)
{
    // 检查参数
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 复制当前配置
    memcpy(config, &current_config, sizeof(wifi_config_t));
    
    return ESP_OK;
}

// 获取当前WiFi状态
wifi_status_t wifi_manager_get_status(void)
{
    return current_status;
}

// 检查WiFi是否连接
bool wifi_manager_is_connected(void)
{
    return WIFI_Connection && WIFI_GotIP;
}

// 获取设备IP
esp_err_t wifi_manager_get_ip(char *ip, size_t len)
{
    if (ip == NULL || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!WIFI_GotIP) {
        strcpy(ip, "0.0.0.0");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // 获取IP信息
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        strcpy(ip, "0.0.0.0");
        return ESP_FAIL;
    }
    
    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(netif, &ip_info);
    if (err != ESP_OK) {
        strcpy(ip, "0.0.0.0");
        return err;
    }
    
    // 转换为字符串
    snprintf(ip, len, IPSTR, IP2STR(&ip_info.ip));
    
    return ESP_OK;
}

// 注册状态回调函数
esp_err_t wifi_manager_register_cb(wifi_status_cb_t callback)
{
    status_callback = callback;
    return ESP_OK;
} 