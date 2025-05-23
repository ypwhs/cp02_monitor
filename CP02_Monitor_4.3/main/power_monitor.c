/**
 * @file     power_monitor.c
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Power Monitor Module Implementation
 */

#include "power_monitor.h"
#include "wifi_manager.h"
#include "settings_ui.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "POWER_MONITOR";

// 全局常量定义
#define MAX_PORTS 5
extern const float MAX_POWER_WATTS;
extern const float MAX_PORT_WATTS;
extern const char DATA_URL[128];
extern const int REFRESH_INTERVAL;

// 本地可修改变量
static char local_data_url[128] = {0};
static int local_refresh_interval = 0;

// 全局变量
static port_info_t portInfos[MAX_PORTS];
static float totalPower = 0.0f;
static bool dataError = false;         // 数据错误标志

// HTTP客户端句柄
static esp_http_client_handle_t client = NULL;
static uint32_t last_data_fetch_time = 0;
static int consecutive_errors = 0;  // 新增：连续错误计数器

// UI组件
static lv_obj_t *ui_screen;
static lv_obj_t *ui_title;
static lv_obj_t *ui_wifi_status;
static lv_obj_t *ui_settings_btn;
static lv_obj_t *ui_port_table;       // 添加表格UI组件
static lv_obj_t *ui_port_labels[MAX_PORTS];
static lv_obj_t *ui_power_values[MAX_PORTS];
static lv_obj_t *ui_power_arcs[MAX_PORTS];
static lv_obj_t *ui_total_label;
static lv_obj_t *ui_total_arc;
static lv_timer_t *refresh_timer = NULL;
static lv_timer_t *wifi_timer = NULL;
static lv_timer_t *wifi_blink_timer = NULL;
static lv_timer_t *startup_anim_timer = NULL;

// WiFi图标闪烁控制
static bool wifi_icon_state = false;

// 启动动画控制
static uint8_t startup_anim_progress = 0;
static bool startup_animation_completed = false;

// 前向声明
static void wifi_status_timer_cb(lv_timer_t *timer);
static void startup_animation_cb(lv_timer_t *timer);
static void wifi_blink_timer_cb(lv_timer_t *timer);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static void power_monitor_timer_callback(lv_timer_t *timer);
static void settings_btn_event_cb(lv_event_t *e);

// 设置回调函数
static settings_change_cb_t settings_change_callback = NULL;

// 定义颜色宏，与原始Power_Monitor.c保持一致
#define COLOR_WHITE     lv_color_hex(0xFFFFFF)
#define COLOR_GREEN     lv_color_hex(0x00FF00)
#define COLOR_YELLOW    lv_color_hex(0xFFFF00)
#define COLOR_ORANGE    lv_color_hex(0xFF8800)
#define COLOR_RED       lv_color_hex(0xFF0000)
#define COLOR_PURPLE    lv_color_hex(0xFF00FF)
#define COLOR_GRAY      lv_color_hex(0x888888)

// 初始化电源监控
esp_err_t power_monitor_init(void)
{
    ESP_LOGI(TAG, "初始化电源监控模块...");
    
    // 初始化本地变量
    strncpy(local_data_url, DATA_URL, sizeof(local_data_url) - 1);
    local_data_url[sizeof(local_data_url) - 1] = '\0';
    local_refresh_interval = REFRESH_INTERVAL;
    
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
    
    // 初始化数据获取时间戳
    last_data_fetch_time = esp_log_timestamp();
    
    // 创建电源监控UI
    power_monitor_create_ui();
    
    // 启动动画 - 缩短动画间隔为5ms，加快启动速度
    startup_anim_progress = 0;
    startup_anim_timer = lv_timer_create(startup_animation_cb, 5, NULL);
    
    // 创建WiFi状态监控定时器 - 它会在启动动画完成后启动数据刷新定时器
    wifi_timer = lv_timer_create(wifi_status_timer_cb, 1000, NULL);
    
    ESP_LOGI(TAG, "电源监控模块已初始化");
    
    return ESP_OK;
}

