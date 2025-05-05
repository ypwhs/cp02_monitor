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
static lv_obj_t *ui_wifi_setting_screen = NULL;
static lv_obj_t *ui_ip_setting_screen = NULL;
static lv_obj_t *ui_ssid_input = NULL;
static lv_obj_t *ui_password_input = NULL;
static lv_obj_t *ui_ip_input = NULL;
static lv_obj_t *ui_keyboard = NULL;

// 设置改变回调
static settings_change_cb_t change_callback = NULL;

// 前向声明
static void wifi_save_btn_event_cb(lv_event_t *e);
static void wifi_cancel_btn_event_cb(lv_event_t *e);
static void ip_save_btn_event_cb(lv_event_t *e);
static void ip_cancel_btn_event_cb(lv_event_t *e);
static void input_focused_cb(lv_event_t *e);
static void keyboard_ready_cb(lv_event_t *e);

// 添加外部声明，让settings_ui能够调用power_monitor的函数
extern lv_obj_t *get_main_screen(void);

// 初始化设置UI
void settings_ui_init(void)
{
    ESP_LOGI(TAG, "初始化设置UI");
    
    // 预先创建设置页面，但不显示
    settings_ui_create();
}

// 创建设置UI
void settings_ui_create(void)
{
    // 创建WiFi设置页面 - 包含SSID和密码输入
    ui_wifi_setting_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_wifi_setting_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_wifi_setting_screen, 10, LV_PART_MAIN);  // 增加内边距
    
    // 页面标题
    lv_obj_t *wifi_title = lv_label_create(ui_wifi_setting_screen);
    lv_label_set_text(wifi_title, "WiFi 设置");
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(wifi_title, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建SSID输入区域
    lv_obj_t *ssid_cont = lv_obj_create(ui_wifi_setting_screen);
    lv_obj_set_size(ssid_cont, 300, 60);  // 增加高度
    lv_obj_align(ssid_cont, LV_ALIGN_TOP_MID, 0, 60);
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
    
    // SSID输入框
    ui_ssid_input = lv_textarea_create(ssid_cont);
    lv_obj_set_size(ui_ssid_input, 220, 45);  // 增加高度
    lv_obj_align(ui_ssid_input, LV_ALIGN_RIGHT_MID, -5, 0);  // 靠右对齐
    lv_textarea_set_placeholder_text(ui_ssid_input, "输入WiFi名称");
    lv_obj_set_style_text_font(ui_ssid_input, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ssid_input, input_focused_cb, LV_EVENT_CLICKED, NULL);

    // 创建密码输入区域
    lv_obj_t *pwd_cont = lv_obj_create(ui_wifi_setting_screen);
    lv_obj_set_size(pwd_cont, 300, 60);  // 增加高度
    lv_obj_align_to(pwd_cont, ssid_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);  // 放在SSID区域下方
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
    
    // 密码输入框
    ui_password_input = lv_textarea_create(pwd_cont);
    lv_obj_set_size(ui_password_input, 220, 45);  // 增加高度
    lv_obj_align(ui_password_input, LV_ALIGN_RIGHT_MID, -5, 0);  // 靠右对齐
    lv_textarea_set_placeholder_text(ui_password_input, "输入密码");
    lv_obj_set_style_text_font(ui_password_input, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_textarea_set_password_mode(ui_password_input, true);  // 确保密码输入框始终为密码模式
    lv_obj_add_event_cb(ui_password_input, input_focused_cb, LV_EVENT_CLICKED, NULL);
    
    // 保存按钮 - 使用绿色
    lv_obj_t *wifi_save_btn = lv_btn_create(ui_wifi_setting_screen);
    lv_obj_set_size(wifi_save_btn, 120, 50);
    lv_obj_align(wifi_save_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(wifi_save_btn, lv_color_hex(0x00AA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(wifi_save_btn, wifi_save_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *wifi_save_label = lv_label_create(wifi_save_btn);
    lv_label_set_text(wifi_save_label, "保存");
    lv_obj_set_style_text_font(wifi_save_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(wifi_save_label);
    
    // 取消按钮
    lv_obj_t *wifi_cancel_btn = lv_btn_create(ui_wifi_setting_screen);
    lv_obj_set_size(wifi_cancel_btn, 120, 50);
    lv_obj_align(wifi_cancel_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_bg_color(wifi_cancel_btn, lv_color_hex(0x999999), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(wifi_cancel_btn, wifi_cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *wifi_cancel_label = lv_label_create(wifi_cancel_btn);
    lv_label_set_text(wifi_cancel_label, "返回");
    lv_obj_set_style_text_font(wifi_cancel_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(wifi_cancel_label);
    
    // 创建IP设置页面
    ui_ip_setting_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_ip_setting_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ip_setting_screen, 10, LV_PART_MAIN);  // 增加内边距
    
    // 页面标题
    lv_obj_t *ip_title = lv_label_create(ui_ip_setting_screen);
    lv_label_set_text(ip_title, "设备IP设置");
    lv_obj_set_style_text_color(ip_title, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ip_title, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // 创建IP输入区域
    lv_obj_t *ip_cont = lv_obj_create(ui_ip_setting_screen);
    lv_obj_set_size(ip_cont, 300, 60);  // 增加高度
    lv_obj_align(ip_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_pad_all(ip_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ip_cont, lv_color_hex(0xF5F5F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ip_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ip_cont, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // IP标签
    lv_obj_t *ip_label = lv_label_create(ip_cont);
    lv_label_set_text(ip_label, "设备IP:");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ip_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_label, LV_ALIGN_LEFT_MID, 5, 0);
    
    // IP输入框
    ui_ip_input = lv_textarea_create(ip_cont);
    lv_obj_set_size(ui_ip_input, 200, 45);  // 增加高度
    lv_obj_align(ui_ip_input, LV_ALIGN_RIGHT_MID, -5, 0);  // 靠右对齐
    lv_textarea_set_placeholder_text(ui_ip_input, "输入设备IP");
    lv_obj_set_style_text_font(ui_ip_input, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_ip_input, input_focused_cb, LV_EVENT_CLICKED, NULL);
    
    // 说明标签
    lv_obj_t *ip_desc = lv_label_create(ui_ip_setting_screen);
    lv_label_set_text(ip_desc, "注意: 设置设备IP地址\n格式: 192.168.1.100");
    lv_obj_set_style_text_color(ip_desc, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ip_desc, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_desc, LV_ALIGN_TOP_MID, 0, 135);  // 调整位置
    
    // 保存按钮
    lv_obj_t *ip_save_btn = lv_btn_create(ui_ip_setting_screen);
    lv_obj_set_size(ip_save_btn, 120, 50);
    lv_obj_align(ip_save_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(ip_save_btn, lv_color_hex(0x00AA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ip_save_btn, ip_save_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ip_save_label = lv_label_create(ip_save_btn);
    lv_label_set_text(ip_save_label, "保存");
    lv_obj_set_style_text_font(ip_save_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(ip_save_label);
    
    // 取消按钮
    lv_obj_t *ip_cancel_btn = lv_btn_create(ui_ip_setting_screen);
    lv_obj_set_size(ip_cancel_btn, 120, 50);
    lv_obj_align(ip_cancel_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_bg_color(ip_cancel_btn, lv_color_hex(0x999999), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ip_cancel_btn, ip_cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ip_cancel_label = lv_label_create(ip_cancel_btn);
    lv_label_set_text(ip_cancel_label, "返回");
    lv_obj_set_style_text_font(ip_cancel_label, &cn_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(ip_cancel_label);
    
    // 创建键盘 - 占满底部
    ui_keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(ui_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ui_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // 设置键盘的Ready事件，当用户点击OK时关闭键盘
    lv_obj_add_event_cb(ui_keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ui_keyboard, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);
    
    // 加载当前的WiFi设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ssid_input, config.ssid);
        lv_textarea_set_text(ui_password_input, ""); // 不显示密码，即使配置中有密码
        lv_textarea_set_placeholder_text(ui_password_input, "输入密码");
        lv_textarea_set_text(ui_ip_input, config.device_ip);
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
}

// 键盘ready回调 - 用于处理键盘OK和Cancel按钮
static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// WiFi设置保存按钮回调
static void wifi_save_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Saving WiFi settings");
    
    // 获取输入的值
    const char *ssid = lv_textarea_get_text(ui_ssid_input);
    const char *password = lv_textarea_get_text(ui_password_input);
    
    // 验证SSID不为空
    if (strlen(ssid) == 0) {
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "Error", "WiFi SSID cannot be empty", NULL, true);
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
        lv_obj_t *alert = lv_msgbox_create(NULL, "Error", "Failed to save WiFi settings", NULL, true);
        lv_obj_center(alert);
        return;
    }
    
    // 自动连接WiFi
    wifi_manager_connect();
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 关闭设置页面
    settings_ui_close_wifi_settings();
    
    // 调用设置改变回调
    if (change_callback) {
        change_callback();
    }
}

// WiFi设置取消按钮回调
static void wifi_cancel_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Cancel WiFi Settings");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 关闭设置页面
    settings_ui_close_wifi_settings();
}

// IP设置保存按钮回调
static void ip_save_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Saving IP settings");
    
    // 获取输入的值
    const char *ip = lv_textarea_get_text(ui_ip_input);
    
    // 验证IP不为空
    if (strlen(ip) == 0) {
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "Error", "Device IP cannot be empty", NULL, true);
        lv_obj_center(alert);
        return;
    }
    
    // 创建配置
    wifi_user_config_t config;
    wifi_manager_get_config(&config); // 获取当前配置
    
    // 更新设置
    strncpy(config.device_ip, ip, MAX_IP_LEN);
    
    // 保存配置
    esp_err_t err = wifi_manager_set_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save IP config: %s", esp_err_to_name(err));
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "Error", "Failed to save IP settings", NULL, true);
        lv_obj_center(alert);
        return;
    }
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 关闭设置页面
    settings_ui_close_ip_settings();
    
    // 更新数据URL
    char url[128];
    snprintf(url, sizeof(url), "http://%s/metrics", ip);
    power_monitor_set_data_url(url);
    
    // 调用设置改变回调
    if (change_callback) {
        change_callback();
    }
}

// IP设置取消按钮回调
static void ip_cancel_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Cancel IP settings");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 关闭设置页面
    settings_ui_close_ip_settings();
}

// 打开WiFi设置页面
void settings_ui_open_wifi_settings(void)
{
    ESP_LOGI(TAG, "Opening WiFi settings page");
    
    // 加载当前的WiFi设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ssid_input, config.ssid);
        lv_textarea_set_text(ui_password_input, ""); // 不显示密码，即使配置中有密码
        lv_textarea_set_placeholder_text(ui_password_input, "输入密码");
    }
    
    // 加载WiFi设置页面
    lv_scr_load(ui_wifi_setting_screen);
    
    // 确保键盘隐藏
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// 关闭WiFi设置页面
void settings_ui_close_wifi_settings(void)
{
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 标记WiFi设置页面为非活动
    lv_obj_clear_flag(ui_wifi_setting_screen, LV_OBJ_FLAG_HIDDEN);
    
    // 获取主屏幕并加载
    lv_obj_t *main_screen = get_main_screen();
    if (main_screen != NULL) {
        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    }
}

// 打开IP设置页面
void settings_ui_open_ip_settings(void)
{
    ESP_LOGI(TAG, "Opening IP settings page");
    
    // 加载当前的IP设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ip_input, config.device_ip);
    }
    
    // 加载设置页面
    lv_scr_load(ui_ip_setting_screen);
    
    // 确保键盘隐藏
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// 关闭IP设置页面
void settings_ui_close_ip_settings(void)
{
    ESP_LOGI(TAG, "Closing IP settings page");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 标记IP设置页面为非活动
    lv_obj_clear_flag(ui_ip_setting_screen, LV_OBJ_FLAG_HIDDEN);
    
    // 获取主屏幕并加载
    lv_obj_t *main_screen = get_main_screen();
    if (main_screen != NULL) {
        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    }
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