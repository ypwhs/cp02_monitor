/**
 * @file     Power_Monitor.cpp
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Power Monitor Module Implementation
 */

#include "Power_Monitor.h"
#include <WiFi.h>
#include "Wireless.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "Config_Manager.h"

// 声明外部常量引用
extern const int MAX_POWER_WATTS;
extern const int MAX_PORT_WATTS;
extern const char* DATA_URL;
extern const int REFRESH_INTERVAL;

#define LV_SPRINTF_CUSTOM 1

// 全局变量
PortInfo portInfos[MAX_PORTS];
float totalPower = 0.0f;
bool dataError = false;  // 数据错误标志

// UI组件
static lv_obj_t *ui_screen;
static lv_obj_t *ui_title;
static lv_obj_t *ui_total_label;
static lv_obj_t *ui_port_labels[MAX_PORTS];
static lv_obj_t *ui_power_values[MAX_PORTS];
static lv_obj_t *ui_power_bars[MAX_PORTS];
static lv_obj_t *ui_total_bar;
static lv_obj_t *ui_wifi_status;
static lv_timer_t *refresh_timer = NULL;
static lv_timer_t *wifi_timer = NULL;

// 数据获取任务句柄
TaskHandle_t monitorTaskHandle = NULL;

// 数据队列
QueueHandle_t dataQueue = NULL;

// 初始化电源监控
void PowerMonitor_Init() {
    // 初始化端口信息
    for (int i = 0; i < MAX_PORTS; i++) {
        portInfos[i].id = i;
        portInfos[i].state = 0;
        portInfos[i].fc_protocol = 0;
        portInfos[i].current = 0;
        portInfos[i].voltage = 0;
        portInfos[i].power = 0.0f;
    }
    
    // 设置端口名称
    portInfos[0].name = "A";
    portInfos[1].name = "C1";
    portInfos[2].name = "C2";
    portInfos[3].name = "C3";
    portInfos[4].name = "C4";
    
    // 创建数据队列
    dataQueue = xQueueCreate(1, sizeof(PowerData));
    
    // 创建UI
    PowerMonitor_CreateUI();
    
    // 启动监控任务
    PowerMonitor_Start();
}

