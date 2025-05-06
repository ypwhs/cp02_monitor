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
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "IP_Scanner.h"

// 标签用于日志
static const char *TAG = "POWER_MONITOR";

// 从main.c导入的IP验证标志
bool IP_Valid_In_Main = false;

// 声明外部常量引用
extern const int MAX_POWER_WATTS;    // 最大总功率
extern const int MAX_PORT_WATTS;     // 每个端口最大功率
extern const char* DATA_URL;         // API URL
extern const int REFRESH_INTERVAL;   // 刷新间隔 (ms)

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
static lv_timer_t *startup_anim_timer = NULL;
static uint8_t startup_anim_progress = 0;

// WiFi图标闪烁控制
static lv_timer_t *wifi_blink_timer = NULL;
static bool wifi_icon_state = false;  // 控制WiFi图标颜色切换

// 添加一个全局变量来跟踪启动动画是否已完成
static bool startup_animation_completed = false;

// 添加一个全局变量来记录上次请求的时间戳
static uint32_t last_data_fetch_time = 0;

// 修改全局变量声明
static char current_data_url[64] = {0};  // 用于存储当前使用的URL

// 扫描回调函数 - 当找到设备时记录
static void scan_result_callback(const char* ip, bool success) {
    if (success) {
        ESP_LOGI(TAG, "====================================================");
        ESP_LOGI(TAG, "       发现小电拼设备: %s", ip);
        ESP_LOGI(TAG, "====================================================");
        
        // 保存发现的有效IP地址
        ESP_LOGI(TAG, "保存有效IP地址到NVS: %s", ip);
        IP_Scanner_SaveIP(ip);
        
        // 更新当前数据URL为新发现的设备
        char new_url[64] = {0};
        snprintf(new_url, sizeof(new_url), "http://%s/metrics", ip);
        
        // 检查URL是否发生变化
        if (strcmp(current_data_url, new_url) != 0) {
            ESP_LOGI(TAG, "数据URL已更新:");
            ESP_LOGI(TAG, "  旧URL: %s", current_data_url);
            ESP_LOGI(TAG, "  新URL: %s", new_url);
            
            // 更新URL
            strncpy(current_data_url, new_url, sizeof(current_data_url) - 1);
        } else {
            ESP_LOGI(TAG, "数据URL未变: %s", current_data_url);
        }
        
        // 立即完成启动动画并启动监控
        if (!startup_animation_completed) {
            ESP_LOGI(TAG, "设备发现后强制完成启动动画");
            
            // 强制完成动画
            if (startup_anim_timer != NULL) {
                lv_timer_del(startup_anim_timer);
                startup_anim_timer = NULL;
            }
            
            // 重置所有进度条为0
            for (int i = 0; i < MAX_PORTS; i++) {
                lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);
            }
            lv_bar_set_value(ui_total_bar, 0, LV_ANIM_OFF);
            
            // 设置动画完成标志
            startup_animation_completed = true;
            
            // 立即创建刷新定时器（如果尚未创建）
            if (refresh_timer == NULL) {
                ESP_LOGI(TAG, "发现设备后立即开始电源监控");
                ESP_LOGI(TAG, "监控数据来源URL: %s", current_data_url);
                refresh_timer = lv_timer_create(PowerMonitor_TimerCallback, REFRESH_INTERVAL, NULL);
                ESP_LOGI(TAG, "刷新定时器已创建，间隔: %d ms", REFRESH_INTERVAL);
            }
        } else {
            // 已经完成动画，但检查刷新定时器是否需要更新URL
            // 注意：这里不创建新的定时器，保持现有定时器运行
            ESP_LOGI(TAG, "URL已更新，将在下一次刷新中使用新URL: %s", current_data_url);
            
            // 立即触发一次数据获取，使用新的URL
            PowerMonitor_FetchData();
        }
    }
}

