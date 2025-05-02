#include "Wireless.h"
#include <Arduino.h>

// WiFi状态变量
bool WIFI_Connection = false;
int8_t WiFi_RSSI = 0;

// 初始化WiFi连接
void WiFi_Init(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    
    // 等待WiFi连接
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        WIFI_Connection = true;
        WiFi_RSSI = WiFi.RSSI();
    } else {
        WIFI_Connection = false;
    }
}
