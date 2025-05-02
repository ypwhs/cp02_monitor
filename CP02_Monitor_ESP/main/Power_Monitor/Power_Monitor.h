/**
 * @file     Power_Monitor.h
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    电源监控模块
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "lvgl.h"
#include "esp_wifi.h"

// 定义端口最大数量
#define MAX_PORTS 5

// 全局常量定义
extern const int MAX_POWER_WATTS;    // 最大总功率 160W
extern const int MAX_PORT_WATTS;     // 每个端口最大功率 140W
extern const char* DATA_URL;         // API URL
extern const int REFRESH_INTERVAL;   // 刷新间隔 (ms)

// 端口状态结构体
typedef struct {
    uint8_t id;                // 端口ID
    uint8_t state;             // 端口状态
    uint8_t fc_protocol;       // 协议
    uint16_t current;          // 电流(mA)
    uint16_t voltage;          // 电压(mV)
    float power;               // 功率(W)
    const char* name;          // 端口名称
} PortInfo;

// 所有端口信息
extern PortInfo portInfos[MAX_PORTS];
extern float totalPower;
extern bool WIFI_Connection;

// 初始化功率监控
void PowerMonitor_Init(void);

// 创建功率显示界面
void PowerMonitor_CreateUI(void);

// 从网络获取数据
void PowerMonitor_FetchData(void);

// 解析数据
void PowerMonitor_ParseData(char* payload);

// 更新UI
void PowerMonitor_UpdateUI(void);

// 更新WiFi状态
void PowerMonitor_UpdateWiFiStatus(void);

// 定时器回调
void PowerMonitor_TimerCallback(lv_timer_t *timer);

#endif /* POWER_MONITOR_H */ 