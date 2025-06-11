#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "Display_Manager.h"

// 配置管理类
class ConfigManager {
public:
    static void begin();
    static void handle();
    static bool isConfigured();
    static void resetConfig();
    static String getSSID();
    static String getPassword();
    static void saveConfig(const char* ssid, const char* password);
    static void startConfigPortal();
    static bool isConnected();
    static bool isRGBEnabled();
    static void setRGBEnabled(bool enabled);
    static void updateDisplay();
    static const char* getAPSSID() { return AP_SSID; }
    
    // 添加监控服务器地址相关函数
    static String getMonitorUrl();
    static void saveMonitorUrl(const char* url);
    
    // 添加屏幕方向相关函数
    static int getScreenRotation();
    static void setScreenRotation(int rotation);
    
private:
    static void setupAP();
    static void handleRoot();
    static void handleSave();
    static void handleNotFound();
    static void handleStatus();
    static void handleRGBControl();
    static void handleScreenRotation();
    static void handleReset();
    static String extractIPFromUrl(const String& url);
    
    // Web服务任务相关
    static void webServerTask(void* parameter);
    static TaskHandle_t webServerTaskHandle;
    static const uint32_t WEB_SERVER_STACK_SIZE = 8192;
    static const UBaseType_t WEB_SERVER_PRIORITY = 1;
    
    static WebServer server;
    static DNSServer dnsServer;
    static Preferences preferences;
    static bool configured;
    static bool apStarted;
    static const char* AP_SSID;
    static const char* NVS_NAMESPACE;
    static const char* NVS_SSID_KEY;
    static const char* NVS_PASS_KEY;
    static const char* NVS_RGB_KEY;
    static const char* NVS_MONITOR_URL_KEY;  // 添加监控URL的key
    static const char* NVS_SCREEN_ROTATION_KEY;  // 添加屏幕方向的key
}; 