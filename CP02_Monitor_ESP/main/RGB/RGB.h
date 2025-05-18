#pragma once

#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"

#define BLINK_GPIO 38

void RGB_Init(void);
void Set_RGB( uint8_t red_val, uint8_t green_val, uint8_t blue_val);
void RGB_Example(void);

// 添加类似于原CP02_Monitor项目的RGB_lamp功能
void RGB_Off(void);               // 关闭RGB灯
void RGB_Loop(int step_count);    // RGB循环动画
bool RGB_Update(void);            // 定期更新RGB状态