// WiFi状态定时器回调
static void wifi_status_timer_cb(lv_timer_t *timer)
{
    power_monitor_update_wifi_status();
    
    // 如果启动动画已完成且刷新定时器还未创建，则创建刷新定时器
    // 无论WiFi是否连接，都创建定时器立即显示界面
    if (startup_animation_completed && refresh_timer == NULL) {
        ESP_LOGI(TAG, "启动动画已完成，开始显示界面");
        
        // 如果WiFi已连接并获取IP，则显示正常的日志信息
        if (WIFI_Connection && WIFI_GotIP) {
            ESP_LOGI(TAG, "WiFi已连接并获取IP，开始监控电源数据");
            ESP_LOGI(TAG, "从URL获取数据: %s", local_data_url);
        } else {
            ESP_LOGI(TAG, "WiFi未连接或未获取IP，界面将显示但无数据更新");
        }
        
        refresh_timer = lv_timer_create(power_monitor_timer_callback, local_refresh_interval, NULL);
        ESP_LOGI(TAG, "刷新定时器已创建，间隔: %d ms", local_refresh_interval);
    }
}

// 启动动画回调
static void startup_animation_cb(lv_timer_t *timer)
{
    // 更新进度值
    startup_anim_progress += 20;
    
    // 为所有进度弧设置进度
    for (int i = 0; i < MAX_PORTS; i++) {
        lv_arc_set_value(ui_power_arcs[i], startup_anim_progress);
    }
    
    // 当达到100%时停止动画
    if (startup_anim_progress >= 100) {
        lv_timer_del(startup_anim_timer);
        startup_anim_timer = NULL;
        
        // 重置所有进度弧为0
        for (int i = 0; i < MAX_PORTS; i++) {
            lv_arc_set_value(ui_power_arcs[i], 0);
        }
        
        // 设置动画完成标志
        startup_animation_completed = true;
        
        ESP_LOGI(TAG, "Startup animation completed");
    }
}

