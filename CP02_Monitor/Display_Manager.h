#pragma once
#include <Arduino.h>
#include "lvgl.h"

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
    
private:
    static lv_obj_t* apScreen;
    static lv_obj_t* monitorScreen;
    static lv_obj_t* currentScreen;
    static lv_obj_t* wifiErrorScreen;
    static void createAPScreenContent(const char* ssid, const char* ip);
}; 