#include "Display_Manager.h"

static const char *TAG = "DISPLAY_MANAGER";

static lv_obj_t* ap_screen = NULL;
static lv_obj_t* monitor_screen = NULL;
static lv_obj_t* current_screen = NULL;
static lv_obj_t* wifi_error_screen = NULL;

void display_manager_init(void) {
    ESP_LOGI(TAG, "初始化显示管理器");
    
    // 创建监控屏幕（主屏幕）
    monitor_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(monitor_screen, lv_color_black(), 0);
    
    // 设置当前屏幕为监控屏幕
    current_screen = monitor_screen;
    lv_scr_load(monitor_screen);
}

void display_manager_create_ap_screen(const char* ssid, const char* ip) {
    ESP_LOGI(TAG, "创建AP配置屏幕 SSID:%s IP:%s", ssid, ip);
    
    if (ap_screen != NULL) {
        display_manager_delete_ap_screen();
    }
    
    // 创建AP配置屏幕
    ap_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ap_screen, lv_color_black(), 0);
    
    // 创建标题
    lv_obj_t* title = lv_label_create(ap_screen);
    lv_label_set_text(title, "WiFi设置");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);  // 顶部居中
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    
    // 创建容器来组织内容
    lv_obj_t* cont = lv_obj_create(ap_screen);
    lv_obj_set_size(cont, 280, 80);  // 保持宽度足够显示内容
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);  // 居中对齐
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    
    // 创建SSID信息
    lv_obj_t* ssid_label = lv_label_create(cont);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);
    char ssid_text[64];
    lv_snprintf(ssid_text, sizeof(ssid_text), "网络: %s", ssid);
    lv_label_set_text(ssid_label, ssid_text);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, 0, 0);  // 容器内顶部居中
    
    // 创建IP信息
    lv_obj_t* ip_label = lv_label_create(cont);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ip_label, lv_color_white(), 0);
    char ip_text[64];
    lv_snprintf(ip_text, sizeof(ip_text), "设置地址: %s", ip);
    lv_label_set_text(ip_label, ip_text);
    lv_obj_align(ip_label, LV_ALIGN_TOP_MID, 0, 40);  // 容器内居中，距顶部40px
    
    // 切换到AP屏幕
    current_screen = ap_screen;
    lv_scr_load(ap_screen);
}

void display_manager_delete_ap_screen(void) {
    if (ap_screen != NULL) {
        lv_obj_del(ap_screen);
        ap_screen = NULL;
        ESP_LOGI(TAG, "删除AP配置屏幕");
    }
}

void display_manager_show_monitor_screen(void) {
    if (monitor_screen != NULL) {
        current_screen = monitor_screen;
        lv_scr_load(monitor_screen);
        ESP_LOGI(TAG, "显示监控屏幕");
    }
}

bool display_manager_is_ap_screen_active(void) {
    return current_screen == ap_screen;
}

void display_manager_create_wifi_error_screen(void) {
    ESP_LOGI(TAG, "创建WiFi错误屏幕");
    
    if (wifi_error_screen != NULL) {
        display_manager_delete_wifi_error_screen();
    }
    
    // 创建WiFi错误屏幕
    wifi_error_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(wifi_error_screen, lv_color_black(), 0);
    
    // 创建错误标题
    lv_obj_t* title = lv_label_create(wifi_error_screen);
    lv_label_set_text(title, "WiFi连接失败");
    lv_obj_set_style_text_color(title, lv_color_make(0xFF, 0x00, 0x00), 0);  // 红色
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // 创建提示信息
    lv_obj_t* message = lv_label_create(wifi_error_screen);
    lv_label_set_text(message, "请检查您的WiFi设置\n正在尝试重新连接...");
    lv_obj_set_style_text_color(message, lv_color_white(), 0);
    lv_obj_set_style_text_font(message, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, 0);
    
    // 切换到错误屏幕
    current_screen = wifi_error_screen;
    lv_scr_load(wifi_error_screen);
}

void display_manager_delete_wifi_error_screen(void) {
    if (wifi_error_screen != NULL) {
        lv_obj_del(wifi_error_screen);
        wifi_error_screen = NULL;
        ESP_LOGI(TAG, "删除WiFi错误屏幕");
    }
}

bool display_manager_is_wifi_error_screen_active(void) {
    return current_screen == wifi_error_screen;
} 