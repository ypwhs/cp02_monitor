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
            
            // 连接成功后添加短暂延迟
            vTaskDelay(1 / portTICK_PERIOD_MS);
            break;
            
        case HTTP_EVENT_HEADER_SENT:
            break;
            
        case HTTP_EVENT_ON_HEADER:
            // 处理头部时让出CPU时间
            vTaskDelay(1 / portTICK_PERIOD_MS);
            break;
            
        case HTTP_EVENT_ON_DATA:
            // 不管是否是分块传输，都收集数据
            if (evt->data_len > 0) {
                // 第一次收到数据，需要分配内存
                if (output_buffer == NULL) {
                    // 初始分配8KB缓冲区，足够大多数请求
                    int initial_size = 8192;
                    output_buffer = (char *)malloc(initial_size);
                    if (output_buffer == NULL) {
                        ESP_LOGE(TAG, "无法为输出缓冲区分配内存");
                        return ESP_FAIL;
                    }
                    output_len = 0;
                }
                
                // 检查是否需要扩展缓冲区
                size_t new_size = output_len + evt->data_len + 1; // +1 for null terminator
                output_buffer = (char *)realloc(output_buffer, new_size);
                if (output_buffer == NULL) {
                    ESP_LOGE(TAG, "无法为输出缓冲区重新分配内存");
                    return ESP_FAIL;
                }
                
                // 复制数据到缓冲区
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
                
                // 收到大量数据时让出CPU时间
                if (evt->data_len > 1024) {
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                }
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL && output_len > 0) {
                output_buffer[output_len] = '\0';
                
                // 解析数据前添加短暂延迟
                vTaskDelay(1 / portTICK_PERIOD_MS);
                
                power_monitor_parse_data(output_buffer);
                free(output_buffer);
            } else {
                ESP_LOGW(TAG, "未收到数据");
            }
            output_buffer = NULL;
            output_len = 0;
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer != NULL) {
                // 如果断开连接时已经收到数据，尝试处理
                if (output_len > 0) {
                    output_buffer[output_len] = '\0';
                    
                    // 在断开连接后处理数据前添加短暂延迟
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    
                    power_monitor_parse_data(output_buffer);
                }
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
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
    
    // 创建一个大的容器，包含所有功率条
    lv_obj_t *power_container = lv_obj_create(ui_screen);
    lv_obj_set_size(power_container, screen_width - 40, 350);
    lv_obj_align(power_container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(power_container, lv_color_hex(0xFAFAFA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(power_container, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(power_container, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(power_container, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(power_container, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 为每个端口创建水平功率条和标签 - 调整尺寸以适应更大的屏幕
    int bar_height = 20;     // 条的高度
    
    for (int i = 0; i < MAX_PORTS; i++) {
        // 创建端口标签
        ui_port_labels[i] = lv_label_create(power_container);
        lv_label_set_text(ui_port_labels[i], portInfos[i].name);
        lv_obj_set_style_text_color(ui_port_labels[i], get_voltage_color(portInfos[i].voltage), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_port_labels[i], &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(ui_port_labels[i], 20, i * 65 + 10);
        
        // 创建电压、电流、功率标签
        char info_text[64];
        sprintf(info_text, "0.00V  0.00A  0.00W");
        ui_power_values[i] = lv_label_create(power_container);
        lv_label_set_text(ui_power_values[i], info_text);
        lv_obj_set_style_text_color(ui_power_values[i], get_voltage_color(portInfos[i].voltage), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_power_values[i], &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(ui_power_values[i], 80, i * 65 + 12);
        
        // 创建功率条（彩色水平条）
        ui_power_arcs[i] = lv_bar_create(power_container);
        lv_obj_set_size(ui_power_arcs[i], 400, bar_height); // 减小宽度，避免与文字重叠
        lv_obj_set_pos(ui_power_arcs[i], 300, i * 65 + 12);
        
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
    
    // 移除总功率标签和弧形
    ui_total_label = NULL;
    ui_total_arc = NULL;
    
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
    
    // 确保请求间隔大于刷新间隔
    if (current_time - last_data_fetch_time < local_refresh_interval) {
        return ESP_OK; // 间隔不够，跳过本次请求
    }
    
    // 如果WiFi未连接或未获取IP地址，则不尝试获取数据，但不记录警告
    if (!WIFI_Connection || !WIFI_GotIP) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // 每次调用时检查客户端是否已初始化
    if (client == NULL) {
        // 创建HTTP客户端配置
        esp_http_client_config_t config = {
            .url = local_data_url,
            .event_handler = http_event_handler,
            .timeout_ms = 5000,  // 增加超时时间到5秒
            .buffer_size = 4096,
            .disable_auto_redirect = true, // 禁用自动重定向
            .skip_cert_common_name_check = true, // 跳过证书检查
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
        
        // 初始化HTTP客户端可能耗时，添加短暂延迟
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    
    // 记录请求开始时间
    last_data_fetch_time = current_time;
    
    // 在HTTP请求前添加短暂延迟，让UI任务有时间处理事件
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 执行HTTP请求
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        if (status_code == 200) {
            dataError = false;  // 重置数据错误标志
        } else {
            dataError = true;   // 设置数据错误标志
            ESP_LOGE(TAG, "HTTP GET请求失败，状态码: %d", status_code);
        }
    } else {
        dataError = true;   // 设置数据错误标志
        ESP_LOGE(TAG, "HTTP GET请求失败: %s (错误码: %d)", esp_err_to_name(err), err);
        
        // 如果是连接错误，不立即释放客户端，而是执行重试策略
        if (err == ESP_ERR_HTTP_FETCH_HEADER || err == ESP_ERR_HTTP_CONNECTING) {
            ESP_LOGI(TAG, "HTTP连接失败，将在下次尝试重连");
            // 仅在多次失败后才重置客户端
            static int retry_count = 0;
            retry_count++;
            
            if (retry_count > 5) {
                ESP_LOGI(TAG, "多次连接失败，重置HTTP客户端");
                esp_http_client_cleanup(client);
                client = NULL;
                retry_count = 0;
            }
        } else {
            // 其他错误，清理并重置客户端
            ESP_LOGI(TAG, "HTTP请求错误，重置客户端");
            esp_http_client_cleanup(client);
            client = NULL;
        }
    }
    
    // 更新WiFi状态以反映数据错误
    power_monitor_update_wifi_status();
    
    // 让出CPU时间
    vTaskDelay(1);
    
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
    
    // 让出CPU时间，避免长时间解析数据导致UI卡顿
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 逐行解析数据
    char* line = strtok(payload, "\n");
    int lineCount = 0;
    while (line != NULL) {
        lineCount++;
        
        // 每解析10行数据，让出一次CPU时间
        if (lineCount % 10 == 0) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        
        // 解析电流数据
        if (strncmp(line, "ionbridge_port_current{id=", 26) == 0) {
            // 提取端口ID
            char* idStart = strchr(line, '"') + 1;
            char* idEnd = strchr(idStart, '"');
            if (idEnd == NULL) {
                ESP_LOGW(TAG, "电流行格式无效: %s", line);
                continue;
            }
            
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电流值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            if (valueStart == NULL) {
                ESP_LOGW(TAG, "电流值格式无效: %s", line);
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
                continue;
            }
            
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电压值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            if (valueStart == NULL) {
                ESP_LOGW(TAG, "电压值格式无效: %s", line);
                continue;
            }
            
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
    
    // 再次让出CPU时间，避免计算功率导致UI卡顿
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    // 计算每个端口的功率
    for (int i = 0; i < MAX_PORTS; i++) {
        // 功率 = 电流(mA) * 电压(mV) / 1000000 (转换为W)
        portInfos[i].power = (portInfos[i].current * portInfos[i].voltage) / 1000000.0f;
        totalPower += portInfos[i].power;
    }
    
    // 添加一行日志显示所有端口的电源信息
    ESP_LOGI(TAG, "电源信息: A=%.2fW(%dmA,%dmV), C1=%.2fW(%dmA,%dmV), C2=%.2fW(%dmA,%dmV), C3=%.2fW(%dmA,%dmV), C4=%.2fW(%dmA,%dmV), 总功率=%.2fW", 
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
    
    // 更新端口数据
    for (int i = 0; i < MAX_PORTS; i++) {
        // 计算并格式化值
        float power_w = portInfos[i].power;
        float voltage_v = portInfos[i].voltage / 1000.0f; // 转换为V
        float current_a = portInfos[i].current / 1000.0f; // 转换为A
        
        // 根据电压确定颜色
        lv_color_t color = get_voltage_color(portInfos[i].voltage);
        
        // 更新端口标签颜色
        lv_obj_set_style_text_color(ui_port_labels[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 格式化并更新信息文本
        sprintf(text_buf, "%.2fV  %.2fA  %.2fW", voltage_v, current_a, power_w);
        lv_label_set_text(ui_power_values[i], text_buf);
        lv_obj_set_style_text_color(ui_power_values[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 更新功率条的值（最大功率的百分比）
        int percent = (int)((portInfos[i].power / MAX_PORT_WATTS) * 100);
        // 确保非零功率至少显示一些进度
        if (portInfos[i].power > 0 && percent == 0) {
            percent = 1;
        }
        // 限制最大值为100
        if (percent > 100) {
            percent = 100;
        }
        
        // 设置功率条的值
        lv_bar_set_value(ui_power_arcs[i], percent, LV_ANIM_OFF);
        
        // 确保端口标签文本不变
        lv_label_set_text(ui_port_labels[i], portInfos[i].name);
        
        // 每更新一个端口后添加短暂延迟
        vTaskDelay(1 / portTICK_PERIOD_MS);
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