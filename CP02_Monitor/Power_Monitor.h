/**
 * @file     Power_Monitor.h
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    电源监控模块
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <lvgl.h>

// 定义端口最大数量
#define MAX_PORTS 5

// 从主程序引用常量定义
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

// 数据结构体
struct PowerData {
    String payload;
    bool isValid;
};

// 所有端口信息
extern PortInfo portInfos[MAX_PORTS];
extern float totalPower;

// 数据获取任务句柄
extern TaskHandle_t monitorTaskHandle;  // 监控任务句柄

// 初始化功率监控
void PowerMonitor_Init();

// 创建功率显示界面
void PowerMonitor_CreateUI();

// 监控任务（数据获取、解析和UI更新）
void PowerMonitor_Task(void* parameter);

// 启动监控任务
void PowerMonitor_Start();

// 停止监控任务
void PowerMonitor_Stop();

// 更新UI
void PowerMonitor_UpdateUI();

// 更新WiFi状态
void PowerMonitor_UpdateWiFiStatus();

#endif /* POWER_MONITOR_H */ 