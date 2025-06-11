#pragma once
#include <Arduino.h>
#include "lvgl.h"
#include "Power_Monitor.h"
#include "Display_ST7789.h"  // 添加显示器控制头文件
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class DisplayManager {
public:
    static void init();
    static void createAPScreen(const char* ssid, const char* ip);
    static void deleteAPScreen();
    static void showMonitorScreen();
    static bool isAPScreenActive();
    static void createWiFiErrorScreen();
    static void deleteWiFiErrorScreen();
    static bool isWiFiErrorScreenActive();
    
    // 时间显示相关函数
    static void createTimeScreen();
    static void deleteTimeScreen();
    static bool isTimeScreenActive();
    static void updateTimeScreen();
    
    // 电源监控相关
    static void createPowerMonitorScreen();
    static void deletePowerMonitorScreen();
    static void updatePowerMonitorScreen();
    static bool isPowerMonitorScreenActive();

    // 扫描界面相关函数
    static void createScanScreen();
    static void deleteScanScreen();
    static bool isScanScreenActive();
    static void updateScanStatus(const char* status);
    
    // LVGL 任务处理
    static void handleLvglTask();
    
    // 屏幕亮度设置
    static const uint8_t BRIGHTNESS_NORMAL = 42;  // 正常亮度
    static const uint8_t BRIGHTNESS_DIM = 8;      // 时间显示时的低亮度
    
    // 屏幕方向设置
    static void applyScreenRotation(int rotation);
    static int getCurrentRotation(); // 新增：获取当前屏幕方向
    
private:
    static lv_obj_t* mainScreen;  // 主屏幕
    static lv_obj_t* currentScreen;
    
    // AP配置UI组件
    static lv_obj_t* apContainer;
    static lv_obj_t* apTitle;
    static lv_obj_t* apContent;
    
    // WiFi错误UI组件
    static lv_obj_t* wifiErrorTitle;
    static lv_obj_t* wifiErrorMessage;
    static lv_obj_t* wifiErrorContainer;
    
    // 时间显示UI组件
    static lv_obj_t* timeContainer;
    static lv_obj_t* timeLabel;
    static lv_obj_t* dateLabel;  // 添加日期标签
    
    // 电源监控UI组件
    static lv_obj_t* powerMonitorContainer;
    static lv_obj_t* ui_title;
    static lv_obj_t* ui_total_label;
    static lv_obj_t* ui_port_labels[MAX_PORTS];
    static lv_obj_t* ui_power_values[MAX_PORTS];
    static lv_obj_t* ui_power_bars[MAX_PORTS];
    static lv_obj_t* ui_total_bar;
    static lv_obj_t* ui_wifi_status;
    
    // 扫描界面UI组件
    static lv_obj_t* scanContainer;
    static lv_obj_t* scanLabel;
    static lv_obj_t* scanStatus;
    
    // 状态标志
    static bool apScreenActive;
    static bool wifiErrorScreenActive;
    static bool timeScreenActive;
    static bool powerMonitorScreenActive;
    static bool scanScreenActive;
    static bool dataError;
    static unsigned long screenSwitchTime;
    static int currentRotation; // 新增：当前屏幕方向
    
    static SemaphoreHandle_t lvgl_mutex;  // LVGL互斥锁
    static const TickType_t LVGL_LOCK_TIMEOUT = portMAX_DELAY; // 永久等待
    
    // 私有方法
    static void createAPScreenContent(const char* ssid, const char* ip);
    static bool createPowerMonitorContent();
    static void setScreenBrightness(uint8_t brightness);
    static void hideAllContainers();  // 新增：隐藏所有容器
    static void createMainScreen();    // 新增：创建主屏幕
    
    // 互斥锁相关
    static void takeLvglLock();  // 改为void，因为会一直等待直到获取锁
    static void giveLvglLock();
    
    // 安全性检查函数
    static bool isValidScreenState();  // 检查屏幕状态一致性
    static void resetAllScreenStates();  // 紧急重置所有屏幕状态
}; 