#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

class NetworkScanner {
public:
    // 使用mDNS扫描网络并返回找到的cp02设备URL
    static bool findMetricsServer(String& outUrl, bool printLog = false);
    
private:
    static bool testMetricsEndpoint(const String& ip, bool printLog);
    static bool findDeviceByMDNS(const char* hostname, String& outIP, bool printLog);
    static bool fallbackIPScan(String& outUrl, bool printLog);
}; 