/**
 * @file     Power_Monitor.cpp
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Power Monitor Module Implementation
 */

#include "Power_Monitor.h"
#include "Display_Manager.h"
#include <WiFi.h>
#include "Wireless.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "Config_Manager.h"
#include "Network_Scanner.h"
#include <time.h>

// 声明主文件中的函数
extern void resetTimeScreenState();

// 声明外部常量引用
extern const int MAX_POWER_WATTS;
extern const int MAX_PORT_WATTS;
extern const char* DATA_URL;
extern const int REFRESH_INTERVAL;

#define LV_SPRINTF_CUSTOM 1

// 全局变量
PortInfo portInfos[MAX_PORTS];
static float totalPower = 0.0f;  // 内部静态变量，只在本模块中可见
bool dataError = false;  // 数据错误标志
static bool scanUIActive = false;

// 数据获取任务句柄
TaskHandle_t monitorTaskHandle = NULL;

// 数据队列
QueueHandle_t dataQueue = NULL;

// 界面状态管理
enum UIState {
    UI_POWER_MONITOR,
    UI_SCAN_SCREEN,
    UI_WIFI_ERROR,  // 添加WiFi错误界面状态
    UI_UNKNOWN
};
static UIState globalUIState = UI_UNKNOWN;

// 安全的界面切换函数
static void safeUISwitch(UIState newState) {
    if (globalUIState == newState) {
        return; // 已经是目标状态，无需切换
    }
    
    printf("[Monitor] Safe UI switch from %d to %d\n", globalUIState, newState);
    
    // 删除当前界面
    switch (globalUIState) {
        case UI_SCAN_SCREEN:
            DisplayManager::deleteScanScreen();
            break;
        case UI_WIFI_ERROR:
            DisplayManager::deleteWiFiErrorScreen();
            break;
        case UI_POWER_MONITOR:
            // Power monitor screen 通常不需要删除，会被覆盖
            break;
        default:
            break;
    }
    
    // 短暂延迟确保界面删除完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 创建新界面
    switch (newState) {
        case UI_POWER_MONITOR:
            DisplayManager::createPowerMonitorScreen();
            break;
        case UI_SCAN_SCREEN:
            DisplayManager::createScanScreen();
            DisplayManager::updateScanStatus("Starting mDNS scan for cp02...");
            break;
        case UI_WIFI_ERROR:
            DisplayManager::createWiFiErrorScreen();
            break;
        default:
            break;
    }
    
    globalUIState = newState;
    printf("[Monitor] UI switch completed\n");
}

// NTP服务器配置
const char* NTP_SERVER = "ntp.aliyun.com";  // 阿里云NTP服务器
const char* TZ_INFO = "CST-8";              // 中国时区
const long  GMT_OFFSET_SEC = 8 * 3600;      // 时区偏移（北京时间：GMT+8）
const int   DAYLIGHT_OFFSET_SEC = 0;        // 夏令时偏移（中国不使用夏令时）

// NTP同步间隔（30分钟）
const unsigned long NTP_SYNC_INTERVAL = 30 * 60 * 1000;
static unsigned long lastNTPSync = 0;

// NTP时间同步函数
void syncTimeWithNTP() {
    printf("[Time] Synchronizing time with NTP server...\n");
    
    // 配置时区
    setenv("TZ", TZ_INFO, 1);
    tzset();
    
    // 配置NTP服务器
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    // 等待时间同步
    int retry = 0;
    const int maxRetries = 5;
    time_t now = 0;
    struct tm timeinfo = { 0 };
    
    while (timeinfo.tm_year < (2024 - 1900) && retry < maxRetries) {
        printf("[Time] Waiting for NTP sync... (%d/%d)\n", retry + 1, maxRetries);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }
    
    if (timeinfo.tm_year >= (2024 - 1900)) {
        printf("[Time] Time synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
               timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        lastNTPSync = millis();
    } else {
        printf("[Time] NTP sync failed after %d attempts\n", maxRetries);
    }
}

// 检查是否需要同步时间
void checkAndSyncTime() {
    unsigned long currentMillis = millis();
    
    // 检查是否需要进行定期同步
    if (currentMillis - lastNTPSync >= NTP_SYNC_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            syncTimeWithNTP();
        }
    }
}