// 创建电源显示UI
void PowerMonitor_CreateUI() {
    // 创建屏幕
    ui_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 标题
    ui_title = lv_label_create(ui_screen);
    lv_label_set_text(ui_title, "Power Monitor");
    lv_obj_set_style_text_color(ui_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, 5);
    
    // WiFi状态
    ui_wifi_status = lv_label_create(ui_screen);
    lv_label_set_text(ui_wifi_status, "WiFi");
    lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_wifi_status, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    // 屏幕高度只有172像素，布局需要紧凑
    uint8_t start_y = 30;
    uint8_t item_height = 22;
    
    // 为每个端口创建UI元素
    for (int i = 0; i < MAX_PORTS; i++) {
        // 端口名称标签
        ui_port_labels[i] = lv_label_create(ui_screen);
        lv_label_set_text_fmt(ui_port_labels[i], "%s:", portInfos[i].name);
        lv_obj_set_style_text_color(ui_port_labels[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_port_labels[i], LV_ALIGN_TOP_LEFT, 10, start_y + i * item_height);
        
        // 功率值标签
        ui_power_values[i] = lv_label_create(ui_screen);
        lv_label_set_text(ui_power_values[i], "0.00W");
        lv_obj_set_style_text_color(ui_power_values[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_power_values[i], LV_ALIGN_TOP_LEFT, 45, start_y + i * item_height);
        
        
        // 功率进度条 - 带渐变色
        ui_power_bars[i] = lv_bar_create(ui_screen);
        lv_obj_set_size(ui_power_bars[i], 200, 15);
        lv_obj_align(ui_power_bars[i], LV_ALIGN_TOP_RIGHT, -10, start_y + i * item_height);
        lv_bar_set_range(ui_power_bars[i], 0, 100);
        lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);

         // 设置不同区间的颜色
        lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 设置进度条指示器颜色为绿黄色
        lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x88FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 启用水平渐变
        lv_obj_set_style_bg_grad_dir(ui_power_bars[i], LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 设置渐变终止颜色为红黄色
        lv_obj_set_style_bg_grad_color(ui_power_bars[i], lv_color_hex(0xFF8800), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
    }
    
    // 总功率标签
    ui_total_label = lv_label_create(ui_screen);
    lv_label_set_text(ui_total_label, "Total: 0W");
    lv_obj_set_style_text_color(ui_total_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_total_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_total_label, LV_ALIGN_TOP_LEFT, 10, start_y + MAX_PORTS * item_height + 5);
    
    // 总功率进度条 - 带渐变色
    ui_total_bar = lv_bar_create(ui_screen);
    lv_obj_set_size(ui_total_bar, 200, 15);
    lv_obj_align(ui_total_bar, LV_ALIGN_TOP_RIGHT, -10, start_y + MAX_PORTS * item_height + 5);
    lv_bar_set_range(ui_total_bar, 0, 100);
    lv_bar_set_value(ui_total_bar, 0, LV_ANIM_OFF);
    
    // 设置总功率进度条背景色
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 设置进度条指示器颜色为绿黄色
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(0x88FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 启用水平渐变
    lv_obj_set_style_bg_grad_dir(ui_total_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 设置渐变终止颜色为红黄色
    lv_obj_set_style_bg_grad_color(ui_total_bar, lv_color_hex(0xFF8800), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 加载屏幕
    lv_scr_load(ui_screen);
    
    // 初始化WiFi状态
    //PowerMonitor_UpdateWiFiStatus();
}

// 监控任务
void PowerMonitor_Task(void* parameter) {
    HTTPClient http;
    String payload;
    bool lastWiFiState = false;
    uint32_t wifiRetryTime = 0;
    const uint32_t WIFI_RETRY_INTERVAL = 5000; // 5秒重试一次WiFi连接
    
    while (true) {
        bool currentWiFiState = WiFi.status() == WL_CONNECTED;
        
        // WiFi状态发生变化
        if (currentWiFiState != lastWiFiState) {
            if (currentWiFiState) {
                printf("[Monitor] WiFi connected\n");
                // WiFi刚连接上，等待1秒让连接稳定
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                printf("[Monitor] WiFi disconnected\n");
                dataError = true;
            }
            lastWiFiState = currentWiFiState;
        }
        
        // 检查WiFi连接
        if (!currentWiFiState) {
            // 如果WiFi断开，定期尝试重连
            uint32_t currentTime = millis();
            if (currentTime - wifiRetryTime >= WIFI_RETRY_INTERVAL) {
                printf("[Monitor] Trying to reconnect WiFi...\n");
                WiFi.reconnect();
                wifiRetryTime = currentTime;
            }
            
            // 更新UI显示离线状态
            dataError = true;
            //PowerMonitor_UpdateWiFiStatus();
            vTaskDelay(pdMS_TO_TICKS(1000)); // WiFi断开时降低检查频率
            continue;
        }
        
        // WiFi已连接，获取数据
        String url = ConfigManager::getMonitorUrl();
        printf("[Monitor] Fetching data from: %s\n", url.c_str());
        
        http.begin(url);
        int httpCode = http.GET();
        
        // 检查HTTP响应代码
        if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
            payload = http.getString();
            
            // 重置总功率
            totalPower = 0.0f;
            
            // 逐行解析数据
            int position = 0;
            while (position < payload.length()) {
                int lineEnd = payload.indexOf('\n', position);
                if (lineEnd == -1) lineEnd = payload.length();
                
                String line = payload.substring(position, lineEnd);
                position = lineEnd + 1;
                
                // 解析电流数据
                if (line.startsWith("ionbridge_port_current{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取电流值
                    int valueStart = line.indexOf("}") + 1;
                    int current = line.substring(valueStart).toInt();
                    
                    // 更新端口电流
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].current = current;
                    }
                }
                // 解析电压数据
                else if (line.startsWith("ionbridge_port_voltage{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取电压值
                    int valueStart = line.indexOf("}") + 1;
                    int voltage = line.substring(valueStart).toInt();
                    
                    // 更新端口电压
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].voltage = voltage;
                    }
                }
                // 解析状态数据
                else if (line.startsWith("ionbridge_port_state{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取状态值
                    int valueStart = line.indexOf("}") + 1;
                    int state = line.substring(valueStart).toInt();
                    
                    // 更新端口状态
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].state = state;
                    }
                }
                // 解析协议数据
                else if (line.startsWith("ionbridge_port_fc_protocol{id=")) {
                    // 提取端口ID
                    int idStart = line.indexOf("\"") + 1;
                    int idEnd = line.indexOf("\"", idStart);
                    int portId = line.substring(idStart, idEnd).toInt();
                    
                    // 提取协议值
                    int valueStart = line.indexOf("}") + 1;
                    int protocol = line.substring(valueStart).toInt();
                    
                    // 更新端口协议
                    if (portId >= 0 && portId < MAX_PORTS) {
                        portInfos[portId].fc_protocol = protocol;
                    }
                }
            }
            
            // 计算每个端口的功率
            for (int i = 0; i < MAX_PORTS; i++) {
                // 功率 = 电流(mA) * 电压(mV) / 1000000 (转换为W)
                portInfos[i].power = (portInfos[i].current * portInfos[i].voltage) / 1000000.0f;
                totalPower += portInfos[i].power;
            }
            
            // 更新UI
            PowerMonitor_UpdateUI();
            dataError = false;
            printf("[Monitor] Data updated successfully\n");
        } else {
            dataError = true;
            printf("[Monitor] Failed to fetch data, HTTP code: %d\n", httpCode);
        }
        
        http.end();
        
        // 更新WiFi状态
        //PowerMonitor_UpdateWiFiStatus();
        
        // 延时200ms后再次获取数据
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// 启动监控任务
void PowerMonitor_Start() {
    if (monitorTaskHandle == NULL) {
        xTaskCreate(
            PowerMonitor_Task,    // 任务函数
            "MonitorTask",        // 任务名称
            8192,                 // 堆栈大小
            NULL,                 // 任务参数
            1,                    // 任务优先级
            &monitorTaskHandle    // 任务句柄
        );
    }
}

// 停止监控任务
void PowerMonitor_Stop() {
    if (monitorTaskHandle != NULL) {
        vTaskDelete(monitorTaskHandle);
        monitorTaskHandle = NULL;
    }
}

// 更新UI
void PowerMonitor_UpdateUI() {
    // 定义临时字符串缓冲区
    char text_buf[64];
    // 更新每个端口的显示
    for (int i = 0; i < MAX_PORTS; i++) {
        // 根据电压确定颜色代码
        const char* color_code;
        int voltage_mv = portInfos[i].voltage;
        
        // 设置电压对应的颜色代码，根据区间要求
        if (voltage_mv > 21000) {                        // 21V以上
            color_code = "#FF00FF";                      // 紫色
        } else if (voltage_mv > 16000 && voltage_mv <= 21000) { // 16V~21V
            color_code = "#FF0000";                      // 红色
        } else if (voltage_mv > 13000 && voltage_mv <= 16000) { // 13V~16V
            color_code = "#FF8800";                      // 橙色
        } else if (voltage_mv > 10000 && voltage_mv <= 13000) { // 10V~13V
            color_code = "#FFFF00";                      // 黄色
        } else if (voltage_mv > 6000 && voltage_mv <= 10000) {  // 6V~10V
            color_code = "#00FF00";                      // 绿色
        } else if (voltage_mv >= 0 && voltage_mv <= 6000) {     // 0V~6V
            color_code = "#FFFFFF";                      // 白色
        } else {
            color_code = "#888888";                      // 灰色（未识别电压）
        }

        // 启用标签的重着色功能
        lv_label_set_recolor(ui_power_values[i], true);

        // 更新功率值标签 - 将浮点数转换为整数显示，并添加颜色标记
        int power_int = (int)(portInfos[i].power * 100);

        // 使用sprintf格式化文本到缓冲区
        sprintf(text_buf, "%s %d.%02dW#", color_code, power_int / 100, power_int % 100);

        // 设置标签文本
        lv_label_set_text(ui_power_values[i], text_buf);
               
        // 更新进度条值（最大功率的百分比）
        int percent = (int)((portInfos[i].power / MAX_PORT_WATTS) * 100);
        // 确保非零功率至少显示一些进度
        if (portInfos[i].power > 0 && percent == 0) {
            percent = 1;
        }
        lv_bar_set_value(ui_power_bars[i], percent, LV_ANIM_OFF);
    }

// 更新总功率标签 - 将浮点数转换为整数显示
    int total_power_int = (int)(totalPower * 100);
    
    // 启用总功率标签的重着色功能
    lv_label_set_recolor(ui_total_label, true);

// 使用sprintf格式化总功率文本
    sprintf(text_buf, "Total: #FFFFFF %d.%02dW#", total_power_int / 100, total_power_int % 100);
    
    // 设置总功率标签
    lv_label_set_text(ui_total_label, text_buf);
    
    // 更新总功率进度条
    int totalPercent = (int)((totalPower / MAX_POWER_WATTS) * 100);
    // 确保非零功率至少显示一些进度
    if (totalPower > 0 && totalPercent == 0) {
        totalPercent = 1;
    }
    lv_bar_set_value(ui_total_bar, totalPercent, LV_ANIM_OFF);
}

// 更新UI上的WiFi状态
void PowerMonitor_UpdateWiFiStatus() {
    // 更新WiFi连接状态
    if (WIFI_Connection) {
        if (dataError) {
            // WiFi已连接但数据错误
            lv_label_set_text(ui_wifi_status, "WiFi: DATA ERROR");
            lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
            // 将"DATA ERROR"部分设为红色
            lv_obj_t * label = ui_wifi_status;
            lv_label_set_recolor(label, true);
            lv_label_set_text(label, "WiFi: #FF0000 DATA ERROR#");
        } else {
            // WiFi已连接且数据正常
            lv_label_set_text(ui_wifi_status, "WiFi");
            lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        // WiFi断开连接
        lv_label_set_text(ui_wifi_status, "WiFi");
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
} 