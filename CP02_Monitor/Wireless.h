#pragma once
#include "WiFi.h"

// WiFi状态变量
extern bool WIFI_Connection;
extern int8_t WiFi_RSSI;

// 初始化WiFi连接
void WiFi_Init(const char* ssid, const char* password);