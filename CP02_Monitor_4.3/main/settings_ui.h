/**
 * @file     settings_ui.h
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Settings UI Module Header
 */

#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include <stdbool.h>
#include "lvgl.h"
#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化设置UI
void settings_ui_init(void);

// 创建设置UI
void settings_ui_create(void);

// 打开WiFi设置页面
void settings_ui_open_wifi_settings(void);

// 关闭WiFi设置页面
void settings_ui_close_wifi_settings(void);

// 打开IP设置页面
void settings_ui_open_ip_settings(void);

// 关闭IP设置页面
void settings_ui_close_ip_settings(void);

// 检查设置按钮是否被按下
void settings_ui_check_button(void);

// 设置UI回调接口 - 当设置改变时调用
typedef void (*settings_change_cb_t)(void);
void settings_ui_register_change_cb(settings_change_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_UI_H */ 