// 初始化电源监控
void PowerMonitor_Init() {
    // 初始化端口信息
    for (int i = 0; i < MAX_PORTS; i++) {
        portInfos[i].id = i;
        portInfos[i].state = 0;
        portInfos[i].fc_protocol = 0;
        portInfos[i].current = 0;
        portInfos[i].voltage = 0;
        portInfos[i].power = 0.0f;
    }
    
    // 设置端口名称
    portInfos[0].name = "A1";
    portInfos[1].name = "C1";
    portInfos[2].name = "C2";
    portInfos[3].name = "C3";
    portInfos[4].name = "C4";
    
    // 创建数据队列
    dataQueue = xQueueCreate(1, sizeof(PowerData));
    
    // 初始化界面状态并创建电源监控界面
    globalUIState = UI_UNKNOWN;
    safeUISwitch(UI_POWER_MONITOR);
    
    // 启动监控任务
    PowerMonitor_Start();
}

// 监控任务
void PowerMonitor_Task(void* parameter) {
    HTTPClient http;
    String payload;
    bool lastWiFiState = false;
    uint32_t wifiRetryTime = 0;
    const uint32_t WIFI_RETRY_INTERVAL = 5000;
    uint32_t lastScanTime = 0;
    const uint32_t SCAN_RETRY_INTERVAL = 6000; // 进一步缩短到6秒
    bool isScanning = false;
    uint32_t lastSuccessfulDataTime = 0;
    const uint32_t DATA_TIMEOUT = 8000; // 8秒数据超时
    uint32_t wifiDisconnectTime = 0;     // WiFi断开时间
    const uint32_t WIFI_ERROR_TIMEOUT = 12000; // 12秒后显示WiFi错误界面
    
    // 连续失败检测机制 - 优化触发策略
    uint32_t consecutiveFailures = 0;
    const uint32_t FAILURE_THRESHOLD = 2; // 减少到连续失败2次触发扫描
    uint32_t lastFailureTime = 0;
    
    while (true) {
        bool currentWiFiState = WiFi.status() == WL_CONNECTED;
        uint32_t currentTime = millis();
        
        // WiFi状态发生变化
        if (currentWiFiState != lastWiFiState) {
            if (currentWiFiState) {
                printf("[Monitor] WiFi connected\n");
                syncTimeWithNTP();
                // WiFi连接后，切换到电源监控界面
                safeUISwitch(UI_POWER_MONITOR);
                isScanning = false; // 重置扫描状态
                wifiDisconnectTime = 0; // 重置断开时间
                consecutiveFailures = 0; // 重置失败计数器
                
                // 重置时间切换状态，让主任务重新评估功率状态
                resetTimeScreenState();
                
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                printf("[Monitor] WiFi disconnected\n");
                dataError = true;
                wifiDisconnectTime = currentTime; // 记录断开时间
            }
            lastWiFiState = currentWiFiState;
        }
        
        // 检查并同步时间
        if (currentWiFiState) {
            checkAndSyncTime();
        }
        
        // 检查WiFi连接
        if (!currentWiFiState) {
            // 检查是否需要显示WiFi错误界面
            if (wifiDisconnectTime > 0 && 
                (currentTime - wifiDisconnectTime >= WIFI_ERROR_TIMEOUT) &&
                globalUIState != UI_WIFI_ERROR) {
                printf("[Monitor] WiFi disconnected for too long, showing error screen\n");
                safeUISwitch(UI_WIFI_ERROR);
            }
            
            if (currentTime - wifiRetryTime >= WIFI_RETRY_INTERVAL) {
                printf("[Monitor] Trying to reconnect WiFi...\n");
                WiFi.reconnect();
                wifiRetryTime = currentTime;
            }
            
            dataError = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // WiFi已连接，获取数据
        String url = ConfigManager::getMonitorUrl();
        
        http.begin(url);
        int httpCode = http.GET();
        
        if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
            payload = http.getString();
            totalPower = 0.0f;
            
            // 逐行解析数据
            int position = 0;
            while (position < payload.length()) {
                int lineEnd = payload.indexOf('\n', position);
                if (lineEnd == -1) lineEnd = payload.length();
                
                String line = payload.substring(position, lineEnd);
                position = lineEnd + 1;
                
                // 解析电流数据
                if (line.startsWith("ionbridge_port_current{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取电流值
                    int valueStart = line.indexOf("}") + 1;
                    int current = line.substring(valueStart).toInt();
                    
                    // 更新端口电流
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].current = current;
                    }
                }
                // 解析电压数据
                else if (line.startsWith("ionbridge_port_voltage{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取电压值
                    int valueStart = line.indexOf("}") + 1;
                    int voltage = line.substring(valueStart).toInt();
                    
                    // 更新端口电压
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].voltage = voltage;
                    }
                }
                // 解析状态数据
                else if (line.startsWith("ionbridge_port_state{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取状态值
                    int valueStart = line.indexOf("}") + 1;
                    int state = line.substring(valueStart).toInt();
                    
                    // 更新端口状态
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].state = state;
                    }
                }
                // 解析协议数据
                else if (line.startsWith("ionbridge_port_fc_protocol{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取协议值
                    int valueStart = line.indexOf("}") + 1;
                    int protocol = line.substring(valueStart).toInt();
                    
                    // 更新端口协议
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].fc_protocol = protocol;
                    }
                }
            }
            
            // 计算每个端口的功率
            for (int i = 0; i < MAX_PORTS; i++) {
                // 功率 = 电流(mA) * 电压(mV) / 1000000 (转换为W)
                portInfos[i].power = (portInfos[i].current * portInfos[i].voltage) / 1000000.0f;
                totalPower += portInfos[i].power;
            }
            
            // 数据获取成功，确保显示电源监控界面
            if (globalUIState != UI_POWER_MONITOR) {
                printf("[Monitor] Data received successfully, switching to power monitor\n");
                safeUISwitch(UI_POWER_MONITOR);
                
                // 重置时间切换状态，让主任务重新评估功率状态
                resetTimeScreenState();
            }
            
            // 更新UI
            if (DisplayManager::isPowerMonitorScreenActive()) {
                DisplayManager::updatePowerMonitorScreen();
            }
            
            dataError = false;
            isScanning = false; // 重置扫描状态
            lastSuccessfulDataTime = currentTime;
            consecutiveFailures = 0; // 重置连续失败计数器
        } else {
            dataError = true;
            printf("[Monitor] Failed to fetch data, HTTP code: %d\n", httpCode);
            
            // 增加失败计数器
            consecutiveFailures++;
            lastFailureTime = currentTime;
            
            // 判断是否是临时性错误
            bool isTemporaryError = false;
            if (httpCode == -1 ||  // 超时错误
                httpCode == HTTP_CODE_NOT_FOUND ||  // 404
                httpCode == HTTP_CODE_INTERNAL_SERVER_ERROR ||  // 500
                httpCode == HTTP_CODE_SERVICE_UNAVAILABLE ||  // 503
                httpCode == HTTP_CODE_BAD_GATEWAY ||  // 502
                httpCode == HTTP_CODE_GATEWAY_TIMEOUT) {  // 504
                isTemporaryError = true;
                printf("[Monitor] Detected temporary error (HTTP %d), consecutive failures: %d\n", httpCode, consecutiveFailures);
            } else {
                printf("[Monitor] Detected persistent error (HTTP %d), consecutive failures: %d\n", httpCode, consecutiveFailures);
            }
            
            // 只有在WiFi连接正常但数据获取失败时才进行扫描
            // 如果是WiFi错误状态，则不进行设备扫描
            if (globalUIState != UI_WIFI_ERROR) {
                // 检查是否需要开始扫描（需要连续失败多次，且达到时间间隔）
                bool shouldTriggerScan = false;
                
                if (isTemporaryError) {
                    // 临时错误需要更多次失败才触发扫描（4次）
                    shouldTriggerScan = (consecutiveFailures >= FAILURE_THRESHOLD * 2);
                } else {
                    // 非临时错误，达到失败阈值就触发扫描（2次）
                    shouldTriggerScan = (consecutiveFailures >= FAILURE_THRESHOLD);
                }
                
                if (shouldTriggerScan && (currentTime - lastScanTime >= SCAN_RETRY_INTERVAL)) {
                    printf("[Monitor] Triggering scan after %d consecutive failures (threshold: %d for %s errors)\n", 
                           consecutiveFailures, 
                           isTemporaryError ? FAILURE_THRESHOLD * 2 : FAILURE_THRESHOLD,
                           isTemporaryError ? "temporary" : "persistent");
                    // 只有在不是扫描界面时才切换到扫描界面
                    if (globalUIState != UI_SCAN_SCREEN) {
                        safeUISwitch(UI_SCAN_SCREEN);
                        DisplayManager::updateScanStatus("Starting mDNS scan for cp02...");
                        isScanning = true;
                    }
                    
                    printf("[Monitor] Trying to find new metrics server...\n");
                    
                    // 先快速检查一次原连接是否恢复
                    HTTPClient testHttp;
                    testHttp.begin(url);
                    testHttp.setTimeout(1000); // 1秒超时
                    int testCode = testHttp.GET();
                    
                    if (testCode > 0 && testCode == HTTP_CODE_OK) {
                        printf("[Monitor] Original connection restored!\n");
                        DisplayManager::updateScanStatus("Connection restored!");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        
                        testHttp.end();
                        safeUISwitch(UI_POWER_MONITOR);
                        isScanning = false;
                        consecutiveFailures = 0; // 重置失败计数器
                        
                        // 重置时间切换状态，让主任务重新评估功率状态
                        resetTimeScreenState();
                    } else {
                        testHttp.end();
                        
                        // 原连接仍然失败，尝试扫描新设备
                        String newUrl;
                        DisplayManager::updateScanStatus("Looking for cp02 device via mDNS...");
                        
                        if (NetworkScanner::findMetricsServer(newUrl, true)) {
                            printf("[Monitor] Found cp02 metrics server: %s\n", newUrl.c_str());
                            ConfigManager::saveMonitorUrl(newUrl.c_str());
                            
                            DisplayManager::updateScanStatus("cp02 found! Connecting...");
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            
                            safeUISwitch(UI_POWER_MONITOR);
                            isScanning = false;
                            consecutiveFailures = 0; // 重置失败计数器
                            
                            // 重置时间切换状态，让主任务重新评估功率状态
                            resetTimeScreenState();
                        } else {
                            DisplayManager::updateScanStatus("cp02 not found, will retry...");
                            isScanning = true;
                        }
                    }
                    
                    lastScanTime = currentTime;
                } else {
                    // 还未达到扫描阈值，显示等待信息
                    if (consecutiveFailures > 0) {
                        uint32_t neededFailures = isTemporaryError ? FAILURE_THRESHOLD * 2 : FAILURE_THRESHOLD;
                        if (consecutiveFailures < neededFailures) {
                            printf("[Monitor] Waiting before scan (%d/%d failures, %s error)\n", 
                                   consecutiveFailures, neededFailures, isTemporaryError ? "temporary" : "persistent");
                        }
                    }
                }
                
                if (globalUIState == UI_SCAN_SCREEN && isScanning) {
                    // 在扫描界面期间，定期检查原连接是否恢复
                    static uint32_t lastQuickCheck = 0;
                    if (currentTime - lastQuickCheck >= 3000) { // 每3秒快速检查一次
                        printf("[Monitor] Quick check during scan...\n");
                        HTTPClient quickHttp;
                        quickHttp.begin(url);
                        quickHttp.setTimeout(800); // 短超时
                        int quickCode = quickHttp.GET();
                        
                        if (quickCode > 0 && quickCode == HTTP_CODE_OK) {
                            printf("[Monitor] Original connection restored during scan!\n");
                            DisplayManager::updateScanStatus("Connection restored!");
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            
                            safeUISwitch(UI_POWER_MONITOR);
                            isScanning = false;
                            consecutiveFailures = 0; // 重置失败计数器
                            
                            // 重置时间切换状态，让主任务重新评估功率状态
                            resetTimeScreenState();
                        } else {
                            // 更新扫描状态显示
                            DisplayManager::updateScanStatus("Still looking for cp02...");
                        }
                        quickHttp.end();
                        lastQuickCheck = currentTime;
                    }
                }
            }
        }
        
        http.end();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// 启动监控任务
void PowerMonitor_Start() {
    if (monitorTaskHandle == NULL) {
        xTaskCreate(
            PowerMonitor_Task,    // 任务函数
            "MonitorTask",        // 任务名称
            16284,                 // 堆栈大小
            NULL,                 // 任务参数
            1,                    // 任务优先级
            &monitorTaskHandle    // 任务句柄
        );
    }
}

// 停止监控任务
void PowerMonitor_Stop() {
    if (monitorTaskHandle != NULL) {
        vTaskDelete(monitorTaskHandle);
        monitorTaskHandle = NULL;
    }
}

// 获取总功率
float PowerMonitor_GetTotalPower() {
    return totalPower;
}