// 从IP地址提取网段前缀
static bool extract_network_prefix(const char* ip, char* prefix_buffer, size_t buffer_size) {
    if (!ip || !prefix_buffer || buffer_size < 16) {
        ESP_LOGE(TAG, "无效的参数");
        return false;
    }
    
    // 提取网段前缀 (例如: 从"192.168.1.100"提取"192.168.1.")
    const char* last_dot = strrchr(ip, '.');
    if (!last_dot) {
        ESP_LOGE(TAG, "IP格式无效: %s", ip);
        return false;
    }
    
    size_t prefix_len = last_dot - ip + 1;
    if (prefix_len >= buffer_size) {
        ESP_LOGE(TAG, "缓冲区太小");
        return false;
    }
    
    memcpy(prefix_buffer, ip, prefix_len);
    prefix_buffer[prefix_len] = '\0';
    
    ESP_LOGI(TAG, "提取的网段前缀: %s", prefix_buffer);
    return true;
}

// 修改 wifi_status_timer_cb 函数，只有在动画完成后才开始监控
static void wifi_status_timer_cb(lv_timer_t *timer) {
    static bool has_scanned = false;
    
    // 只记录当前WiFi连接状态，不再检查IP状态
    // ESP_LOGI(TAG, "WiFi状态检查 - 连接状态: %s", 
    //          WIFI_Connection ? "已连接" : "未连接");
    
    PowerMonitor_UpdateWiFiStatus();
    
    // 只检查WiFi连接状态，不再检查IP状态
    if (WIFI_Connection) {
        // 如果启动动画已完成且刷新定时器还未创建，则创建刷新定时器
        if (startup_animation_completed && refresh_timer == NULL) {
            ESP_LOGI(TAG, "WiFi已连接，开始电源监控");
            ESP_LOGI(TAG, "监控数据来源URL: %s", current_data_url);
            refresh_timer = lv_timer_create(PowerMonitor_TimerCallback, REFRESH_INTERVAL, NULL);
            ESP_LOGI(TAG, "刷新定时器已创建，间隔: %d ms", REFRESH_INTERVAL);
        }
        
        // 如果尚未进行扫描，且动画已完成，且当前没有有效的URL，则进行网络扫描
        if (!has_scanned && startup_animation_completed) {
            // 检查当前URL是否有效 - 添加此检查
            bool need_scan = true;
            char saved_ip[32] = {0};
            
            // 如果main.c中已经验证过IP，则跳过扫描
            if (IP_Valid_In_Main) {
                ESP_LOGI(TAG, "主程序已验证IP有效，无需重新扫描");
                need_scan = false;
                has_scanned = true;  // 标记为已扫描，避免后续重复检查
            } 
            // 否则检查是否有保存的有效IP
            else if (IP_Scanner_LoadIP(saved_ip, sizeof(saved_ip)) && strlen(saved_ip) > 0) {
                ESP_LOGI(TAG, "已有保存的IP: %s，检查是否需要扫描", saved_ip);
                
                // 检查当前URL是否包含此IP - 如果包含，说明已经有效
                char expected_url[64] = {0};
                snprintf(expected_url, sizeof(expected_url), "http://%s/metrics", saved_ip);
                
                if (strcmp(current_data_url, expected_url) == 0) {
                    ESP_LOGI(TAG, "当前URL已包含有效IP，无需重新扫描");
                    need_scan = false;
                    has_scanned = true;  // 标记为已扫描，避免后续重复检查
                }
            }
            
            if (need_scan) {
                ESP_LOGI(TAG, "WiFi已连接，开始网络扫描");
                
                // 获取当前IP地址
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif != NULL) {
                    esp_netif_ip_info_t ip_info;
                    char self_ip[16] = {0};
                    char network_prefix[16] = {0};
                    
                    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                        sprintf(self_ip, IPSTR, IP2STR(&ip_info.ip));
                        ESP_LOGI(TAG, "当前设备IP: %s", self_ip);
                        
                        // 提取网段前缀
                        if (extract_network_prefix(self_ip, network_prefix, sizeof(network_prefix))) {
                            // 保存扫描前的URL
                            char pre_scan_url[64];
                            strcpy(pre_scan_url, current_data_url);
                            ESP_LOGI(TAG, "===========================");
                            ESP_LOGI(TAG, "开始扫描网段: %s* 寻找小电拼设备", network_prefix);
                            ESP_LOGI(TAG, "===========================");
                            
                            // 记录扫描前的URL
                            ESP_LOGI(TAG, "扫描前的数据URL: %s", pre_scan_url);
                            
                            // 开始网络扫描，允许跳过验证，因为main中已经验证过了
                            IP_Scanner_ScanNetwork(network_prefix, scan_result_callback, true);
                            
                            // 记录扫描后的URL，看是否有变化
                            ESP_LOGI(TAG, "扫描后的数据URL: %s", current_data_url);
                            has_scanned = true;
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "获取网络接口句柄失败");
                }
            }
        }
    }
}

