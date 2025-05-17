/**
 ******************************************************************************
 * @file     LVGL_Arduino.ino
 * @author   YPW
 * @version  V1.8
 * @date     2025-05-02
 * @brief    功率监控应用程序
 * @license  MIT
 * @copyright Copyright (c) 2025, YPW
 ******************************************************************************
 * 
 * 应用程序说明：实现CP-02电源监控显示功能
 *
 * 硬件资源和引脚分配：
 * 1. 显示器接口 --> 查看Display_ST7789.h配置
 * 2. 无线模块接口 --> 查看Wireless.h配置
 * 
 ******************************************************************************
 * 
 * 开发平台：ESP32
 * 开发文档：https://www.waveshare.net/wiki/ESP32-S3-LCD-1.47
 *
 ******************************************************************************
 */
#include <Arduino.h>
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "Wireless.h"
#include "RGB_lamp.h"
#include "Power_Monitor.h"
#include "Config_Manager.h"
#include "Display_Manager.h"

// 电源监控配置
const int MAX_POWER_WATTS = 160;    // 最大总功率 180W
const int MAX_PORT_WATTS = 140;     // 每个端口最大功率 140W
const char* DATA_URL = "http://192.168.1.19/metrics";  // 修改成你的小电拼IP，后面的 /metrics 保留不变
const int REFRESH_INTERVAL = 500;   // 刷新间隔 (ms)

// 任务计时器
unsigned long lastRGBUpdate = 0;
unsigned long lastPowerUpdate = 0;
unsigned long lastWiFiCheck = 0;
const unsigned long RGB_UPDATE_INTERVAL = 20;     // RGB更新间隔 (ms)
const unsigned long POWER_UPDATE_INTERVAL = 500;  // 功率监控更新间隔 (ms)
const unsigned long WIFI_CHECK_INTERVAL = 1000;   // WiFi状态检查间隔 (ms)

// 系统状态
bool systemInitialized = false;
bool displayInitialized = false;
bool powerMonitorInitialized = false;
bool lastRGBState = false;

// 系统初始化函数
bool initializeSystem() {
    printf("\n[System] Starting system initialization...\n");
    delay(100);

    // 初始化串口
    Serial.begin(115200);
    delay(100);
    
    // 初始化显示相关组件
    printf("[Display] Initializing display components...\n");
    LCD_Init();  // 直接调用，不检查返回值
    delay(100);
    
    Set_Backlight(66);
    delay(50);
    
    Lvgl_Init();  // 直接调用，不检查返回值
    delay(100);
    
    DisplayManager::init();
    displayInitialized = true;
    printf("[Display] Display initialization complete\n");
    delay(100);

    // 初始化配置管理器
    printf("[Config] Starting configuration manager...\n");
    ConfigManager::begin();
    delay(100);

    // 初始化RGB灯
    printf("[RGB] Initializing RGB lamp...\n");
    lastRGBState = ConfigManager::isRGBEnabled();
    if (!lastRGBState) {
        RGB_Lamp_Off();
        printf("[RGB] RGB lamp disabled\n");
    } else {
        printf("[RGB] RGB lamp enabled\n");
    }
    delay(100);

    printf("[System] System initialization complete\n");
    return true;
}

void setup()
{
    // 等待系统稳定
    delay(1000);
    
    // 执行系统初始化
    systemInitialized = initializeSystem();
    
    if (!systemInitialized) {
        printf("[System] System initialization failed, restarting...\n");
        delay(3000);
        ESP.restart();
    }
}

void loop()
{
    // 如果系统未初始化，不执行任何操作
    if (!systemInitialized) {
        delay(1000);
        return;
    }

    unsigned long currentMillis = millis();
    
    // 处理LVGL任务
    if (displayInitialized) {
        lv_timer_handler();
    }
    
    // 处理配置门户
    ConfigManager::handle();
    
    // 定期检查WiFi状态
    if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        bool wifiReady = ConfigManager::isConfigured() && ConfigManager::isConnected();
        
        if (wifiReady && !powerMonitorInitialized) {
            printf("[Power] Initializing power monitor...\n");
            PowerMonitor_Init();  // 直接调用，不检查返回值
            powerMonitorInitialized = true;
            printf("[Power] Power monitor initialized successfully\n");
            printf("[WiFi] Connected to: %s\n", ConfigManager::getSSID().c_str());
            printf("[WiFi] IP address: %s\n", WiFi.localIP().toString().c_str());
        } else if (!wifiReady && powerMonitorInitialized) {
            powerMonitorInitialized = false;
            printf("[System] Power monitor stopped due to WiFi disconnection\n");
            if (ConfigManager::isConfigured()) {
                printf("[WiFi] Connection lost, will retry automatically\n");
            }
        }
        
        lastWiFiCheck = currentMillis;
    }
    
    // 检查并更新RGB灯状态
    bool currentRGBState = ConfigManager::isRGBEnabled();
    if (currentRGBState != lastRGBState) {
        if (!currentRGBState) {
            RGB_Lamp_Off();
            printf("[RGB] RGB lamp disabled\n");
        } else {
            printf("[RGB] RGB lamp enabled\n");
        }
        lastRGBState = currentRGBState;
    }
    
    // 更新RGB灯效果
    if (currentMillis - lastRGBUpdate >= RGB_UPDATE_INTERVAL && currentRGBState) {
        RGB_Lamp_Loop(1);
        lastRGBUpdate = currentMillis;
    }
    
    // 给其他任务一些执行时间
    yield();
    delay(5);  // 短暂延时，防止看门狗复位
}
