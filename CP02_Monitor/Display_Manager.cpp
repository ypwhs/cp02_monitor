#include "Display_Manager.h"

lv_obj_t* DisplayManager::apScreen = nullptr;
lv_obj_t* DisplayManager::monitorScreen = nullptr;
lv_obj_t* DisplayManager::currentScreen = nullptr;
lv_obj_t* DisplayManager::wifiErrorScreen = nullptr;

void DisplayManager::init() {
    // 创建监控屏幕（主屏幕）
    monitorScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(monitorScreen, lv_color_black(), 0);
    
    // 设置当前屏幕为监控屏幕
    currentScreen = monitorScreen;
    lv_scr_load(monitorScreen);
}

void DisplayManager::createAPScreen(const char* ssid, const char* ip) {
    if (apScreen != nullptr) {
        deleteAPScreen();
    }
    
    // 创建AP配置屏幕
    apScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(apScreen, lv_color_black(), 0);
    
    // 创建屏幕内容
    createAPScreenContent(ssid, ip);
    
    // 切换到AP屏幕
    currentScreen = apScreen;
    lv_scr_load(apScreen);
}

void DisplayManager::createAPScreenContent(const char* ssid, const char* ip) {
    // 创建标题
    lv_obj_t* title = lv_label_create(apScreen);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);  // 顶部居中
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    
    // 创建容器来组织内容
    lv_obj_t* cont = lv_obj_create(apScreen);
    lv_obj_set_size(cont, 280, 80);  // 保持宽度足够显示内容
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);  // 居中对齐
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    
    // 创建SSID信息
    lv_obj_t* ssidLabel = lv_label_create(cont);
    lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssidLabel, lv_color_white(), 0);
    String ssidText = String("Network: ") + ssid;
    lv_label_set_text(ssidLabel, ssidText.c_str());
    lv_obj_align(ssidLabel, LV_ALIGN_TOP_MID, 0, 0);  // 容器内顶部居中
    
    // 创建IP信息
    lv_obj_t* ipLabel = lv_label_create(cont);
    lv_obj_set_style_text_font(ipLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ipLabel, lv_color_white(), 0);
    String ipText = String("Setup URL: ") + ip;
    lv_label_set_text(ipLabel, ipText.c_str());
    lv_obj_align(ipLabel, LV_ALIGN_TOP_MID, 0, 40);  // 容器内居中，距顶部40px
}

void DisplayManager::deleteAPScreen() {
    if (apScreen != nullptr) {
        lv_obj_del(apScreen);
        apScreen = nullptr;
    }
}

void DisplayManager::showMonitorScreen() {
    if (monitorScreen != nullptr) {
        currentScreen = monitorScreen;

        /*/ 创建屏幕
    monitorScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(monitorScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 标题
    ui_title = lv_label_create(monitorScreen);
    lv_label_set_text(ui_title, "Power Monitor");
    lv_obj_set_style_text_color(ui_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, 5);
    
    // WiFi状态
    ui_wifi_status = lv_label_create(monitorScreen);
    lv_label_set_text(ui_wifi_status, "WiFi");
    lv_obj_set_style_text_color(ui_wifi_status, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_wifi_status, LV_ALIGN_TOP_RIGHT, -10, 5);
    
    // 屏幕高度只有172像素，布局需要紧凑
    uint8_t start_y = 30;
    uint8_t item_height = 22;
    
    // 为每个端口创建UI元素
    for (int i = 0; i < MAX_PORTS; i++) {
        // 端口名称标签
        ui_port_labels[i] = lv_label_create(monitorScreen);
        lv_label_set_text_fmt(ui_port_labels[i], "%s:", portInfos[i].name);
        lv_obj_set_style_text_color(ui_port_labels[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_port_labels[i], LV_ALIGN_TOP_LEFT, 10, start_y + i * item_height);
        
        // 功率值标签
        ui_power_values[i] = lv_label_create(monitorScreen);
        lv_label_set_text(ui_power_values[i], "0.00W");
        lv_obj_set_style_text_color(ui_power_values[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_power_values[i], LV_ALIGN_TOP_LEFT, 45, start_y + i * item_height);
        
        
        // 功率进度条 - 带渐变色
        ui_power_bars[i] = lv_bar_create(monitorScreen);
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
    ui_total_label = lv_label_create(monitorScreen);
    lv_label_set_text(ui_total_label, "Total: 0W");
    lv_obj_set_style_text_color(ui_total_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_total_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_total_label, LV_ALIGN_TOP_LEFT, 10, start_y + MAX_PORTS * item_height + 5);
    
    // 总功率进度条 - 带渐变色
    ui_total_bar = lv_bar_create(monitorScreen);
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
    */
    // 加载屏幕
    lv_scr_load(monitorScreen);
    
    }
}

bool DisplayManager::isAPScreenActive() {
    return currentScreen == apScreen;
}

void DisplayManager::createWiFiErrorScreen() {
    if (wifiErrorScreen != nullptr) {
        deleteWiFiErrorScreen();
    }
    
    // 创建WiFi错误屏幕
    wifiErrorScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(wifiErrorScreen, lv_color_black(), 0);
    
    // 创建错误标题
    lv_obj_t* title = lv_label_create(wifiErrorScreen);
    lv_label_set_text(title, "WiFi Connection Failed");
    lv_obj_set_style_text_color(title, lv_color_make(0xFF, 0x00, 0x00), 0);  // 红色
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    // 创建提示信息
    lv_obj_t* message = lv_label_create(wifiErrorScreen);
    lv_label_set_text(message, "Please check your WiFi settings\nRetrying connection...");
    lv_obj_set_style_text_color(message, lv_color_white(), 0);
    lv_obj_set_style_text_font(message, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, 0);
    
    // 切换到错误屏幕
    currentScreen = wifiErrorScreen;
    lv_scr_load(wifiErrorScreen);
}

void DisplayManager::deleteWiFiErrorScreen() {
    if (wifiErrorScreen != nullptr) {
        lv_obj_del(wifiErrorScreen);
        wifiErrorScreen = nullptr;
    }
}

bool DisplayManager::isWiFiErrorScreenActive() {
    return currentScreen == wifiErrorScreen;
} 