// 修改启动动画回调函数，在动画完成时设置标志
static void startup_animation_cb(lv_timer_t *timer) {
    // 更新进度值
    startup_anim_progress += 20;
    
    // 为所有进度条设置进度
    for (int i = 0; i < MAX_PORTS; i++) {
        lv_bar_set_value(ui_power_bars[i], startup_anim_progress, LV_ANIM_OFF);
    }
    
    // 设置总功率进度条进度
    lv_bar_set_value(ui_total_bar, startup_anim_progress, LV_ANIM_OFF);
    
    // 立即完成动画
    if (startup_anim_progress < 100) {
        // 强制设置进度为100%
        startup_anim_progress = 100;
        
        // 为所有进度条设置进度
        for (int i = 0; i < MAX_PORTS; i++) {
            lv_bar_set_value(ui_power_bars[i], startup_anim_progress, LV_ANIM_OFF);
        }
        
        // 设置总功率进度条进度
        lv_bar_set_value(ui_total_bar, startup_anim_progress, LV_ANIM_OFF);
    }
    
    // 当达到100%时停止动画
    if (startup_anim_progress >= 100) {
        lv_timer_del(startup_anim_timer);
        startup_anim_timer = NULL;
        
        // 重置所有进度条为0
        for (int i = 0; i < MAX_PORTS; i++) {
            lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);
        }
        lv_bar_set_value(ui_total_bar, 0, LV_ANIM_OFF);
        
        // 设置动画完成标志
        startup_animation_completed = true;
        
        ESP_LOGI(TAG, "Startup animation completed");
        
        // 立即检查WiFi状态并创建刷新定时器(如果已连接)
        if (WIFI_Connection && refresh_timer == NULL) {
            ESP_LOGI(TAG, "动画完成后立即开始电源监控");
            ESP_LOGI(TAG, "监控数据来源URL: %s", current_data_url);
            refresh_timer = lv_timer_create(PowerMonitor_TimerCallback, REFRESH_INTERVAL, NULL);
            ESP_LOGI(TAG, "刷新定时器已创建，间隔: %d ms", REFRESH_INTERVAL);
            
            // 立即执行一次数据获取
            PowerMonitor_FetchData();
        }
    }
}


// WiFi状态图标闪烁计时器回调
static void wifi_blink_timer_cb(lv_timer_t *timer) {
    // 只检查WiFi连接状态，不再检查IP状态
    if (WIFI_Connection && !dataError) {
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
    }
}

// ESP-IDF HTTP客户端事件处理
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
                    // 初始分配8KB缓冲区，足够大多数请求
                    int initial_size = 8192;
                    output_buffer = (char *)malloc(initial_size);
                    if (output_buffer == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                    output_len = 0;
                }
                
                // 检查是否需要扩展缓冲区
                size_t new_size = output_len + evt->data_len + 1; // +1 for null terminator
                output_buffer = (char *)realloc(output_buffer, new_size);
                if (output_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to reallocate memory for output buffer");
                    return ESP_FAIL;
                }
                
                // 复制数据到缓冲区
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL && output_len > 0) {
                output_buffer[output_len] = '\0';
                PowerMonitor_ParseData(output_buffer);
                free(output_buffer);
            } else {
                ESP_LOGW(TAG, "No data received");
            }
            output_buffer = NULL;
            output_len = 0;
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer != NULL) {
                // 如果断开连接时已经收到数据，尝试处理
                if (output_len > 0) {
                    output_buffer[output_len] = '\0';
                    PowerMonitor_ParseData(output_buffer);
                }
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown HTTP event: %d", evt->event_id);
            break;
    }
    return ESP_OK;
}

// 初始化电源监控
void PowerMonitor_Init(void) {
    ESP_LOGI(TAG, "Initializing Power Monitor...");
    
    // 不要在这里初始化WiFi状态，因为main.c中已经设置了正确的值
    // WIFI_GotIP = false;  // 删除这行
    
    // 初始化启动动画标志
    startup_animation_completed = false;
    
    // 初始化数据获取时间戳
    last_data_fetch_time = esp_log_timestamp();
    
    // 从NVS加载保存的IP地址
    char saved_ip[32] = {0};
    if (IP_Scanner_LoadIP(saved_ip, sizeof(saved_ip))) {
        snprintf(current_data_url, sizeof(current_data_url), "http://%s/metrics", saved_ip);
        ESP_LOGI(TAG, "Using saved IP for data URL: %s", current_data_url);
    } else {
        // 如果无法加载保存的IP，使用默认URL
        strncpy(current_data_url, DATA_URL, sizeof(current_data_url) - 1);
        ESP_LOGW(TAG, "Using default data URL: %s", current_data_url);
    }
    
    // 打印明显的数据URL信息，方便调试
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "电源监控数据将从以下URL获取: %s", current_data_url);
    ESP_LOGI(TAG, "============================================");
    
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
    
    // 启动动画：创建一个5ms的定时器，每次增加20%，总共5步，约0.25秒完成
    startup_anim_progress = 0;
    startup_anim_timer = lv_timer_create(startup_animation_cb, 5, NULL);
    
    // 创建WiFi状态监控定时器 - 它会在WiFi连接后启动数据刷新定时器
    wifi_timer = lv_timer_create(wifi_status_timer_cb, 1000, NULL);
    
    ESP_LOGI(TAG, "Power Monitor initialized, waiting for WiFi connection");
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
    
    // 开始WiFi图标闪烁定时器
    wifi_blink_timer = lv_timer_create(wifi_blink_timer_cb, 500, NULL);
    
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
    lv_label_set_text(ui_total_label, "Total: 0.00W");
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
    PowerMonitor_UpdateWiFiStatus();
}

// 从网络获取数据
void PowerMonitor_FetchData(void) {
    static esp_http_client_handle_t client = NULL;
    uint32_t current_time = esp_log_timestamp();
    
    // 确保请求间隔大于REFRESH_INTERVAL
    if (current_time - last_data_fetch_time < REFRESH_INTERVAL) {
        return; // 间隔不够，跳过本次请求
    }
    
    // 只检查WiFi连接状态，不再检查IP状态
    if (!WIFI_Connection) {
        ESP_LOGW(TAG, "WiFi未连接，跳过数据获取");
        return;
    }
    
    // 每次调用时检查客户端是否已初始化
    if (client == NULL) {
        // 创建HTTP客户端配置
        esp_http_client_config_t config = {
            .url = current_data_url,  // 使用当前URL
            .event_handler = http_event_handler,
            .timeout_ms = 1000,  // 减少超时时间
            .buffer_size = 4096,
            .disable_auto_redirect = true, // 禁用自动重定向
            .skip_cert_common_name_check = true, // 跳过证书检查
        };
        
        client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "初始化HTTP客户端失败");
            return;
        }
        
        // 设置请求头
        esp_http_client_set_method(client, HTTP_METHOD_GET);
        esp_http_client_set_header(client, "Accept", "text/plain");
        esp_http_client_set_header(client, "User-Agent", "ESP32-HTTP-Client");
    }
    
    // 记录请求开始时间
    last_data_fetch_time = current_time;

    // 执行非阻塞HTTP请求
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
        
        // 如果是超时错误，清理并重新初始化客户端
        if (err == ESP_ERR_HTTP_FETCH_HEADER || err == ESP_ERR_HTTP_CONNECTING) {
            ESP_LOGI(TAG, "重置HTTP客户端连接");
            esp_http_client_cleanup(client);
            client = NULL;
        }
    }
    
    // 更新WiFi状态以反映数据错误
    PowerMonitor_UpdateWiFiStatus();
    
    // 让出CPU时间
    vTaskDelay(1);
}

