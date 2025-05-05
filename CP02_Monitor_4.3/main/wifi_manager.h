/**
 * @file     wifi_manager.h
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    WiFi Manager Header
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 最大SSID和密码长度
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define MAX_IP_LEN   16

// WiFi配置结构体
typedef struct {
    char ssid[MAX_SSID_LEN + 1];
    char password[MAX_PASS_LEN + 1];
    char device_ip[MAX_IP_LEN + 1]; // 小电拼设备IP
    bool auto_connect;
} wifi_config_t;

// WiFi状态
typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_GOT_IP,
    WIFI_STATUS_CONNECT_FAILED
} wifi_status_t;

// 初始化WiFi管理器
esp_err_t wifi_manager_init(void);

// 启动WiFi连接
esp_err_t wifi_manager_connect(void);

// 断开WiFi连接
esp_err_t wifi_manager_disconnect(void);

// 保存WiFi配置
esp_err_t wifi_manager_save_config(wifi_config_t *config);

// 加载WiFi配置
esp_err_t wifi_manager_load_config(wifi_config_t *config);

// 设置WiFi配置
esp_err_t wifi_manager_set_config(wifi_config_t *config);

// 获取WiFi配置
esp_err_t wifi_manager_get_config(wifi_config_t *config);

// 获取当前WiFi状态
wifi_status_t wifi_manager_get_status(void);

// 检查WiFi是否连接
bool wifi_manager_is_connected(void);

// 获取设备IP
esp_err_t wifi_manager_get_ip(char *ip, size_t len);

// 注册状态回调函数
typedef void (*wifi_status_cb_t)(wifi_status_t status);
esp_err_t wifi_manager_register_cb(wifi_status_cb_t callback);

// 全局变量 - 表示WiFi连接和IP状态
extern bool WIFI_Connection;
extern bool WIFI_GotIP;

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */ 