#pragma once

#include "lvgl.h"
#include "esp_log.h"

// 显示管理功能
void display_manager_init(void);
void display_manager_create_ap_screen(const char* ssid, const char* ip);
void display_manager_delete_ap_screen(void);
void display_manager_show_monitor_screen(void);
bool display_manager_is_ap_screen_active(void);
void display_manager_create_wifi_error_screen(void);
void display_manager_delete_wifi_error_screen(void);
bool display_manager_is_wifi_error_screen_active(void); 