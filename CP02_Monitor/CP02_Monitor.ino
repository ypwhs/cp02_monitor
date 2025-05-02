/**
 ******************************************************************************
 * @file     LVGL_Arduino.ino
 * @author   YPW
 * @version  V1.0
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
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "Wireless.h"
// #include "RGB_lamp.h"
#include "Power_Monitor.h"

// 电源监控配置
const int MAX_POWER_WATTS = 160;    // 最大总功率 160W
const int MAX_PORT_WATTS = 140;     // 每个端口最大功率 140W
const char* DATA_URL = "http://192.168.1.19/metrics";  // 修改成你的小电拼IP，后面的 /metrics 保留不变
const int REFRESH_INTERVAL = 500;   // 刷新间隔 (ms)

// WiFi 配置
const char* WIFI_SSID = "Apple";          // 填写你的WiFi名称
const char* WIFI_PASSWORD = "88888888";   // 填写你的WiFi密码

void setup()
{
  // 初始化显示
  LCD_Init();
  Set_Backlight(90);
  Lvgl_Init();

  // 初始化WiFi连接
  WiFi_Init(WIFI_SSID, WIFI_PASSWORD);
  
  // 初始化功率监控
  PowerMonitor_Init();
}

void loop()
{
  Timer_Loop();
  // RGB_Lamp_Loop(2);
  delay(5);
}
