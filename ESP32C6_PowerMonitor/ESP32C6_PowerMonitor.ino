/**
 ******************************************************************************
 * @file     LVGL_Arduino.ino
 * @author   YPW
 * @version  V1.9.5
 * @date     2025-05-10
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

// 系统状态
bool systemInitialized = false;
bool displayInitialized = false;
bool powerMonitorInitialized = false;
bool lastRGBState = false;

// 屏幕切换相关
const unsigned long SCREEN_SWITCH_DELAY = 30000;  // 30秒延迟
const unsigned long POWER_CHECK_INTERVAL = 1000;  // 每秒检查一次功率

// RGB控制任务相关
TaskHandle_t rgbTaskHandle = NULL;
const uint32_t RGB_TASK_STACK_SIZE = 2048;
const UBaseType_t RGB_TASK_PRIORITY = 1;
const unsigned long RGB_UPDATE_INTERVAL = 20;     // RGB更新间隔 (ms)

// 屏幕和WiFi监控任务相关
TaskHandle_t screenWifiTaskHandle = NULL;
const uint32_t SCREEN_WIFI_TASK_STACK_SIZE = 4096;
const UBaseType_t SCREEN_WIFI_TASK_PRIORITY = 2;
const unsigned long SCREEN_WIFI_UPDATE_INTERVAL = 50;  // 屏幕和WiFi检查间隔 (ms)

// 添加状态变量
static bool isInTimeMode = false;  // 是否处于时间显示模式
static unsigned long lowPowerStartTime = 0;  // 低功率开始时间
static bool rgbTempDisabled = false;  // 用于跟踪RGB灯是否被临时禁用
const unsigned long LOW_POWER_DELAY = 30000;  // 低功率切换延迟（30秒）

// 公开函数：重置时间切换状态（供其他模块调用）
void resetTimeScreenState() {
    isInTimeMode = false;
    lowPowerStartTime = 0;
    printf("[Display] Time screen state reset by external module\n");
}

// LVGL任务相关
TaskHandle_t lvglTaskHandle = NULL;
const uint32_t LVGL_TASK_STACK_SIZE = 32768;  // 增加到32KB
const UBaseType_t LVGL_TASK_PRIORITY = 3;     // 提高优先级
const unsigned long LVGL_UPDATE_INTERVAL = 20;  // 20ms 更新间隔

// 电源监控配置
const int MAX_POWER_WATTS = 160;    // 最大总功率 160W
const int MAX_PORT_WATTS = 140;     // 每个端口最大功率 140W
const int REFRESH_INTERVAL = 500;   // 刷新间隔 (ms)

// 任务计时器
const unsigned long WIFI_CHECK_INTERVAL = 1000;   // WiFi状态检查间隔 (ms)

// 内存监控任务相关
TaskHandle_t memoryTaskHandle = NULL;
const uint32_t MEMORY_TASK_STACK_SIZE = 2048;
const UBaseType_t MEMORY_TASK_PRIORITY = 1;
const unsigned long MEMORY_CHECK_INTERVAL = 2000;  // 2秒检查一次内存

// RGB控制任务
void rgbControlTask(void* parameter) {
    printf("[RGB] Task started\n");
    unsigned long lastUpdate = 0;
    bool lastState = ConfigManager::isRGBEnabled() && !rgbTempDisabled;
    
    while(1) {
        unsigned long currentMillis = millis();
        bool currentState = ConfigManager::isRGBEnabled() && !rgbTempDisabled;
        
        // 检查RGB灯状态变化
        if (currentState != lastState) {
            if (!currentState) {
                RGB_Lamp_Off();
                printf("[RGB] RGB lamp disabled\n");
            } else {
                printf("[RGB] RGB lamp enabled\n");
            }
            lastState = currentState;
        }
        
        // 更新RGB灯效果
        if (currentState && (currentMillis - lastUpdate >= RGB_UPDATE_INTERVAL)) {
            RGB_Lamp_Loop(1);
            lastUpdate = currentMillis;
        }
        
        // 任务延时
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 屏幕和WiFi监控任务
void screenWifiMonitorTask(void* parameter) {
    printf("[ScreenWiFi] Task started\n");
    unsigned long lastWiFiCheck = 0;
    unsigned long lastPowerCheckTime = 0;
    
    while(1) {
        unsigned long currentMillis = millis();
        
        // 检查并更新屏幕显示
        if (powerMonitorInitialized && (currentMillis - lastPowerCheckTime >= POWER_CHECK_INTERVAL)) {
            lastPowerCheckTime = currentMillis;
            
            float totalPower = PowerMonitor_GetTotalPower();
            
            // 如果正在扫描或显示WiFi错误界面，不执行切换
            if (!DisplayManager::isScanScreenActive() && !DisplayManager::isWiFiErrorScreenActive()) {
                if (totalPower < 1.0) {
                    // 低功率状态
                    if (!isInTimeMode) {
                        // 如果还没开始计时，开始计时
                        if (lowPowerStartTime == 0) {
                            lowPowerStartTime = currentMillis;
                            printf("[Display] Low power detected (%.2fW), starting timer for time mode\n", totalPower);
                        }
                        // 检查是否达到切换条件（30秒）
                        else if (currentMillis - lowPowerStartTime >= LOW_POWER_DELAY) {
                            printf("[Display] Switching to time mode after %.1f seconds of low power\n", (currentMillis - lowPowerStartTime) / 1000.0);
                            DisplayManager::deletePowerMonitorScreen();
                            DisplayManager::createTimeScreen();
                            DisplayManager::updateTimeScreen();
                            isInTimeMode = true;
                            // 临时关闭RGB灯
                            if (!rgbTempDisabled && ConfigManager::isRGBEnabled()) {
                                printf("[RGB] Temporarily disabling RGB in time mode\n");
                                rgbTempDisabled = true;
                                RGB_Lamp_Off();
                            }
                        }
                    } else {
                        // 在时间显示模式下持续更新时间
                        DisplayManager::updateTimeScreen();
                    }
                } else if (totalPower >= 2.0) {
                    // 高功率状态，立即切换到电源监控
                    if (isInTimeMode) {
                        printf("[Display] High power detected (%.2fW), switching to power monitor mode\n", totalPower);
                        DisplayManager::deleteTimeScreen();
                        DisplayManager::createPowerMonitorScreen();
                        DisplayManager::updatePowerMonitorScreen();
                        isInTimeMode = false;
                        
                        // 恢复RGB灯到配置状态
                        if (rgbTempDisabled) {
                            rgbTempDisabled = false;
                            if (ConfigManager::isRGBEnabled()) {
                                printf("[RGB] Restoring RGB state in power monitor mode\n");
                            }
                        }
                    }
                    // 重置低功率计时器
                    lowPowerStartTime = 0;
                } else {
                    // 功率在1-2W之间，保持当前显示状态，只重置低功率计时
                    if (lowPowerStartTime != 0) {
                        printf("[Display] Power in middle range (%.2fW), resetting low power timer\n", totalPower);
                        lowPowerStartTime = 0;
                    }
                }
            }
        }
        
        // 定期检查WiFi状态
        if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            bool wifiReady = ConfigManager::isConfigured() && ConfigManager::isConnected();
            
            if (wifiReady && !powerMonitorInitialized) {
                printf("[Power] Initializing power monitor...\n");
                PowerMonitor_Init();
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
        
        // 任务延时
        vTaskDelay(pdMS_TO_TICKS(SCREEN_WIFI_UPDATE_INTERVAL));
    }
}

// LVGL处理任务
void lvglTask(void* parameter) {
    printf("[LVGL] Task started\n");
    unsigned long lastUpdate = 0;
    
    // 等待显示初始化完成
    while (!displayInitialized) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    while(1) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastUpdate >= LVGL_UPDATE_INTERVAL) {
            DisplayManager::handleLvglTask();
            lastUpdate = currentMillis;
        }
        // 增加任务延时
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// 内存监控任务
void memoryMonitorTask(void* parameter) {
    printf("[Memory] Task started\n");
    
    // 保存上一次的内存状态
    uint32_t lastFreeHeap = 0;
    uint32_t lastTotalHeap = 0;
    uint32_t lastMinFreeHeap = 0;
    uint32_t lastMaxAllocHeap = 0;
    float lastHeapUsagePercent = 0;
    
    // 首次运行标志
    bool firstRun = true;
    
    while(1) {
        // 获取当前内存信息
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        uint32_t minFreeHeap = ESP.getMinFreeHeap();
        uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
        float heapUsagePercent = ((float)(totalHeap - freeHeap) / totalHeap) * 100;
        
        // 检查是否有变化（考虑1KB的误差范围）
        bool hasChanged = firstRun ||
                         abs((int32_t)freeHeap - (int32_t)lastFreeHeap) > 1024 ||
                         abs((int32_t)totalHeap - (int32_t)lastTotalHeap) > 1024 ||
                         abs((int32_t)minFreeHeap - (int32_t)lastMinFreeHeap) > 1024 ||
                         abs((int32_t)maxAllocHeap - (int32_t)lastMaxAllocHeap) > 1024 ||
                         abs(heapUsagePercent - lastHeapUsagePercent) > 1.0;
        
        // 只在首次运行或有显著变化时输出
        if (hasChanged) {
            // 使用分隔线使输出更清晰
            printf("\n----------------------------------------\n");
            printf("系统内存状态更新:\n");
            printf("----------------------------------------\n");
            
            // 只在相关值发生变化时输出对应信息
            if (firstRun || abs((int32_t)totalHeap - (int32_t)lastTotalHeap) > 1024) {
                printf("总内存:     %7u 字节 (%.1f KB)\n", totalHeap, totalHeap/1024.0);
            }
            
            if (firstRun || abs((int32_t)freeHeap - (int32_t)lastFreeHeap) > 1024) {
                printf("可用内存:   %7u 字节 (%.1f KB)\n", freeHeap, freeHeap/1024.0);
                printf("已用内存:   %7u 字节 (%.1f KB)\n", totalHeap - freeHeap, (totalHeap - freeHeap)/1024.0);
            }
            
            if (firstRun || abs(heapUsagePercent - lastHeapUsagePercent) > 1.0) {
                printf("内存使用率: %7.1f%%\n", heapUsagePercent);
            }
            
            if (firstRun || abs((int32_t)maxAllocHeap - (int32_t)lastMaxAllocHeap) > 1024) {
                printf("最大可分配: %7u 字节 (%.1f KB)\n", maxAllocHeap, maxAllocHeap/1024.0);
            }
            
            if (firstRun || abs((int32_t)minFreeHeap - (int32_t)lastMinFreeHeap) > 1024) {
                printf("历史最低:   %7u 字节 (%.1f KB)\n", minFreeHeap, minFreeHeap/1024.0);
            }
            
            printf("----------------------------------------\n");
            
            // 添加内存使用预警
            if (heapUsagePercent > 80) {
                printf("⚠️ 警告: 内存使用率超过80%%，请注意！\n");
            } else if (freeHeap < 10240) { // 小于10KB时警告
                printf("⚠️ 警告: 可用内存低于10KB，请注意！\n");
            }
            
            // 更新上一次的状态
            lastFreeHeap = freeHeap;
            lastTotalHeap = totalHeap;
            lastMinFreeHeap = minFreeHeap;
            lastMaxAllocHeap = maxAllocHeap;
            lastHeapUsagePercent = heapUsagePercent;
            firstRun = false;
        }
        
        // 延时2秒
        vTaskDelay(pdMS_TO_TICKS(MEMORY_CHECK_INTERVAL));
    }
}

bool initializeSystem() {
    printf("\n[System] Starting system initialization...\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 初始化显示相关组件
    printf("[Display] Initializing display components...\n");
    LCD_Init();  // 直接调用，不检查返回值
    vTaskDelay(pdMS_TO_TICKS(100));
    
    Lvgl_Init();  // 直接调用，不检查返回值
    vTaskDelay(pdMS_TO_TICKS(100));
    
    DisplayManager::init();
    displayInitialized = true;
    printf("[Display] Display initialization complete\n");
    vTaskDelay(pdMS_TO_TICKS(100));

    // 初始化配置管理器
    printf("[Config] Starting configuration manager...\n");
    ConfigManager::begin();
    vTaskDelay(pdMS_TO_TICKS(100));

    // 初始化RGB灯
    printf("[RGB] Initializing RGB lamp...\n");
    RGB_Lamp_Init();  // 初始化RGB灯数据
    lastRGBState = ConfigManager::isRGBEnabled();
    if (!lastRGBState) {
        RGB_Lamp_Off();
        printf("[RGB] RGB lamp disabled\n");
    } else {
        printf("[RGB] RGB lamp enabled\n");
    }
    
    // 创建RGB控制任务
    xTaskCreate(
        rgbControlTask,          // 任务函数
        "RGBControlTask",        // 任务名称
        RGB_TASK_STACK_SIZE,     // 堆栈大小
        NULL,                    // 任务参数
        RGB_TASK_PRIORITY,       // 任务优先级
        &rgbTaskHandle           // 任务句柄
    );
    
    // 创建屏幕和WiFi监控任务
    xTaskCreate(
        screenWifiMonitorTask,      // 任务函数
        "ScreenWiFiMonitorTask",    // 任务名称
        SCREEN_WIFI_TASK_STACK_SIZE, // 堆栈大小
        NULL,                       // 任务参数
        SCREEN_WIFI_TASK_PRIORITY,  // 任务优先级
        &screenWifiTaskHandle       // 任务句柄
    );
    
    // 创建LVGL处理任务
    xTaskCreate(
        lvglTask,               // 任务函数
        "LVGLTask",            // 任务名称
        LVGL_TASK_STACK_SIZE,  // 堆栈大小
        NULL,                  // 任务参数
        LVGL_TASK_PRIORITY,    // 任务优先级
        &lvglTaskHandle        // 任务句柄
    );
/*
    // 创建内存监控任务
    xTaskCreate(
        memoryMonitorTask,         // 任务函数
        "MemoryMonitorTask",       // 任务名称
        MEMORY_TASK_STACK_SIZE,    // 堆栈大小
        NULL,                      // 任务参数
        MEMORY_TASK_PRIORITY,      // 任务优先级
        &memoryTaskHandle          // 任务句柄
    );
*/
    
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("[System] System initialization complete\n");
    return true;
}

void setup()
{
    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 执行系统初始化
    systemInitialized = initializeSystem();
    
    if (!systemInitialized) {
        printf("[System] System initialization failed, restarting...\n");
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP.restart();
    }
}

void loop()
{
    // 主循环中不再处理LVGL任务，改为短暂延时
    vTaskDelay(pdMS_TO_TICKS(100));
}
