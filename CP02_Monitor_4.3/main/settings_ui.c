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
static lv_obj_t *ui_auto_connect_cb = NULL;

// 设置改变回调
static settings_change_cb_t change_callback = NULL;

// 前向声明
static void wifi_save_btn_event_cb(lv_event_t *e);
static void wifi_cancel_btn_event_cb(lv_event_t *e);
static void ip_save_btn_event_cb(lv_event_t *e);
static void ip_cancel_btn_event_cb(lv_event_t *e);
static void input_focused_cb(lv_event_t *e);

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
    // 创建WiFi设置页面
    ui_wifi_setting_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_wifi_setting_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 页面标题
    lv_obj_t *wifi_title = lv_label_create(ui_wifi_setting_screen);
    lv_label_set_text(wifi_title, "WiFi设置");
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // SSID标签
    lv_obj_t *ssid_label = lv_label_create(ui_wifi_setting_screen);
    lv_label_set_text(ssid_label, "WiFi名称:");
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 20, 50);
    
    // SSID输入框
    ui_ssid_input = lv_textarea_create(ui_wifi_setting_screen);
    lv_obj_set_size(ui_ssid_input, 200, 40);
    lv_obj_align(ui_ssid_input, LV_ALIGN_TOP_LEFT, 100, 45);
    lv_textarea_set_placeholder_text(ui_ssid_input, "输入WiFi名称");
    lv_obj_add_event_cb(ui_ssid_input, input_focused_cb, LV_EVENT_FOCUSED, NULL);
    
    // 密码标签
    lv_obj_t *password_label = lv_label_create(ui_wifi_setting_screen);
    lv_label_set_text(password_label, "密   码:");
    lv_obj_set_style_text_color(password_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(password_label, LV_ALIGN_TOP_LEFT, 20, 100);
    
    // 密码输入框
    ui_password_input = lv_textarea_create(ui_wifi_setting_screen);
    lv_obj_set_size(ui_password_input, 200, 40);
    lv_obj_align(ui_password_input, LV_ALIGN_TOP_LEFT, 100, 95);
    lv_textarea_set_placeholder_text(ui_password_input, "输入WiFi密码");
    lv_textarea_set_password_mode(ui_password_input, true);
    lv_obj_add_event_cb(ui_password_input, input_focused_cb, LV_EVENT_FOCUSED, NULL);
    
    // 自动连接复选框
    ui_auto_connect_cb = lv_checkbox_create(ui_wifi_setting_screen);
    lv_checkbox_set_text(ui_auto_connect_cb, "自动连接");
    lv_obj_set_style_text_color(ui_auto_connect_cb, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_auto_connect_cb, LV_ALIGN_TOP_LEFT, 20, 150);
    
    // 保存按钮
    lv_obj_t *wifi_save_btn = lv_btn_create(ui_wifi_setting_screen);
    lv_obj_set_size(wifi_save_btn, 100, 40);
    lv_obj_align(wifi_save_btn, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_add_event_cb(wifi_save_btn, wifi_save_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *wifi_save_label = lv_label_create(wifi_save_btn);
    lv_label_set_text(wifi_save_label, "保存");
    lv_obj_center(wifi_save_label);
    
    // 取消按钮
    lv_obj_t *wifi_cancel_btn = lv_btn_create(ui_wifi_setting_screen);
    lv_obj_set_size(wifi_cancel_btn, 100, 40);
    lv_obj_align(wifi_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
    lv_obj_add_event_cb(wifi_cancel_btn, wifi_cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *wifi_cancel_label = lv_label_create(wifi_cancel_btn);
    lv_label_set_text(wifi_cancel_label, "取消");
    lv_obj_center(wifi_cancel_label);
    
    // 创建IP设置页面
    ui_ip_setting_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_ip_setting_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 页面标题
    lv_obj_t *ip_title = lv_label_create(ui_ip_setting_screen);
    lv_label_set_text(ip_title, "设备IP设置");
    lv_obj_set_style_text_color(ip_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ip_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_title, LV_ALIGN_TOP_MID, 0, 10);
    
    // IP标签
    lv_obj_t *ip_label = lv_label_create(ui_ip_setting_screen);
    lv_label_set_text(ip_label, "设备IP:");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 20, 50);
    
    // IP输入框
    ui_ip_input = lv_textarea_create(ui_ip_setting_screen);
    lv_obj_set_size(ui_ip_input, 200, 40);
    lv_obj_align(ui_ip_input, LV_ALIGN_TOP_LEFT, 100, 45);
    lv_textarea_set_placeholder_text(ui_ip_input, "输入设备IP地址");
    lv_obj_add_event_cb(ui_ip_input, input_focused_cb, LV_EVENT_FOCUSED, NULL);
    
    // 说明标签
    lv_obj_t *ip_desc = lv_label_create(ui_ip_setting_screen);
    lv_label_set_text(ip_desc, "注意：此处设置小电拼设备的IP地址\n格式为：192.168.1.100");
    lv_obj_set_style_text_color(ip_desc, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ip_desc, LV_ALIGN_TOP_LEFT, 20, 100);
    
    // 保存按钮
    lv_obj_t *ip_save_btn = lv_btn_create(ui_ip_setting_screen);
    lv_obj_set_size(ip_save_btn, 100, 40);
    lv_obj_align(ip_save_btn, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_add_event_cb(ip_save_btn, ip_save_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ip_save_label = lv_label_create(ip_save_btn);
    lv_label_set_text(ip_save_label, "保存");
    lv_obj_center(ip_save_label);
    
    // 取消按钮
    lv_obj_t *ip_cancel_btn = lv_btn_create(ui_ip_setting_screen);
    lv_obj_set_size(ip_cancel_btn, 100, 40);
    lv_obj_align(ip_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
    lv_obj_add_event_cb(ip_cancel_btn, ip_cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ip_cancel_label = lv_label_create(ip_cancel_btn);
    lv_label_set_text(ip_cancel_label, "取消");
    lv_obj_center(ip_cancel_label);
    
    // 创建键盘
    ui_keyboard = lv_keyboard_create(NULL);
    lv_keyboard_set_mode(ui_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 加载当前的WiFi设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ssid_input, config.ssid);
        lv_textarea_set_text(ui_password_input, config.password);
        lv_textarea_set_text(ui_ip_input, config.device_ip);
        lv_obj_set_state(ui_auto_connect_cb, config.auto_connect ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    }
}

// 输入框聚焦回调
static void input_focused_cb(lv_event_t *e)
{
    lv_obj_t *textarea = lv_event_get_target(e);
    
    // 显示键盘并设置目标
    lv_keyboard_set_textarea(ui_keyboard, textarea);
    lv_obj_clear_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// WiFi设置保存按钮回调
static void wifi_save_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "保存WiFi设置");
    
    // 获取输入的值
    const char *ssid = lv_textarea_get_text(ui_ssid_input);
    const char *password = lv_textarea_get_text(ui_password_input);
    bool auto_connect = lv_obj_has_state(ui_auto_connect_cb, LV_STATE_CHECKED);
    
    // 验证SSID不为空
    if (strlen(ssid) == 0) {
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "错误", "WiFi名称不能为空", NULL, true);
        lv_obj_center(alert);
        return;
    }
    
    // 创建配置
    wifi_user_config_t config;
    wifi_manager_get_config(&config); // 获取当前配置
    
    // 更新设置
    strncpy(config.ssid, ssid, MAX_SSID_LEN);
    strncpy(config.password, password, MAX_PASS_LEN);
    config.auto_connect = auto_connect;
    
    // 保存配置
    esp_err_t err = wifi_manager_set_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存WiFi配置失败: %s", esp_err_to_name(err));
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "错误", "保存WiFi设置失败", NULL, true);
        lv_obj_center(alert);
        return;
    }
    
    // 如果启用了自动连接，则连接WiFi
    if (auto_connect) {
        wifi_manager_connect();
    }
    
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
    ESP_LOGI(TAG, "取消WiFi设置");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 关闭设置页面
    settings_ui_close_wifi_settings();
}

// IP设置保存按钮回调
static void ip_save_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "保存IP设置");
    
    // 获取输入的值
    const char *ip = lv_textarea_get_text(ui_ip_input);
    
    // 验证IP不为空
    if (strlen(ip) == 0) {
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "错误", "设备IP不能为空", NULL, true);
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
        ESP_LOGE(TAG, "保存IP配置失败: %s", esp_err_to_name(err));
        // 显示错误消息
        lv_obj_t *alert = lv_msgbox_create(NULL, "错误", "保存IP设置失败", NULL, true);
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
    ESP_LOGI(TAG, "取消IP设置");
    
    // 隐藏键盘
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
    
    // 关闭设置页面
    settings_ui_close_ip_settings();
}

// 打开WiFi设置页面
void settings_ui_open_wifi_settings(void)
{
    ESP_LOGI(TAG, "打开WiFi设置页面");
    
    // 加载当前的WiFi设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ssid_input, config.ssid);
        lv_textarea_set_text(ui_password_input, config.password);
        lv_obj_set_state(ui_auto_connect_cb, config.auto_connect ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    }
    
    // 加载设置页面
    lv_scr_load(ui_wifi_setting_screen);
}

// 关闭WiFi设置页面
void settings_ui_close_wifi_settings(void)
{
    ESP_LOGI(TAG, "关闭WiFi设置页面");
    
    // 返回到主页面
    lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

// 打开IP设置页面
void settings_ui_open_ip_settings(void)
{
    ESP_LOGI(TAG, "打开IP设置页面");
    
    // 加载当前的IP设置
    wifi_user_config_t config;
    if (wifi_manager_get_config(&config) == ESP_OK) {
        lv_textarea_set_text(ui_ip_input, config.device_ip);
    }
    
    // 加载设置页面
    lv_scr_load(ui_ip_setting_screen);
}

// 关闭IP设置页面
void settings_ui_close_ip_settings(void)
{
    ESP_LOGI(TAG, "关闭IP设置页面");
    
    // 返回到主页面
    lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
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