// WiFi状态图标闪烁计时器回调
static void wifi_blink_timer_cb(lv_timer_t *timer)
{
    // 只有当WiFi连接成功且没有数据错误时才闪烁
    if (WIFI_Connection && WIFI_GotIP && !dataError) {
        wifi_icon_state = !wifi_icon_state;
        if (wifi_icon_state) {
            // 绿色
            lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            // 白色
            lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else if (dataError) {
        // 数据错误时保持红色
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (!WIFI_Connection) {
        // WiFi断开连接时保持红色
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (WIFI_Connection && !WIFI_GotIP) {
        // 正在获取IP
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

// 定时器回调
static void power_monitor_timer_callback(lv_timer_t *timer)
{
    static uint32_t last_log_time = 0;
    uint32_t current_time = esp_log_timestamp();
    
    // 执行数据获取函数
    power_monitor_fetch_data();
    
    // 如果从上次获取数据已经过了太长时间，记录日志（仅用于调试）
    if (current_time - last_data_fetch_time > local_refresh_interval * 2 && 
        current_time - last_log_time > 1000) {  // 限制日志频率
        ESP_LOGW(TAG, "数据获取间隔超过预期: %d ms (预期: %d ms)", 
                 (int)(current_time - last_data_fetch_time), local_refresh_interval);
        last_log_time = current_time;
    }
}

// HTTP客户端事件处理
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;
    static int  output_len;
    static bool buffer_overflow_reported = false; // 避免重复报告缓冲区溢出
    
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        
        case HTTP_EVENT_ON_CONNECTED:
            // 在连接时重置缓冲区
            if (output_buffer != NULL) {
                free(output_buffer);
            }
            output_buffer = NULL;
            output_len = 0;
            buffer_overflow_reported = false; // 重置溢出报告标志
            break;
            
        case HTTP_EVENT_HEADER_SENT:
            break;
            
        case HTTP_EVENT_ON_HEADER:
            break;
            
        case HTTP_EVENT_ON_DATA:
            // 不管是否是分块传输，都收集数据
            if (evt->data_len > 0) {
                // 第一次收到数据，需要分配内存
                if (output_buffer == NULL) {
                    int initial_size = 8192;
                    output_buffer = (char *)malloc(initial_size);
                    if (output_buffer == NULL) {
                        ESP_LOGE(TAG, "无法为输出缓冲区分配内存");
                        return ESP_FAIL;
                    }
                    output_len = 0;
                }
                
                // 检查缓冲区是否足够大
                if (output_len + evt->data_len + 1 > 8192) {
                    // 数据太大，截断处理
                    if (!buffer_overflow_reported) {
                        ESP_LOGW(TAG, "数据过大(>8KB)，进行截断处理");
                        buffer_overflow_reported = true; // 设置标志，避免重复日志
                    }
                    
                    // 只复制能容纳的部分数据
                    int copy_len = 8192 - output_len - 1;
                    if (copy_len > 0) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                        output_len = 8192 - 1;
                    }
                } else {
                    // 复制数据到缓冲区
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                    output_len += evt->data_len;
                }
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL && output_len > 0) {
                output_buffer[output_len] = '\0';
                power_monitor_parse_data(output_buffer);
                free(output_buffer);
            } else {
                ESP_LOGW(TAG, "未收到数据");
            }
            output_buffer = NULL;
            output_len = 0;
            buffer_overflow_reported = false;
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer != NULL) {
                // 如果断开连接时已经收到数据，尝试处理
                if (output_len > 0) {
                    output_buffer[output_len] = '\0';
                    power_monitor_parse_data(output_buffer);
                }
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            buffer_overflow_reported = false;
            break;
            
        default:
            ESP_LOGW(TAG, "未知HTTP事件: %d", evt->event_id);
            break;
    }
    return ESP_OK;
}

// 设置数据URL
esp_err_t power_monitor_set_data_url(const char* url)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 复制到本地变量
    strncpy(local_data_url, url, sizeof(local_data_url) - 1);
    local_data_url[sizeof(local_data_url) - 1] = '\0';
    
    ESP_LOGI(TAG, "设置数据URL: %s", local_data_url);
    
    // 如果客户端已初始化，需要重新初始化
    if (client != NULL) {
        esp_http_client_cleanup(client);
        client = NULL;
    }
    
    return ESP_OK;
}

// 获取当前数据URL
const char* power_monitor_get_data_url(void)
{
    return local_data_url;
}

// 设置刷新间隔
void power_monitor_set_refresh_interval(int interval_ms)
{
    if (interval_ms < 500) {
        interval_ms = 500; // 最小刷新间隔限制
    }
    
    // 更新本地变量
    local_refresh_interval = interval_ms;
    ESP_LOGI(TAG, "设置刷新间隔: %d ms", local_refresh_interval);
    
    // 如果定时器已存在，更新间隔
    if (refresh_timer != NULL) {
        lv_timer_set_period(refresh_timer, local_refresh_interval);
    }
}

// 获取全局端口信息
port_info_t* power_monitor_get_port_info(void)
{
    return portInfos;
}

// 获取总功率
float power_monitor_get_total_power(void)
{
    return totalPower;
}

// 是否有数据错误
bool power_monitor_has_error(void)
{
    return dataError;
}

// 注册设置改变回调
void power_monitor_on_settings_change(void)
{
    ESP_LOGI(TAG, "设置已更改，更新配置");
    
    // 可以在这里添加任何设置更改后的处理逻辑
    
    // 如果有需要更新数据URL，可以在这里获取新的IP地址
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        char url[128];
        snprintf(url, sizeof(url), "http://%s/metrics", config.device_ip);
        power_monitor_set_data_url(url);
    }
}

// 获取基于电压的颜色 - 与原Power_Monitor.c保持一致的逻辑
static lv_color_t get_voltage_color(int voltage_mv)
{
    if (voltage_mv > 21000) {                           // 21V以上
        return lv_color_hex(0xFF00FF);                  // 紫色
    } else if (voltage_mv > 16000 && voltage_mv <= 21000) { // 16V~21V
        return lv_color_hex(0xFF0000);                  // 红色
    } else if (voltage_mv > 13000 && voltage_mv <= 16000) { // 13V~16V
        return lv_color_hex(0xFF8800);                  // 橙色
    } else if (voltage_mv > 10000 && voltage_mv <= 13000) { // 10V~13V
        return lv_color_hex(0x88FF00);                  // 黄色
    } else if (voltage_mv > 6000 && voltage_mv <= 10000) {  // 6V~10V
        return lv_color_hex(0x00FF00);                  // 绿色
    } else if (voltage_mv >= 0 && voltage_mv <= 6000) {     // 0V~6V
        return lv_color_hex(0x444444);                  // 黑色（白底黑字）
    } else {
        return lv_color_hex(0x888888);                  // 灰色（未识别电压）
    }
}

// 根据协议ID获取协议名称
static const char* get_fc_protocol_name(uint8_t protocol)
{
    switch (protocol) {
        case 0:  return "None";
        case 1:  return "QC2";
        case 2:  return "QC3";
        case 3:  return "QC3+";
        case 4:  return "SFCP";
        case 5:  return "AFC";
        case 6:  return "FCP";
        case 7:  return "SCP";
        case 8:  return "VOOC1.0";
        case 9:  return "VOOC4.0";
        case 10: return "SVOOC2.0";
        case 11: return "TFCP";
        case 12: return "UFCS";
        case 13: return "PE1";
        case 14: return "PE2";
        case 15: return "PD_Fix5V";
        case 16: return "PD_FixHV";
        case 17: return "PD_SPR_AVS";
        case 18: return "PD_PPS";
        case 19: return "PD_EPR_HV";
        case 20: return "PD_AVS";
        case 0xff: return "未充电";
        default: return "未知";
    }
}

// 创建电源显示UI
esp_err_t power_monitor_create_ui(void)
{
    ESP_LOGI(TAG, "创建电源监控UI");
    
    // 创建屏幕 - 改为亮色背景
    ui_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 计算布局参数 - 调整为800*480屏幕
    int screen_width = 800;  // 屏幕宽度
    int screen_height = 480; // 屏幕高度
    
    // 标题 - 字体加大到24
    ui_title = lv_label_create(ui_screen);
    lv_label_set_text(ui_title, "CP-02 Monitor");
    lv_obj_set_style_text_color(ui_title, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);  // 字号改为24
    lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 设置按钮 - 调整位置和大小
    ui_settings_btn = lv_btn_create(ui_screen);
    lv_obj_set_size(ui_settings_btn, 80, 40);
    lv_obj_align(ui_settings_btn, LV_ALIGN_TOP_RIGHT, -15, 10);
    lv_obj_set_style_bg_color(ui_settings_btn, lv_color_hex(0x2196F3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_settings_btn, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(ui_settings_btn);
    lv_label_set_text(btn_label, "设置");
    lv_obj_set_style_text_font(btn_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);  // 中文字体
    lv_obj_center(btn_label);
    
    // WiFi状态 - 与设置按钮对齐
    ui_wifi_status = lv_label_create(ui_screen);
    lv_label_set_text(ui_wifi_status, "WiFi");
    lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0x0000FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_wifi_status, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(ui_wifi_status, ui_settings_btn, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // 开始WiFi图标闪烁定时器
    wifi_blink_timer = lv_timer_create(wifi_blink_timer_cb, 500, NULL);
    
    // 创建一个大的容器，包含所有功率条 - 增加高度以容纳总功率显示
    lv_obj_t *power_container = lv_obj_create(ui_screen);
    lv_obj_set_size(power_container, screen_width - 40, 400); // 从350增加到400
    lv_obj_align(power_container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(power_container, lv_color_hex(0xFAFAFA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(power_container, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(power_container, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(power_container, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(power_container, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 为每个端口创建水平功率条和标签 - 调整尺寸以适应更大的屏幕
    int bar_height = 20;     // 条的高度
    int port_spacing = 55;   // 端口间距，从65减少到55以节省垂直空间
    
    // 定义自定义顺序 - A口挪到C1~C4后面
    int display_order[MAX_PORTS] = {1, 2, 3, 4, 0}; // 显示顺序：C1, C2, C3, C4, A
    
    for (int i = 0; i < MAX_PORTS; i++) {
        // 根据自定义顺序获取实际的端口索引
        int port_idx = display_order[i];
        
        // 创建端口标签
        ui_port_labels[i] = lv_label_create(power_container);
        lv_label_set_text(ui_port_labels[i], portInfos[port_idx].name);
        lv_obj_set_style_text_color(ui_port_labels[i], get_voltage_color(portInfos[port_idx].voltage), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_port_labels[i], &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(ui_port_labels[i], 20, i * port_spacing + 12);
        
        // 创建电压、电流、功率标签 - 给文字增加宽度
        char info_text[64];
        sprintf(info_text, "0.00V  0.00A  0.00W");
        ui_power_values[i] = lv_label_create(power_container);
        lv_label_set_text(ui_power_values[i], info_text);
        lv_obj_set_style_text_color(ui_power_values[i], get_voltage_color(portInfos[port_idx].voltage), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_power_values[i], &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(ui_power_values[i], 80, i * port_spacing + 12);
        
        // 创建功率条（彩色水平条）- 向左移动位置
        ui_power_arcs[i] = lv_bar_create(power_container);
        lv_obj_set_size(ui_power_arcs[i], 400, bar_height); // 保持宽度不变
        lv_obj_set_pos(ui_power_arcs[i], 330, i * port_spacing + 12); // 右移到容器右侧
        
        // 设置条的颜色
        lv_obj_set_style_bg_color(ui_power_arcs[i], lv_color_hex(0xCCCCCC), LV_PART_MAIN);  // 背景色使用深灰色
        lv_obj_set_style_bg_color(ui_power_arcs[i], lv_color_hex(0x88FF00), LV_PART_INDICATOR);  // 指示器颜色使用绿黄色
        
        // 启用水平渐变
        lv_obj_set_style_bg_grad_dir(ui_power_arcs[i], LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 设置渐变终止颜色为橙色
        lv_obj_set_style_bg_grad_color(ui_power_arcs[i], lv_color_hex(0xFF8800), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
        // 设置条的圆角
        lv_obj_set_style_radius(ui_power_arcs[i], bar_height/2, LV_PART_MAIN);
        lv_obj_set_style_radius(ui_power_arcs[i], bar_height/2, LV_PART_INDICATOR);
        
        // 设置初始值为0
        lv_bar_set_range(ui_power_arcs[i], 0, 100);
        lv_bar_set_value(ui_power_arcs[i], 0, LV_ANIM_OFF);
    }
    
    // 添加总功率显示 - 与单端口保持风格一致
    ui_total_label = lv_label_create(power_container);
    lv_label_set_text(ui_total_label, "总功率"); // 仅显示"总功率"，不显示数值
    lv_obj_set_style_text_color(ui_total_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_total_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT); // 与端口标签一致
    lv_obj_set_pos(ui_total_label, 20, MAX_PORTS * port_spacing + 12);
    
    // 创建总功率信息标签 - 与单端口电压电流功率标签风格一致
    lv_obj_t *ui_total_power_value = lv_label_create(power_container);
    lv_label_set_text(ui_total_power_value, "0.00W");
    lv_obj_set_style_text_color(ui_total_power_value, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_total_power_value, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT); // 与端口信息一致
    lv_obj_set_pos(ui_total_power_value, 80, MAX_PORTS * port_spacing + 12);
    
    // 创建总功率条 - 与单端口功率条保持一致的风格
    ui_total_arc = lv_bar_create(power_container);
    lv_obj_set_size(ui_total_arc, 400, bar_height); // 与单端口宽度一致
    lv_obj_set_pos(ui_total_arc, 330, MAX_PORTS * port_spacing + 12); // 与单端口位置一致
    
    // 设置总功率条的颜色 - 使用与单端口一致的颜色
    lv_obj_set_style_bg_color(ui_total_arc, lv_color_hex(0xCCCCCC), LV_PART_MAIN); // 与单端口一致
    lv_obj_set_style_bg_color(ui_total_arc, lv_color_hex(0x88FF00), LV_PART_INDICATOR); // 与单端口一致
    
    // 启用水平渐变
    lv_obj_set_style_bg_grad_dir(ui_total_arc, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 设置渐变终止颜色为橙色 - 与单端口一致
    lv_obj_set_style_bg_grad_color(ui_total_arc, lv_color_hex(0xFF8800), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 设置条的圆角
    lv_obj_set_style_radius(ui_total_arc, bar_height/2, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_total_arc, bar_height/2, LV_PART_INDICATOR);
    
    // 设置初始值为0
    lv_bar_set_range(ui_total_arc, 0, 100);
    lv_bar_set_value(ui_total_arc, 0, LV_ANIM_OFF);
    
    // 不再需要表格
    ui_port_table = NULL;
    
    // 加载屏幕
    lv_scr_load(ui_screen);
    
    return ESP_OK;
}

// 设置按钮回调函数
static void settings_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Settings button clicked");
    settings_ui_open_wifi_settings();
}

// 更新UI上的WiFi状态
void power_monitor_update_wifi_status(void)
{
    // 更新WiFi连接状态
    if (WIFI_Connection && WIFI_GotIP) {
        if (dataError) {
            // WiFi已连接但数据错误
            lv_obj_t * label = ui_wifi_status;
            lv_label_set_recolor(label, true);
            lv_label_set_text(label, "WiFi: #FF0000 数据错误#");
            lv_obj_set_style_text_font(label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);  // 中文字体
            ESP_LOGW(TAG, "WiFi connected but data error");
        } else {
            // WiFi已连接且数据正常 - 颜色由闪烁定时器控制
            lv_label_set_text(ui_wifi_status, "WiFi");
            // 不在这里设置颜色，由wifi_blink_timer_cb处理
        }
    } else if (WIFI_Connection && !WIFI_GotIP) {
        // WiFi已连接但未获取IP
        lv_label_set_text(ui_wifi_status, "WiFi: 获取IP中");
        lv_obj_set_style_text_font(ui_wifi_status, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);  // 中文字体
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        ESP_LOGW(TAG, "WiFi connected but no IP");
    } else {
        // WiFi断开连接
        lv_label_set_text(ui_wifi_status, "WiFi");
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        ESP_LOGW(TAG, "WiFi disconnected");
    }
    
    // 重新对齐，确保与设置按钮对齐
    lv_obj_align_to(ui_wifi_status, ui_settings_btn, LV_ALIGN_OUT_LEFT_MID, -10, 0);
}

// 从网络获取数据
esp_err_t power_monitor_fetch_data(void)
{
    static esp_http_client_handle_t client = NULL;
    uint32_t current_time = esp_log_timestamp();
    static uint32_t last_error_time = 0;
    
    // 确保请求间隔大于刷新间隔
    if (current_time - last_data_fetch_time < local_refresh_interval) {
        return ESP_OK; // 间隔不够，跳过本次请求
    }
    
    // 如果WiFi未连接或未获取IP地址，则不尝试获取数据，但不记录警告
    if (!WIFI_Connection || !WIFI_GotIP) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // 检查距离上次错误的时间，如果太短，可能网络还未恢复
    if (last_error_time > 0 && current_time - last_error_time < 1000) {
        ESP_LOGI(TAG, "上次错误后间隔太短，延迟请求");
        return ESP_ERR_NOT_FINISHED;
    }
    
    // 每次调用时检查客户端是否已初始化
    if (client == NULL) {
        // 创建HTTP客户端配置
        esp_http_client_config_t config = {
            .url = local_data_url,
            .event_handler = http_event_handler,
            .timeout_ms = 2000,        // 调整超时时间到2秒
            .buffer_size = 4096,       // 增加缓冲区大小
            .disable_auto_redirect = true,
            .skip_cert_common_name_check = true,
            .use_global_ca_store = false,
            .keep_alive_enable = false, // 每次请求后关闭连接
            .is_async = false,         // 同步请求
        };
        
        client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "HTTP客户端初始化失败");
            return ESP_FAIL;
        }
        
        // 设置请求头
        esp_http_client_set_method(client, HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Accept", "text/plain");
        esp_http_client_set_header(client, "User-Agent", "ESP32-HTTP-Client");
        esp_http_client_set_header(client, "Connection", "close"); 
        
        // 初始化后添加一些延迟来确保网络就绪
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // 记录请求开始时间
    last_data_fetch_time = current_time;
    
    // 执行HTTP请求
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        if (status_code == 200) {
            dataError = false;  // 重置数据错误标志
            consecutive_errors = 0;  // 重置连续错误计数
            last_error_time = 0;     // 清除上次错误时间
        } else {
            dataError = true;   // 设置数据错误标志
            consecutive_errors++;  // 增加连续错误计数
            last_error_time = current_time; // 记录错误时间
            ESP_LOGE(TAG, "HTTP GET请求失败，状态码: %d (连续错误: %d)", status_code, consecutive_errors);
        }
    } else {
        dataError = true;   // 设置数据错误标志
        consecutive_errors++;  // 增加连续错误计数
        last_error_time = current_time; // 记录错误时间
        ESP_LOGE(TAG, "HTTP GET请求失败: %s (错误码: %d, 连续错误: %d)", esp_err_to_name(err), err, consecutive_errors);
        
        // 每次错误后都重置HTTP客户端
        ESP_LOGI(TAG, "错误发生，重置HTTP客户端");
        esp_http_client_cleanup(client);
        client = NULL;
        
        // 添加额外延迟以便网络恢复
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }
    
    // 更新WiFi状态以反映数据错误
    power_monitor_update_wifi_status();
    
    // 让出更多CPU时间给其他任务
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    return err;
}

// 解析数据
void power_monitor_parse_data(char* payload)
{
    // 检查有效载荷
    if (payload == NULL || strlen(payload) == 0) {
        ESP_LOGE(TAG, "收到空的数据有效载荷");
        return;
    }
    
    // 重置总功率
    totalPower = 0.0f;
    
    // 创建副本进行解析，避免修改原始数据
    char* payload_copy = strdup(payload);
    if (payload_copy == NULL) {
        ESP_LOGE(TAG, "无法为数据创建副本，内存不足");
        return;
    }
    
    // 让出CPU时间，避免长时间解析数据导致UI卡顿
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 逐行解析数据
    char* saveptr = NULL;
    char* line = strtok_r(payload_copy, "\n", &saveptr);
    int lineCount = 0;
    
    while (line != NULL && lineCount < 1000) {  // 添加行数限制，防止无限循环
        lineCount++;
        
        // 每解析20行数据，让出一次CPU时间
        if (lineCount % 20 == 0) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        
        // 解析电流数据
        if (strncmp(line, "ionbridge_port_current{id=", 26) == 0) {
            // 提取端口ID
            char* idStart = strchr(line, '"') + 1;
            char* idEnd = strchr(idStart, '"');
            if (idEnd == NULL) {
                ESP_LOGW(TAG, "电流行格式无效: %s", line);
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电流值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            if (valueStart == NULL) {
                ESP_LOGW(TAG, "电流值格式无效: %s", line);
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            
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
            if (idEnd == NULL) {
                ESP_LOGW(TAG, "电压行格式无效: %s", line);
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电压值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            if (valueStart == NULL) {
                ESP_LOGW(TAG, "电压值格式无效: %s", line);
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            
            int voltage = atoi(valueStart);
            
            // 更新端口电压
            if (portId >= 0 && portId < MAX_PORTS) {
                portInfos[portId].voltage = voltage;
            }
        }
        
        // 获取下一行
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    // 释放副本内存
    free(payload_copy);
    
    // 再次让出CPU时间，避免计算功率导致UI卡顿
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 计算每个端口的功率
    for (int i = 0; i < MAX_PORTS; i++) {
        // 功率 = 电流(mA) * 电压(mV) / 1000000 (转换为W)
        portInfos[i].power = (portInfos[i].current * portInfos[i].voltage) / 1000000.0f;
        totalPower += portInfos[i].power;
    }
    
    // 添加一行日志显示所有端口的电源信息
    ESP_LOGI(TAG, "A=%.2fW(%dmA,%dmV), C1=%.2fW(%dmA,%dmV), C2=%.2fW(%dmA,%dmV), C3=%.2fW(%dmA,%dmV), C4=%.2fW(%dmA,%dmV), 总功率=%.2fW", 
             portInfos[0].power, portInfos[0].current, portInfos[0].voltage,
             portInfos[1].power, portInfos[1].current, portInfos[1].voltage,
             portInfos[2].power, portInfos[2].current, portInfos[2].voltage,
             portInfos[3].power, portInfos[3].current, portInfos[3].voltage,
             portInfos[4].power, portInfos[4].current, portInfos[4].voltage,
             totalPower);
    
    // 更新UI前添加短暂延迟
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 更新UI
    power_monitor_update_ui();
}

// 更新UI
void power_monitor_update_ui(void)
{
    // 定义临时字符串缓冲区
    char text_buf[64];
    
    // 在开始更新UI前添加短暂延迟
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 定义自定义顺序 - A口挪到C1~C4后面
    int display_order[MAX_PORTS] = {1, 2, 3, 4, 0}; // 显示顺序：C1, C2, C3, C4, A
    
    // 更新端口数据
    for (int i = 0; i < MAX_PORTS; i++) {
        // 根据自定义顺序获取实际的端口索引
        int port_idx = display_order[i];
        
        // 计算并格式化值
        float power_w = portInfos[port_idx].power;
        float voltage_v = portInfos[port_idx].voltage / 1000.0f; // 转换为V
        float current_a = portInfos[port_idx].current / 1000.0f; // 转换为A
        
        // 根据电压确定颜色
        lv_color_t color = get_voltage_color(portInfos[port_idx].voltage);
        
        // 更新端口标签颜色
        lv_obj_set_style_text_color(ui_port_labels[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 获取充电协议名称
        const char* protocol_name = get_fc_protocol_name(portInfos[port_idx].fc_protocol);
        
        // 格式化并更新信息文本，添加协议信息
        sprintf(text_buf, "%.1fV  %.1fA  %.2fW %s", voltage_v, current_a, power_w, protocol_name);
        lv_label_set_text(ui_power_values[i], text_buf);
        lv_obj_set_style_text_color(ui_power_values[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 更新功率条的值（最大功率的百分比）
        int percent = (int)((portInfos[port_idx].power / MAX_PORT_WATTS) * 100);
        // 确保非零功率至少显示一些进度
        if (portInfos[port_idx].power > 0 && percent == 0) {
            percent = 1;
        }
        // 限制最大值为100
        if (percent > 100) {
            percent = 100;
        }
        
        // 设置功率条的值
        lv_bar_set_value(ui_power_arcs[i], percent, LV_ANIM_OFF);
        
        // 确保端口标签文本不变
        lv_label_set_text(ui_port_labels[i], portInfos[port_idx].name);
        
        // 每更新一个端口后添加短暂延迟
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    
    // 更新总功率显示 - 只更新功率值文本
    if (ui_total_label != NULL) {
        // 找到总功率值标签 - 它紧跟在ui_total_label之后
        lv_obj_t *value_label = lv_obj_get_child(lv_obj_get_parent(ui_total_label), lv_obj_get_index(ui_total_label) + 1);
        if (value_label != NULL) {
            sprintf(text_buf, "%.2fW", totalPower);
            lv_label_set_text(value_label, text_buf);
        }
    }
    
    // 更新总功率条 - 使用MAX_POWER_WATTS作为最大值
    if (ui_total_arc != NULL) {
        int total_percent = (int)((totalPower / MAX_POWER_WATTS) * 100);
        // 确保非零功率至少显示一些进度
        if (totalPower > 0 && total_percent == 0) {
            total_percent = 1;
        }
        // 限制最大值为100
        if (total_percent > 100) {
            total_percent = 100;
        }
        
        // 设置总功率条的值
        lv_bar_set_value(ui_total_arc, total_percent, LV_ANIM_OFF);
    }
    
    // UI更新完成后添加短暂延迟
    vTaskDelay(1 / portTICK_PERIOD_MS);
}

// 暂停主程序定时器
void pause_main_timer(void)
{
    ESP_LOGI(TAG, "暂停主程序定时器");
    
    // 暂停数据刷新定时器
    if (refresh_timer != NULL) {
        lv_timer_pause(refresh_timer);
        ESP_LOGI(TAG, "数据刷新定时器已暂停");
    }
    
    // 暂停WiFi状态监测定时器
    if (wifi_timer != NULL) {
        lv_timer_pause(wifi_timer);
        ESP_LOGI(TAG, "WiFi状态定时器已暂停");
    }
    
    // 暂停WiFi闪烁定时器
    if (wifi_blink_timer != NULL) {
        lv_timer_pause(wifi_blink_timer);
        ESP_LOGI(TAG, "WiFi闪烁定时器已暂停");
    }
}

// 恢复主程序定时器
void resume_main_timer(void)
{
    ESP_LOGI(TAG, "恢复主程序定时器");
    
    // 恢复数据刷新定时器
    if (refresh_timer != NULL) {
        lv_timer_resume(refresh_timer);
        ESP_LOGI(TAG, "数据刷新定时器已恢复");
    }
    
    // 恢复WiFi状态监测定时器
    if (wifi_timer != NULL) {
        lv_timer_resume(wifi_timer);
        ESP_LOGI(TAG, "WiFi状态定时器已恢复");
    }
    
    // 恢复WiFi闪烁定时器
    if (wifi_blink_timer != NULL) {
        lv_timer_resume(wifi_blink_timer);
        ESP_LOGI(TAG, "WiFi闪烁定时器已恢复");
    }
}

// 获取主屏幕对象引用，供其他模块访问
lv_obj_t *get_main_screen(void)
{
    return ui_screen;
} 