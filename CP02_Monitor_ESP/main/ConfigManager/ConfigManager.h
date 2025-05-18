#pragma once

#include <string.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/dns.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

// 配置管理类
typedef struct {
    // 服务器和DNS相关
    httpd_handle_t server;
    char ap_ssid[32];
    char nvs_namespace[16];
    char nvs_ssid_key[16];
    char nvs_pass_key[16];
    char nvs_rgb_key[16];
    char nvs_monitor_url_key[16];
    
    // 状态标志
    bool configured;
    bool ap_started;
} config_manager_t;

extern config_manager_t config_manager;

// 全局标志
extern bool WIFI_GotIP;
extern bool WIFI_Connection;

// 初始化配置管理器
void config_manager_init(void);

// 处理配置页面请求
void config_manager_handle(void);

// 检查是否已配置
bool config_manager_is_configured(void);

// 重置配置
void config_manager_reset(void);

// 获取SSID
char* config_manager_get_ssid(void);

// 获取密码
char* config_manager_get_password(void);

// 保存配置
void config_manager_save(const char* ssid, const char* password);

// 启动配置门户
void config_manager_start_portal(void);

// 检查WiFi是否已连接
bool config_manager_is_connected(void);

// 检查RGB灯是否已启用
bool config_manager_is_rgb_enabled(void);

// 设置RGB灯启用状态
void config_manager_set_rgb_enabled(bool enabled);

// 更新显示
void config_manager_update_display(void);

// 获取监控服务器地址
char* config_manager_get_monitor_url(void);

// 保存监控服务器地址
void config_manager_save_monitor_url(const char* url);

// 从URL中提取IP地址
char* config_manager_extract_ip_from_url(const char* url); 