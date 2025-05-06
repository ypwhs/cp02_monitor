/**
 * @file     settings_ui.c
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Settings UI Module Implementation
 */

#include "settings_ui.h"
#include "wifi_manager.h"
#include "power_monitor.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SETTINGS_UI";

// UI组件
static lv_obj_t *ui_settings_screen = NULL;
static lv_obj_t *ui_ssid_input = NULL;
static lv_obj_t *ui_password_input = NULL;
static lv_obj_t *ui_device_ip_label = NULL;   // 改为只读标签
static lv_obj_t *ui_device_ip_input = NULL;   // 小电拼IP输入
static lv_obj_t *ui_keyboard = NULL;

// 设置改变回调
static settings_change_cb_t change_callback = NULL;

// 前向声明
static void settings_save_btn_event_cb(lv_event_t *e);
static void settings_return_btn_event_cb(lv_event_t *e);
static void wifi_connect_btn_event_cb(lv_event_t *e);
static void input_focused_cb(lv_event_t *e);
static void keyboard_ready_cb(lv_event_t *e);

// 添加外部声明，让settings_ui能够调用power_monitor的函数
extern lv_obj_t *get_main_screen(void);
// 添加外部声明，用于暂停和恢复主程序定时器
extern void pause_main_timer(void);
extern void resume_main_timer(void);

// 添加WiFi状态回调声明和全局变量
static lv_obj_t *wifi_status_mbox = NULL;
static lv_timer_t *wifi_timeout_timer = NULL;

// 函数前向声明
static void update_wifi_status_display(const char *title, const char *msg);
static void wifi_status_callback(wifi_status_t status);

// 定义一个回调函数用于定时器
static void wifi_connect_timer_cb(lv_timer_t *timer)
{
    if (wifi_status_mbox != NULL) {
        lv_msgbox_close(wifi_status_mbox);
        wifi_status_mbox = NULL;
    }
    lv_timer_del(timer);
}

// WiFi连接超时回调
static void wifi_connect_timeout_cb(lv_timer_t *timer)
{
    ESP_LOGE(TAG, "WiFi连接超时");
    
    if (wifi_status_mbox != NULL) {
        lv_msgbox_close(wifi_status_mbox);
        
        // 显示连接超时消息
        wifi_status_mbox = lv_msgbox_create(NULL, "错误", "WiFi连接超时，请检查网络设置", NULL, true);
        lv_obj_set_style_text_font(wifi_status_mbox, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(wifi_status_mbox);
    }
    
    if (wifi_timeout_timer != NULL) {
        lv_timer_del(wifi_timeout_timer);
        wifi_timeout_timer = NULL;
    }
}

// 更新WiFi连接状态显示
static void update_wifi_status_display(const char *title, const char *msg)
{
    // 关闭之前的消息框
    if (wifi_status_mbox != NULL) {
        lv_msgbox_close(wifi_status_mbox);
    }
    
    // 创建新的消息框
    wifi_status_mbox = lv_msgbox_create(NULL, title, msg, NULL, false);
    lv_obj_set_style_text_font(wifi_status_mbox, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(wifi_status_mbox);
}

// WiFi状态回调 - 真实处理WiFi事件
static void wifi_status_callback(wifi_status_t status)
{
    ESP_LOGI(TAG, "WiFi状态更新: %d", status);
    
    switch (status) {
        case WIFI_STATUS_CONNECTING:
            update_wifi_status_display("提示", "正在连接WiFi...");
            break;
            
        case WIFI_STATUS_CONNECTED:
            update_wifi_status_display("提示", "WiFi已连接，正在获取IP...");
            break;
            
        case WIFI_STATUS_GOT_IP:
            // 取消超时定时器
            if (wifi_timeout_timer != NULL) {
                lv_timer_del(wifi_timeout_timer);
                wifi_timeout_timer = NULL;
            }
            
            // 获取当前IP显示在界面上
            wifi_user_config_t config;
            if (wifi_manager_get_config(&config) == ESP_OK) {
                lv_label_set_text(ui_device_ip_label, config.device_ip);
            }
            
            update_wifi_status_display("成功", "WiFi连接成功");
            
            // 2秒后关闭成功消息
            lv_timer_t *close_timer = lv_timer_create(wifi_connect_timer_cb, 2000, NULL);
            lv_timer_set_repeat_count(close_timer, 1);
            break;
            
        case WIFI_STATUS_DISCONNECTED:
            update_wifi_status_display("提示", "WiFi已断开连接");
            break;
            
        case WIFI_STATUS_CONNECT_FAILED:
            // 取消超时定时器
            if (wifi_timeout_timer != NULL) {
                lv_timer_del(wifi_timeout_timer);
                wifi_timeout_timer = NULL;
            }
            
            update_wifi_status_display("错误", "WiFi连接失败，请检查设置");
            break;
            
        default:
            break;
    }
}

// 初始化设置UI
void settings_ui_init(void)
{
    ESP_LOGI(TAG, "初始化设置UI");
    
    // 注册WiFi状态回调
    wifi_manager_register_cb(wifi_status_callback);
    
    // 预先创建设置页面，但不显示
    settings_ui_create();
}

// 创建设置UI
void settings_ui_create(void)
{
    // 创建统一的设置页面
    ui_settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_settings_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_settings_screen, 10, LV_PART_MAIN);  // 增加内边距
    
    // 页面标题
    lv_obj_t *settings_title = lv_label_create(ui_settings_screen);
    lv_label_set_text(settings_title, "设置");
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(settings_title, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 返回按钮 - 移到右上角
    lv_obj_t *return_btn = lv_btn_create(ui_settings_screen);
    lv_obj_set_size(return_btn, 80, 40);
    lv_obj_align(return_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(return_btn, lv_color_hex(0x999999), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(return_btn, settings_return_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *return_label = lv_label_create(return_btn);
    lv_label_set_text(return_label, "返回");
    lv_obj_set_style_text_font(return_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(return_label);
    
    // 分隔线1 - 标题下方
    lv_obj_t *separator1 = lv_line_create(ui_settings_screen);
    static lv_point_t sep1_points[] = {{0, 0}, {320, 0}};
    lv_line_set_points(separator1, sep1_points, 2);
    lv_obj_set_style_line_width(separator1, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(separator1, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(separator1, LV_ALIGN_TOP_MID, 0, 50);
    
    // 创建SSID输入区域 - 增加20%宽度
    lv_obj_t *ssid_cont = lv_obj_create(ui_settings_screen);
    lv_obj_set_size(ssid_cont, 360, 60); // 从300增加到360
    lv_obj_align(ssid_cont, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_pad_all(ssid_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ssid_cont, lv_color_hex(0xF5F5F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ssid_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ssid_cont, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);

    // SSID标签
    lv_obj_t *ssid_label = lv_label_create(ssid_cont);
    lv_label_set_text(ssid_label, "SSID:");
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ssid_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 5, 0);
    
    // SSID输入框 - 增加宽度
    ui_ssid_input = lv_textarea_create(ssid_cont);
    lv_obj_set_size(ui_ssid_input, 280, 45); // 从220增加到280
    lv_obj_align(ui_ssid_input, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_textarea_set_placeholder_text(ui_ssid_input, "输入WiFi名称");
    lv_obj_set_style_text_font(ui_ssid_input, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ssid_input, input_focused_cb, LV_EVENT_CLICKED, NULL);

    // 创建密码输入区域 - 增加20%宽度
    lv_obj_t *pwd_cont = lv_obj_create(ui_settings_screen);
    lv_obj_set_size(pwd_cont, 360, 60); // 从300增加到360
    lv_obj_align_to(pwd_cont, ssid_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_pad_all(pwd_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pwd_cont, lv_color_hex(0xF5F5F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pwd_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pwd_cont, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 密码标签
    lv_obj_t *password_label = lv_label_create(pwd_cont);
    lv_label_set_text(password_label, "密码:");
    lv_obj_set_style_text_color(password_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(password_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(password_label, LV_ALIGN_LEFT_MID, 5, 0);
    
    // 密码输入框 - 增加宽度
    ui_password_input = lv_textarea_create(pwd_cont);
    lv_obj_set_size(ui_password_input, 210, 45); // 从150增加到210
    lv_obj_align(ui_password_input, LV_ALIGN_LEFT_MID, 70, 0);
    lv_textarea_set_placeholder_text(ui_password_input, "输入密码");
    lv_obj_set_style_text_font(ui_password_input, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_textarea_set_password_mode(ui_password_input, true);
    lv_obj_add_event_cb(ui_password_input, input_focused_cb, LV_EVENT_CLICKED, NULL);
    
    // 连接按钮 - 放在密码输入框右边
    lv_obj_t *wifi_connect_btn = lv_btn_create(pwd_cont);
    lv_obj_set_size(wifi_connect_btn, 60, 45);
    lv_obj_align(wifi_connect_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(wifi_connect_btn, lv_color_hex(0x2196F3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(wifi_connect_btn, wifi_connect_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *connect_btn_label = lv_label_create(wifi_connect_btn);
    lv_label_set_text(connect_btn_label, "连接");
    lv_obj_set_style_text_font(connect_btn_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(connect_btn_label);
    
    // 分隔线2 - IP部分上方
    lv_obj_t *separator2 = lv_line_create(ui_settings_screen);
    static lv_point_t sep2_points[] = {{0, 0}, {320, 0}};
    lv_line_set_points(separator2, sep2_points, 2);
    lv_obj_set_style_line_width(separator2, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(separator2, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(separator2, pwd_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);
    
    // 创建设备IP显示区域 - 增加20%宽度
    lv_obj_t *ip_cont = lv_obj_create(ui_settings_screen);
    lv_obj_set_size(ip_cont, 360, 60); // 从300增加到360
    lv_obj_align_to(ip_cont, separator2, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);
    lv_obj_set_style_pad_all(ip_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ip_cont, lv_color_hex(0xF5F5F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ip_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ip_cont, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 设备IP标签
    lv_obj_t *ip_label = lv_label_create(ip_cont);
    lv_label_set_text(ip_label, "当前设备IP:");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ip_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_label, LV_ALIGN_LEFT_MID, 5, 0);
    
    // 设备IP显示标签（只读）
    ui_device_ip_label = lv_label_create(ip_cont);
    lv_label_set_text(ui_device_ip_label, "0.0.0.0");
    lv_obj_set_style_text_color(ui_device_ip_label, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_device_ip_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_device_ip_label, LV_ALIGN_RIGHT_MID, -10, 0);
    
    // 创建小电拼设备IP输入容器 - 调整位置与其他设置一致
    lv_obj_t *device_ip_cont = lv_obj_create(ui_settings_screen);
    lv_obj_set_size(device_ip_cont, 360, 60); // 从230增加到360
    lv_obj_align_to(device_ip_cont, ip_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 10); // 与其他设置项对齐
    lv_obj_set_style_pad_all(device_ip_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(device_ip_cont, lv_color_hex(0xF5F5F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(device_ip_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(device_ip_cont, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 小电拼设备IP标签
    lv_obj_t *device_ip_text = lv_label_create(device_ip_cont);
    lv_label_set_text(device_ip_text, "小电拼IP:");
    lv_obj_set_style_text_color(device_ip_text, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(device_ip_text, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(device_ip_text, LV_ALIGN_LEFT_MID, 5, 0);
    
    // 小电拼设备IP输入框 - 增加宽度
    ui_device_ip_input = lv_textarea_create(device_ip_cont);
    lv_obj_set_size(ui_device_ip_input, 210, 45); // 从140增加到210
    lv_obj_align(ui_device_ip_input, LV_ALIGN_LEFT_MID, 80, 0);
    lv_textarea_set_placeholder_text(ui_device_ip_input, "输入小电拼IP");
    lv_obj_set_style_text_font(ui_device_ip_input, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_device_ip_input, input_focused_cb, LV_EVENT_CLICKED, NULL);
    
    // IP部分保存按钮 - 调整位置
    lv_obj_t *ip_save_btn = lv_btn_create(device_ip_cont);
    lv_obj_set_size(ip_save_btn, 60, 45);
    lv_obj_align(ip_save_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(ip_save_btn, lv_color_hex(0x00AA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ip_save_btn, settings_save_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ip_save_label = lv_label_create(ip_save_btn);
    lv_label_set_text(ip_save_label, "保存");
    lv_obj_set_style_text_font(ip_save_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(ip_save_label);
    
    // 创建键盘 - 占满底部
    ui_keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(ui_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ui_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // 设置键盘的Ready事件，当用户点击OK时关闭键盘
    lv_obj_add_event_cb(ui_keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ui_keyboard, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);
    
    // 加载当前的WiFi和设备IP设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ssid_input, config.ssid);
        lv_textarea_set_text(ui_password_input, ""); // 不显示密码，即使配置中有密码
        lv_textarea_set_placeholder_text(ui_password_input, "输入密码");
        lv_label_set_text(ui_device_ip_label, config.device_ip);
        
        // 设置小电拼IP默认值
        const char* metrics_url = power_monitor_get_data_url();
        if (metrics_url != NULL) {
            // 从URL中解析IP地址
            char device_ip[128] = "";
            if (sscanf(metrics_url, "http://%[^/]/metrics", device_ip) == 1) {
                lv_textarea_set_text(ui_device_ip_input, device_ip);
            } else {
                lv_textarea_set_text(ui_device_ip_input, "192.168.1.19");  // 默认值
            }
        } else {
            lv_textarea_set_text(ui_device_ip_input, "192.168.1.19");  // 默认值
        }
    }
}

// 输入框聚焦回调
static void input_focused_cb(lv_event_t *e)
{
    lv_obj_t *textarea = lv_event_get_target(e);
    
    // 显示键盘并设置目标
    lv_keyboard_set_textarea(ui_keyboard, textarea);
    
    // 将键盘添加到当前活动屏幕并显示
    lv_obj_set_parent(ui_keyboard, lv_scr_act());
    lv_obj_clear_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ui_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // 如果是小电拼IP输入框，直接应用固定偏移
    if (textarea == ui_device_ip_input) {
        // 针对小电拼IP输入框使用固定偏移
        lv_obj_set_y(ui_settings_screen, -180); // 使用固定的偏移值
        return;
    }
    
    // 获取键盘高度
    lv_coord_t kb_height = lv_obj_get_height(ui_keyboard);
    
    // 获取textarea的位置和大小
    lv_coord_t textarea_y = textarea->coords.y1;
    lv_coord_t textarea_height = lv_obj_get_height(textarea);
    lv_coord_t screen_height = lv_disp_get_ver_res(lv_disp_get_default());
    
    // 计算textarea的底部位置
    lv_coord_t textarea_bottom = textarea_y + textarea_height;
    
    // 计算键盘的顶部位置
    lv_coord_t kb_top = screen_height - kb_height;
    
    // 如果输入框会被键盘遮挡，则调整设置面板的位置
    if (textarea_bottom > kb_top) {
        // 计算需要上移的距离
        lv_coord_t offset = textarea_bottom - kb_top + 40; // 额外加40像素的间距
        
        // 调整整个设置屏幕的位置
        lv_obj_set_y(ui_settings_screen, -offset);
    }
}

// 键盘ready回调 - 用于处理键盘OK和Cancel按钮
static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 恢复设置屏幕的位置
    lv_obj_set_y(ui_settings_screen, 0);
}

// 连接WiFi按钮回调
static void wifi_connect_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Connecting to WiFi");
    
    // 获取输入的值
    const char *ssid = lv_textarea_get_text(ui_ssid_input);
    const char *password = lv_textarea_get_text(ui_password_input);
    
    // 验证SSID不为空
    if (strlen(ssid) == 0) {
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "错误", "WiFi名称不能为空", NULL, true);
        lv_obj_set_style_text_font(alert, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(alert);
        return;
    }
    
    // 创建配置
    wifi_user_config_t config;
    wifi_manager_get_config(&config); // 获取当前配置
    
    // 更新设置
    strncpy(config.ssid, ssid, MAX_SSID_LEN);
    strncpy(config.password, password, MAX_PASS_LEN);
    config.auto_connect = true;  // 默认自动连接
    
    // 保存配置
    esp_err_t err = wifi_manager_set_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi config: %s", esp_err_to_name(err));
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "错误", "保存WiFi设置失败", NULL, true);
        lv_obj_set_style_text_font(alert, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(alert);
        return;
    }
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    // 恢复设置屏幕的位置
    lv_obj_set_y(ui_settings_screen, 0);
    
    // 创建超时定时器
    if (wifi_timeout_timer != NULL) {
        lv_timer_del(wifi_timeout_timer);
    }
    wifi_timeout_timer = lv_timer_create(wifi_connect_timeout_cb, 10000, NULL);
    lv_timer_set_repeat_count(wifi_timeout_timer, 1);
    
    // 连接WiFi - 状态更新将通过回调函数处理
    wifi_manager_connect();
}

// 设置保存按钮回调
static void settings_save_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Saving settings");
    
    // 获取输入的值
    const char *metrics_ip = lv_textarea_get_text(ui_device_ip_input);
    
    // 隐藏键盘如果可见
    if (!lv_obj_has_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
        // 恢复设置屏幕的位置
        lv_obj_set_y(ui_settings_screen, 0);
    }
    
    // 保存小电拼IP设置（通过更新数据URL）
    char url[128];
    snprintf(url, sizeof(url), "http://%s/metrics", metrics_ip);
    esp_err_t err = power_monitor_set_data_url(url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving metrics URL: %s", esp_err_to_name(err));
        
        // 创建一个消息框显示错误
        lv_obj_t *mbox = lv_msgbox_create(NULL, "错误", "保存小电拼IP设置失败", NULL, true);
        lv_obj_set_style_text_font(mbox, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(mbox);
        return;
    }
    
    ESP_LOGI(TAG, "Settings saved successfully");
    ESP_LOGI(TAG, "  Metrics URL: %s", url);
    
    // 显示保存成功消息
    lv_obj_t *success_mbox = lv_msgbox_create(NULL, "提示", "设置已保存", NULL, true);
    lv_obj_set_style_text_font(success_mbox, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(success_mbox);
    
    // 通知设置已更改
    if (change_callback != NULL) {
        change_callback();
    }
}

// 返回按钮回调
static void settings_return_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Returning to main screen");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    // 恢复设置屏幕的位置
    lv_obj_set_y(ui_settings_screen, 0);
    
    // 获取主屏幕并加载
    lv_obj_t *main_screen = get_main_screen();
    if (main_screen != NULL) {
        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
        
        // 恢复主程序定时器
        resume_main_timer();
    }
}

// 打开设置页面
void settings_ui_open_wifi_settings(void)
{
    ESP_LOGI(TAG, "Opening settings page");
    
    // 暂停主程序定时器
    pause_main_timer();
    
    // 加载当前的WiFi和IP设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ssid_input, config.ssid);
        lv_textarea_set_text(ui_password_input, ""); // 不显示密码，即使配置中有密码
        lv_textarea_set_placeholder_text(ui_password_input, "输入密码");
        
        // 获取当前实际IP而不是存储的IP
        char ip[16] = "0.0.0.0";
        if (wifi_manager_is_connected()) {
            wifi_manager_get_ip(ip, sizeof(ip));
            lv_label_set_text(ui_device_ip_label, ip);
        } else {
            lv_label_set_text(ui_device_ip_label, "未连接");
        }
        
        // 设置小电拼IP
        const char* metrics_url = power_monitor_get_data_url();
        if (metrics_url != NULL) {
            // 从URL中解析IP地址
            char device_ip[128] = "";
            if (sscanf(metrics_url, "http://%[^/]/metrics", device_ip) == 1) {
                lv_textarea_set_text(ui_device_ip_input, device_ip);
            }
        }
    }
    
    // 确保设置屏幕位置正确
    lv_obj_set_y(ui_settings_screen, 0);
    
    // 加载设置页面
    lv_scr_load(ui_settings_screen);
    
    // 确保键盘隐藏
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// 为兼容性保留的函数，现在直接调用打开设置页面的函数
void settings_ui_open_ip_settings(void)
{
    settings_ui_open_wifi_settings();
}

// 关闭设置页面
void settings_ui_close_wifi_settings(void)
{
    ESP_LOGI(TAG, "Closing settings page");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 确保设置屏幕位置正确
    lv_obj_set_y(ui_settings_screen, 0);
    
    // 获取主屏幕并加载
    lv_obj_t *main_screen = get_main_screen();
    if (main_screen != NULL) {
        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
        
        // 恢复主程序定时器
        resume_main_timer();
    }
}

// 为兼容性保留的函数，现在直接调用关闭设置页面的函数
void settings_ui_close_ip_settings(void)
{
    settings_ui_close_wifi_settings();
}

// 设置UI回调函数
void settings_ui_register_change_cb(settings_change_cb_t callback)
{
    change_callback = callback;
}

// 检查设置按钮是否被按下
void settings_ui_check_button(void)
{
    // 此函数可用于轮询检查设置按钮状态，但在LVGL中，通常使用事件回调
    // 在我们的实现中，使用了事件回调，所以此函数暂时为空
} 