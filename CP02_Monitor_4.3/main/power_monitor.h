/**
 * @file     power_monitor.h
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Power Monitor Module Header
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// 常量定义
#define MAX_PORTS 5         // 最大端口数量
#define DEFAULT_MAX_POWER_WATTS 100.0f  // 默认最大总功率
#define DEFAULT_MAX_PORT_WATTS 20.0f    // 默认每个端口最大功率
#define DEFAULT_REFRESH_INTERVAL 2000   // 默认刷新间隔 (ms)

// 端口信息结构体
typedef struct {
    int id;                 // 端口ID
    const char* name;       // 端口名称
    int state;              // 端口状态
    int fc_protocol;        // 快充协议
    int current;            // 电流 (mA)
    int voltage;            // 电压 (mV)
    float power;            // 功率 (W)
} port_info_t;

// 初始化电源监控
esp_err_t power_monitor_init(void);

// 创建电源显示UI
esp_err_t power_monitor_create_ui(void);

// 从网络获取数据
esp_err_t power_monitor_fetch_data(void);

// 解析数据
void power_monitor_parse_data(char* payload);

// 更新UI显示
void power_monitor_update_ui(void);

// 更新WiFi状态
void power_monitor_update_wifi_status(void);

// 设置数据URL
esp_err_t power_monitor_set_data_url(const char* url);

// 获取当前数据URL
const char* power_monitor_get_data_url(void);

// 设置刷新间隔
void power_monitor_set_refresh_interval(int interval_ms);

// 获取全局端口信息
port_info_t* power_monitor_get_port_info(void);

// 获取总功率
float power_monitor_get_total_power(void);

// 是否有数据错误
bool power_monitor_has_error(void);

// 添加设置界面回调
void power_monitor_on_settings_change(void);

// 获取主屏幕对象引用，供其他模块访问
lv_obj_t *get_main_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MONITOR_H */ 