// 解析数据
void PowerMonitor_ParseData(char* payload) {
    // 检查有效载荷
    if (payload == NULL || strlen(payload) == 0) {
        ESP_LOGE(TAG, "Empty payload received for parsing");
        return;
    }
    
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
            if (idEnd == NULL) {
                ESP_LOGW(TAG, "Invalid format in current line: %s", line);
                continue;
            }
            
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电流值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            if (valueStart == NULL) {
                ESP_LOGW(TAG, "Invalid format in current value: %s", line);
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
                ESP_LOGW(TAG, "Invalid format in voltage line: %s", line);
                continue;
            }
            
            *idEnd = '\0';
            int portId = atoi(idStart);
            
            // 提取电压值
            char* valueStart = strchr(idEnd + 1, '}') + 1;
            if (valueStart == NULL) {
                ESP_LOGW(TAG, "Invalid format in voltage value: %s", line);
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
    
    // 计算每个端口的功率
    for (int i = 0; i < MAX_PORTS; i++) {
        // 功率 = 电流(mA) * 电压(mV) / 1000000 (转换为W)
        portInfos[i].power = (portInfos[i].current * portInfos[i].voltage) / 1000000.0f;
        totalPower += portInfos[i].power;
    }
    
    // 添加一行日志显示所有端口的电源信息
    ESP_LOGI(TAG, "Power Info: A=%.2fW(%dmA,%dmV), C1=%.2fW(%dmA,%dmV), C2=%.2fW(%dmA,%dmV), C3=%.2fW(%dmA,%dmV), C4=%.2fW(%dmA,%dmV), Total=%.2fW", 
             portInfos[0].power, portInfos[0].current, portInfos[0].voltage,
             portInfos[1].power, portInfos[1].current, portInfos[1].voltage,
             portInfos[2].power, portInfos[2].current, portInfos[2].voltage,
             portInfos[3].power, portInfos[3].current, portInfos[3].voltage,
             portInfos[4].power, portInfos[4].current, portInfos[4].voltage,
             totalPower);
    
    // 更新UI
    PowerMonitor_UpdateUI();
}

// 修改 PowerMonitor_UpdateUI 函数，基于电压使用不同颜色
void PowerMonitor_UpdateUI(void) {
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
        
        // 使用简单方式设置值，避免动画引起的问题
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
    
    // 使用简单方式设置值，避免动画引起的问题
    lv_bar_set_value(ui_total_bar, totalPercent, LV_ANIM_OFF);
}

// 更新UI上的WiFi状态
void PowerMonitor_UpdateWiFiStatus(void) {
    // 输出当前WiFi状态的详细信息，不再检查IP状态
    // ESP_LOGI(TAG, "更新WiFi状态UI - 连接状态: %d, 数据错误: %d", 
    //          WIFI_Connection, dataError);
    
    // 更新WiFi连接状态
    if (WIFI_Connection) {
        if (dataError) {
            // WiFi已连接但数据错误
            lv_obj_t * label = ui_wifi_status;
            lv_label_set_recolor(label, true);
            lv_label_set_text(label, "WiFi: #FF0000 DATA ERROR#");
            ESP_LOGW(TAG, "WiFi已连接但数据获取错误");
        } else {
            // WiFi已连接且数据正常 - 颜色由闪烁定时器控制
            lv_label_set_text(ui_wifi_status, "WiFi");
            // 不在这里设置颜色，由wifi_blink_timer_cb处理
        }
    } else {
        // WiFi断开连接
        lv_label_set_text(ui_wifi_status, "WiFi");
        lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        ESP_LOGW(TAG, "WiFi断开连接");
    }
}

// 定时器回调
void PowerMonitor_TimerCallback(lv_timer_t *timer) {
    static uint32_t last_log_time = 0;
    uint32_t current_time = esp_log_timestamp();
    
    // 执行数据获取函数
    PowerMonitor_FetchData();
    
    // 如果从上次获取数据已经过了太长时间，记录日志（仅用于调试）
    if (current_time - last_data_fetch_time > REFRESH_INTERVAL * 2 && 
        current_time - last_log_time > 1000) {  // 限制日志频率
        ESP_LOGW(TAG, "数据获取间隔超过预期: %d ms (预期: %d ms)", 
                 (int)(current_time - last_data_fetch_time), REFRESH_INTERVAL);
        last_log_time = current_time;
    }
}
