#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "nvs_flash.h" 
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"

#include <stdio.h>
#include <string.h>  // For memcpy
#include "esp_system.h"
#include "esp_netif.h"

extern uint16_t WIFI_NUM;
extern bool WIFI_Connection;  // WiFi连接状态
extern int8_t WiFi_RSSI;      // WiFi信号强度

void Wireless_Init(void);
uint16_t WIFI_Scan(void);

// WiFi连接功能
void WiFi_Connect(const char* ssid, const char* password);