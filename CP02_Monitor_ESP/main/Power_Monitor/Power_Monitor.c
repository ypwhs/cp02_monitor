/**
 * @file     Power_Monitor.c
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Power Monitor Module Implementation
 */

#include "Power_Monitor.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

// 标签用于日志
static const char *TAG = "POWER_MONITOR";

// 声明外部常量引用
extern const int MAX_POWER_WATTS;    // 最大总功率
extern const int MAX_PORT_WATTS;     // 每个端口最大功率
extern const char* DATA_URL;         // API URL
extern const int REFRESH_INTERVAL;   // 刷新间隔 (ms)

#define LV_SPRINTF_CUSTOM 1

// 全局变量
PortInfo portInfos[MAX_PORTS];
float totalPower = 0.0f;
bool dataError = false;  // 数据错误标志
extern bool WIFI_Connection;

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

// 定义WiFi状态更新的计时器回调函数
static void wifi_status_timer_cb(lv_timer_t *timer) {
    PowerMonitor_UpdateWiFiStatus();
}

// ESP-IDF HTTP客户端事件处理
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;
    static int output_len;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (output_buffer == NULL) {
                    output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client) + 1);
                    output_len = 0;
                    if (output_buffer == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL) {
                output_buffer[output_len] = '\0';
                PowerMonitor_ParseData(output_buffer);
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

// 初始化电源监控
void PowerMonitor_Init(void) {
    ESP_LOGI(TAG, "Initializing Power Monitor...");
    
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
    
    // 创建UI
    PowerMonitor_CreateUI();
    
    // 创建定时器
    refresh_timer = lv_timer_create(PowerMonitor_TimerCallback, REFRESH_INTERVAL, NULL);
    wifi_timer = lv_timer_create(wifi_status_timer_cb, 1000, NULL);
    
    ESP_LOGI(TAG, "Power Monitor initialized, refresh interval: %d ms", REFRESH_INTERVAL);
}

// 创建电源显示UI
void PowerMonitor_CreateUI(void) {
    ESP_LOGI(TAG, "Creating Power Monitor UI");
    
    // 创建屏幕
    ui_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 标题
    ui_title = lv_label_create(ui_screen);
    lv_label_set_text(ui_title, "CP-02 Monitor");
    lv_obj_set_style_text_color(ui_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, 5);
    
    // WiFi状态
    ui_wifi_status = lv_label_create(ui_screen);
    lv_label_set_text(ui_wifi_status, "WiFi");
    lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
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
        
        // 功率进度条
        ui_power_bars[i] = lv_bar_create(ui_screen);
        lv_obj_set_size(ui_power_bars[i], 200, 15);
        lv_obj_align(ui_power_bars[i], LV_ALIGN_TOP_RIGHT, -10, start_y + i * item_height);
        lv_bar_set_range(ui_power_bars[i], 0, 100);
        lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x00FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    
    // 总功率标签
    ui_total_label = lv_label_create(ui_screen);
    lv_label_set_text(ui_total_label, "Total: 0W");
    lv_obj_set_style_text_color(ui_total_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_total_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_total_label, LV_ALIGN_TOP_LEFT, 10, start_y + MAX_PORTS * item_height + 5);
    
    // 总功率进度条
    ui_total_bar = lv_bar_create(ui_screen);
    lv_obj_set_size(ui_total_bar, 200, 15);
    lv_obj_align(ui_total_bar, LV_ALIGN_TOP_RIGHT, -10, start_y + MAX_PORTS * item_height + 5);
    lv_bar_set_range(ui_total_bar, 0, 100);
    lv_bar_set_value(ui_total_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(0x0088FF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 加载屏幕
    lv_scr_load(ui_screen);
    
    // 初始化WiFi状态
    PowerMonitor_UpdateWiFiStatus();
}

// 从网络获取数据
void PowerMonitor_FetchData(void) {
    esp_http_client_config_t config = {
        .url = DATA_URL,
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            dataError = false;  // 重置数据错误标志
        } else {
            dataError = true;   // 设置数据错误标志
            ESP_LOGE(TAG, "HTTP GET request failed: %d", status_code);
        }
    } else {
        dataError = true;   // 设置数据错误标志
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    
    // 更新WiFi状态以反映数据错误
    PowerMonitor_UpdateWiFiStatus();
}

// 解析数据
void PowerMonitor_ParseData(char* payload) {
    ESP_LOGI(TAG, "Parsing power data");
    
    // 重置总功率
    totalPower = 0.0f;
    
    // 逐行解析数据
    char* line = strtok(payload, "\n");
    int lineCount = 0;
    while (line != NULL) {
        lineCount++;
        
        // 解析电流数据
        if (strncmp(line, "ionbridge_port_current{id=", 26) == 0) {
            // 提取端口ID
            char* idStart = strchr(line, '"') + 1;
            char* idEnd = strchr(idStart, '"');
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电流值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            int current = atoi(valueStart);
            
            // 更新端口电流
            if (portId >= 0 && portId < MAX_PORTS) {
                portInfos[portId].current = current;
            }
        }
        // 解析电压数据
        else if (strncmp(line, "ionbridge_port_voltage{id=", 26) == 0) {
            // 提取端口ID
            char* idStart = strchr(line, '"') + 1;
            char* idEnd = strchr(idStart, '"');
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电压值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            int voltage = atoi(valueStart);
            
            // 更新端口电压
            if (portId >= 0 && portId < MAX_PORTS) {
                portInfos[portId].voltage = voltage;
            }
        }
        // 解析状态数据
        else if (strncmp(line, "ionbridge_port_state{id=", 24) == 0) {
            // 提取端口ID
            char* idStart = strchr(line, '"') + 1;
            char* idEnd = strchr(idStart, '"');
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取状态值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            int state = atoi(valueStart);
            
            // 更新端口状态
            if (portId >= 0 && portId < MAX_PORTS) {
                portInfos[portId].state = state;
            }
        }
        // 解析协议数据
        else if (strncmp(line, "ionbridge_port_fc_protocol{id=", 30) == 0) {
            // 提取端口ID
            char* idStart = strchr(line, '"') + 1;
            char* idEnd = strchr(idStart, '"');
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取协议值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            int protocol = atoi(valueStart);
            
            // 更新端口协议
            if (portId >= 0 && portId < MAX_PORTS) {
                portInfos[portId].fc_protocol = protocol;
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    // 计算每个端口的功率
    for (int i = 0; i < MAX_PORTS; i++) {
        // 功率 = 电流(mA) * 电压(mV) / 1000000 (转换为W)
        portInfos[i].power = (portInfos[i].current * portInfos[i].voltage) / 1000000.0f;
        totalPower += portInfos[i].power;
    }
    
    ESP_LOGI(TAG, "Total power: %.2fW", totalPower);
    
    // 更新UI
    PowerMonitor_UpdateUI();
}

// 更新UI
void PowerMonitor_UpdateUI(void) {
    // 更新每个端口的显示
    for (int i = 0; i < MAX_PORTS; i++) {
        // 更新功率值标签
        lv_label_set_text_fmt(ui_power_values[i], "%.2fW", portInfos[i].power);
        
        // 设置进度条颜色（基于功率值）
        uint32_t color;
        if (portInfos[i].power <= 15.0f) {
            color = 0x00FF00; // 绿色
        } else if (portInfos[i].power <= 30.0f) {
            color = 0xFFFF00; // 黄色
        } else {
            color = 0xFF0000; // 红色
        }
        lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(color), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 更新进度条值（最大功率的百分比）
        int percent = (int)((portInfos[i].power / MAX_PORT_WATTS) * 100);
        // 确保非零功率至少显示一些进度
        if (portInfos[i].power > 0 && percent == 0) {
            percent = 1;
        }
        lv_bar_set_value(ui_power_bars[i], percent, LV_ANIM_OFF);
    }
    
    // 更新总功率标签
    lv_label_set_text_fmt(ui_total_label, "Total: %.2fW", totalPower);
    
    // 更新总功率进度条
    int totalPercent = (int)((totalPower / MAX_POWER_WATTS) * 100);
    // 确保非零功率至少显示一些进度
    if (totalPower > 0 && totalPercent == 0) {
        totalPercent = 1;
    }
    lv_bar_set_value(ui_total_bar, totalPercent, LV_ANIM_OFF);
    
    // 设置总功率进度条颜色
    uint32_t totalColor;
    if (totalPower <= 60.0f) {
        totalColor = 0x0088FF; // 蓝色
    } else if (totalPower <= 100.0f) {
        totalColor = 0xFFAA00; // 橙色
    } else {
        totalColor = 0xFF0000; // 红色
    }
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(totalColor), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

// 更新UI上的WiFi状态
void PowerMonitor_UpdateWiFiStatus(void) {
    // 更新WiFi连接状态
    if (WIFI_Connection) {
        if (dataError) {
            // WiFi已连接但数据错误
            lv_obj_t * label = ui_wifi_status;
            lv_label_set_recolor(label, true);
            lv_label_set_text(label, "WiFi: #FF0000 DATA ERROR#");
            ESP_LOGW(TAG, "WiFi connected but data error occurred");
        } else {
            // WiFi已连接且数据正常
            lv_label_set_text(ui_wifi_status, "WiFi");
            lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        // WiFi断开连接
        lv_label_set_text(ui_wifi_status, "WiFi");
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        ESP_LOGW(TAG, "WiFi disconnected");
    }
}

// 定时器回调
void PowerMonitor_TimerCallback(lv_timer_t *timer) {
    PowerMonitor_FetchData();
} 