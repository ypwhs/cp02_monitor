#include "Display_Manager.h"
#include "LVGL_Driver.h"  // 新增：包含LVGL驱动头文件
#include "Config_Manager.h"  // 新增：包含配置管理器头文件
#include <time.h>

// 静态成员初始化
lv_obj_t* DisplayManager::mainScreen = nullptr;
lv_obj_t* DisplayManager::currentScreen = nullptr;

// WiFi错误UI组件
lv_obj_t* DisplayManager::wifiErrorTitle = nullptr;
lv_obj_t* DisplayManager::wifiErrorMessage = nullptr;
lv_obj_t* DisplayManager::wifiErrorContainer = nullptr;

// 时间显示UI组件
lv_obj_t* DisplayManager::timeContainer = nullptr;
lv_obj_t* DisplayManager::timeLabel = nullptr;
lv_obj_t* DisplayManager::dateLabel = nullptr;

// 电源监控UI组件
lv_obj_t* DisplayManager::powerMonitorContainer = nullptr;
lv_obj_t* DisplayManager::ui_title = nullptr;
lv_obj_t* DisplayManager::ui_total_label = nullptr;
lv_obj_t* DisplayManager::ui_port_labels[MAX_PORTS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t* DisplayManager::ui_power_values[MAX_PORTS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t* DisplayManager::ui_power_bars[MAX_PORTS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t* DisplayManager::ui_total_bar = nullptr;
lv_obj_t* DisplayManager::ui_wifi_status = nullptr;

// 扫描界面UI组件
lv_obj_t* DisplayManager::scanContainer = nullptr;
lv_obj_t* DisplayManager::scanLabel = nullptr;
lv_obj_t* DisplayManager::scanStatus = nullptr;

// 状态标志初始化
bool DisplayManager::apScreenActive = false;
bool DisplayManager::wifiErrorScreenActive = false;
bool DisplayManager::timeScreenActive = false;
bool DisplayManager::powerMonitorScreenActive = false;
bool DisplayManager::scanScreenActive = false;
bool DisplayManager::dataError = false;
unsigned long DisplayManager::screenSwitchTime = 0;
int DisplayManager::currentRotation = 90; // 默认为90度旋转

// AP配置UI组件
lv_obj_t* DisplayManager::apContainer = nullptr;
lv_obj_t* DisplayManager::apTitle = nullptr;
lv_obj_t* DisplayManager::apContent = nullptr;

SemaphoreHandle_t DisplayManager::lvgl_mutex = nullptr;

// 添加静态变量记录上次时间
static int lastHour = -1;
static int lastMin = -1;
static int lastSec = -1;

void DisplayManager::init() {
    // 创建LVGL互斥锁
    lvgl_mutex = xSemaphoreCreateMutex();
    if (lvgl_mutex == nullptr) {
        printf("[Display] Failed to create LVGL mutex\n");
        return;
    }
    
    // 创建主屏幕
    createMainScreen();
}

void DisplayManager::createMainScreen() {
    if (mainScreen == nullptr) {
        takeLvglLock();
        mainScreen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(mainScreen, lv_color_black(), 0);
        currentScreen = mainScreen;
        lv_scr_load(mainScreen);
        giveLvglLock();
        printf("[Display] Main screen created successfully\n");
    }
}

void DisplayManager::hideAllContainers() {
    // 注意：此函数假设调用者已经获取了LVGL锁
    if (apContainer) lv_obj_add_flag(apContainer, LV_OBJ_FLAG_HIDDEN);
    if (wifiErrorContainer) lv_obj_add_flag(wifiErrorContainer, LV_OBJ_FLAG_HIDDEN);
    if (timeContainer) lv_obj_add_flag(timeContainer, LV_OBJ_FLAG_HIDDEN);
    if (powerMonitorContainer) lv_obj_add_flag(powerMonitorContainer, LV_OBJ_FLAG_HIDDEN);
    if (scanContainer) lv_obj_add_flag(scanContainer, LV_OBJ_FLAG_HIDDEN);
    
    // 同时重置所有屏幕状态标志，确保状态一致性
    apScreenActive = false;
    wifiErrorScreenActive = false;
    timeScreenActive = false;
    powerMonitorScreenActive = false;
    scanScreenActive = false;
}

void DisplayManager::createWiFiErrorScreen() {
    printf("[Display] Creating WiFi error screen\n");
    
    takeLvglLock();
    
    if (wifiErrorScreenActive) {
        printf("[Display] WiFi error screen already active\n");
        giveLvglLock();
        return;
    }
    
    // 检查状态一致性
    if (!isValidScreenState()) {
        printf("[Display] Invalid screen state detected, resetting...\n");
        resetAllScreenStates();
        takeLvglLock(); // 重新获取锁，因为resetAllScreenStates会释放锁
    }
    
    hideAllContainers();
    
    // 获取当前屏幕方向
    int rotation = getCurrentRotation();
    bool isVertical = (rotation == 90 || rotation == 270);
    
    // 如果容器不存在，创建它
    if (wifiErrorContainer == nullptr) {
        wifiErrorContainer = lv_obj_create(mainScreen);
        lv_obj_set_size(wifiErrorContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(wifiErrorContainer, lv_color_black(), 0);
        lv_obj_set_style_border_width(wifiErrorContainer, 0, 0);
        
        // 垂直布局 (90度或270度旋转) - 与水平风格保持一致
        if (isVertical) {
            // 禁用wifiErrorContainer的滚动功能
            lv_obj_clear_flag(wifiErrorContainer, LV_OBJ_FLAG_SCROLLABLE);
            
            // 创建背景 - 保持与水平布局相同风格
            lv_obj_t* errorBg = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(errorBg, 320, 172);
            lv_obj_set_style_bg_color(errorBg, lv_color_hex(0x1A0000), 0);
            lv_obj_set_style_border_width(errorBg, 0, 0);
            lv_obj_align(errorBg, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(errorBg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(errorBg, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            lv_obj_move_background(errorBg);
            
            // 添加顶部装饰 - 与水平布局保持一致
            lv_obj_t* topDecor = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(topDecor, 320, 3);
            lv_obj_set_style_bg_color(topDecor, lv_color_hex(0xFF3333), 0);
            lv_obj_set_style_border_width(topDecor, 0, 0);
            lv_obj_align(topDecor, LV_ALIGN_TOP_MID, 0, 0);
            
            // 添加底部装饰 - 与水平布局保持一致
            lv_obj_t* bottomDecor = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(bottomDecor, 320, 3);
            lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0xFF3333), 0);
            lv_obj_set_style_border_width(bottomDecor, 0, 0);
            lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
            
            // 创建错误标题 - 与水平布局保持一致的样式
            wifiErrorTitle = lv_label_create(wifiErrorContainer);
            lv_label_set_text(wifiErrorTitle, "WiFi Error");  // 简短文本
            lv_obj_set_style_text_color(wifiErrorTitle, lv_color_make(0xFF, 0x55, 0x55), 0);
            lv_obj_set_style_text_font(wifiErrorTitle, &lv_font_montserrat_18, 0);
            lv_obj_align(wifiErrorTitle, LV_ALIGN_TOP_MID, 0, 15);
            
            // 创建提示信息框 - 与水平布局保持一致
            lv_obj_t* messageBox = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(messageBox, 220, 50); // 调整尺寸以适应垂直布局
            lv_obj_set_style_radius(messageBox, 8, 0);
            lv_obj_set_style_bg_color(messageBox, lv_color_hex(0x220011), 0);
            lv_obj_set_style_border_width(messageBox, 1, 0);
            lv_obj_set_style_border_color(messageBox, lv_color_hex(0xFF5555), 0);
            lv_obj_align(messageBox, LV_ALIGN_TOP_MID, 0, 50);
            lv_obj_clear_flag(messageBox, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建提示信息 - 与水平布局保持一致
            wifiErrorMessage = lv_label_create(messageBox);
            lv_label_set_text(wifiErrorMessage, "Check WiFi settings\nRetrying...");
            lv_obj_set_style_text_color(wifiErrorMessage, lv_color_white(), 0);
            lv_obj_set_style_text_font(wifiErrorMessage, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(wifiErrorMessage, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(wifiErrorMessage, 200);
            lv_obj_center(wifiErrorMessage);
            
            // 添加状态指示灯 - 与水平布局保持一致
            lv_obj_t* statusDot = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(statusDot, 8, 8);
            lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(statusDot, lv_color_hex(0xFF5555), 0);
            lv_obj_set_style_border_width(statusDot, 0, 0);
            lv_obj_align(statusDot, LV_ALIGN_BOTTOM_MID, 0, -15);
        }
        // 水平布局 (0度或180度旋转) - 适配172*320分辨率
        else {
            // 创建背景 - 保证完全匹配屏幕尺寸
            lv_obj_t* errorBg = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(errorBg, 172, 320);
            lv_obj_set_style_bg_color(errorBg, lv_color_hex(0x1A0000), 0);
            lv_obj_set_style_border_width(errorBg, 0, 0);
            lv_obj_align(errorBg, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(errorBg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(errorBg, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            lv_obj_move_background(errorBg);
            
            // 禁用wifiErrorContainer的滚动功能
            lv_obj_clear_flag(wifiErrorContainer, LV_OBJ_FLAG_SCROLLABLE);

            // 创建错误标题 - 位置调整确保在视窗内
            wifiErrorTitle = lv_label_create(wifiErrorContainer);
            lv_label_set_text(wifiErrorTitle, "WiFi Error");  // 简短文本
            lv_obj_set_style_text_color(wifiErrorTitle, lv_color_make(0xFF, 0x55, 0x55), 0);
            lv_obj_set_style_text_font(wifiErrorTitle, &lv_font_montserrat_18, 0);
            lv_obj_align(wifiErrorTitle, LV_ALIGN_TOP_MID, 0, 15);
            
            // 添加警告图标 - 适当缩小尺寸
            lv_obj_t* warningIcon = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(warningIcon, 50, 50);
            lv_obj_set_style_radius(warningIcon, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(warningIcon, lv_color_hex(0x330000), 0);
            lv_obj_set_style_border_width(warningIcon, 2, 0);
            lv_obj_set_style_border_color(warningIcon, lv_color_hex(0xFF3333), 0);
            lv_obj_align(warningIcon, LV_ALIGN_TOP_MID, 0, 70);
            lv_obj_clear_flag(warningIcon, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 添加错误标识
            lv_obj_t* exclamation = lv_label_create(warningIcon);
            lv_label_set_text(exclamation, "!");
            lv_obj_set_style_text_color(exclamation, lv_color_hex(0xFF5555), 0);
            lv_obj_set_style_text_font(exclamation, &lv_font_montserrat_30, 0);
            lv_obj_center(exclamation);
            
            // 创建提示信息框 - 尺寸调整以适应屏幕
            lv_obj_t* messageBox = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(messageBox, 150, 60);
            lv_obj_set_style_radius(messageBox, 8, 0);
            lv_obj_set_style_bg_color(messageBox, lv_color_hex(0x220011), 0);
            lv_obj_set_style_border_width(messageBox, 1, 0);
            lv_obj_set_style_border_color(messageBox, lv_color_hex(0xFF5555), 0);
            lv_obj_align(messageBox, LV_ALIGN_TOP_MID, 0, 150);
            lv_obj_clear_flag(messageBox, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建提示信息
            wifiErrorMessage = lv_label_create(messageBox);
            lv_label_set_text(wifiErrorMessage, "Check WiFi settings\nRetrying...");
            lv_obj_set_style_text_color(wifiErrorMessage, lv_color_white(), 0);
            lv_obj_set_style_text_font(wifiErrorMessage, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(wifiErrorMessage, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(wifiErrorMessage, 140);
            lv_obj_center(wifiErrorMessage);
            
            // 添加顶部装饰
            lv_obj_t* topDecor = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(topDecor, 172, 3);
            lv_obj_set_style_bg_color(topDecor, lv_color_hex(0xFF3333), 0);
            lv_obj_set_style_border_width(topDecor, 0, 0);
            lv_obj_align(topDecor, LV_ALIGN_TOP_MID, 0, 0);
            
            // 添加底部装饰
            lv_obj_t* bottomDecor = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(bottomDecor, 172, 3);
            lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0xFF3333), 0);
            lv_obj_set_style_border_width(bottomDecor, 0, 0);
            lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
            
            // 添加状态指示灯
            lv_obj_t* statusDot = lv_obj_create(wifiErrorContainer);
            lv_obj_set_size(statusDot, 8, 8);
            lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(statusDot, lv_color_hex(0xFF5555), 0);
            lv_obj_set_style_border_width(statusDot, 0, 0);
            lv_obj_align(statusDot, LV_ALIGN_BOTTOM_MID, 0, -20);
        }
    }
    
    // 显示WiFi错误容器
    lv_obj_clear_flag(wifiErrorContainer, LV_OBJ_FLAG_HIDDEN);
    wifiErrorScreenActive = true;
    
    giveLvglLock();
    
    // 设置为正常亮度
    setScreenBrightness(BRIGHTNESS_NORMAL);
}

void DisplayManager::createTimeScreen() {
    printf("[Display] Creating time screen\n");
    
    takeLvglLock();
    
    if (timeScreenActive) {
        printf("[Display] Time screen already active\n");
        giveLvglLock();
        return;
    }
    
    hideAllContainers();
    
    // 如果容器不存在，创建它
    if (timeContainer == nullptr) {
        timeContainer = lv_obj_create(mainScreen);
        lv_obj_set_size(timeContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(timeContainer, lv_color_black(), 0);
        lv_obj_set_style_border_width(timeContainer, 0, 0);
        
        // 获取当前屏幕方向
        int rotation = getCurrentRotation();
        bool isVertical = (rotation == 90 || rotation == 270);
        
        // 创建背景图案容器
        lv_obj_t* bgContainer = lv_obj_create(timeContainer);
        lv_obj_set_size(bgContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(bgContainer, lv_color_black(), 0);
        lv_obj_set_style_border_width(bgContainer, 0, 0);
        lv_obj_clear_flag(bgContainer, LV_OBJ_FLAG_SCROLLABLE);

        // 垂直布局 (90度或270度旋转)的时钟UI
        if (isVertical) {
            // 创建外部装饰圆环
            lv_obj_t* outerCircle = lv_obj_create(bgContainer);
            lv_obj_set_size(outerCircle, 150, 150);
            lv_obj_set_style_radius(outerCircle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(outerCircle, lv_color_hex(0x111111), 0);
            lv_obj_set_style_border_width(outerCircle, 1, 0);
            lv_obj_set_style_border_color(outerCircle, lv_color_hex(0x333333), 0);
            lv_obj_align(outerCircle, LV_ALIGN_CENTER, 0, 0);
            
            // 创建中间圆环
            lv_obj_t* circle1 = lv_obj_create(bgContainer);
            lv_obj_set_size(circle1, 120, 120);
            lv_obj_set_style_radius(circle1, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle1, lv_color_hex(0x222222), 0);
            lv_obj_set_style_border_width(circle1, 2, 0);
            lv_obj_set_style_border_color(circle1, lv_color_hex(0x444444), 0);
            lv_obj_align(circle1, LV_ALIGN_CENTER, 0, 0);
            
            // 创建内部圆环
            lv_obj_t* circle2 = lv_obj_create(bgContainer);
            lv_obj_set_size(circle2, 100, 100);
            lv_obj_set_style_radius(circle2, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle2, lv_color_hex(0x111111), 0);
            lv_obj_set_style_border_width(circle2, 1, 0);
            lv_obj_set_style_border_color(circle2, lv_color_hex(0x333333), 0);
            lv_obj_align(circle2, LV_ALIGN_CENTER, 0, 0);

            // 创建装饰性弧线
            for(int i = 0; i < 4; i++) {
                lv_obj_t* arc = lv_arc_create(bgContainer);
                lv_obj_set_size(arc, 160, 160);
                lv_arc_set_rotation(arc, i * 90);
                lv_arc_set_bg_angles(arc, 0, 60);
                lv_arc_set_angles(arc, 0, 60);
                lv_obj_set_style_arc_color(arc, lv_color_hex(0x222222), LV_PART_MAIN);
                lv_obj_set_style_arc_color(arc, lv_color_hex(0x0066FF), LV_PART_INDICATOR);
                lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
                lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR);
                lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
            }
            
            // 创建时间标签
            timeLabel = lv_label_create(timeContainer);
            lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, LV_PART_MAIN);
            lv_obj_set_style_text_align(timeLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_width(timeLabel, LV_PCT(100));
            lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, 0);

            // 创建日期标签
            dateLabel = lv_label_create(timeContainer);
            lv_obj_set_style_text_color(dateLabel, lv_color_hex(0x888888), LV_PART_MAIN);
            lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 40);
            lv_label_set_text(dateLabel, ""); // 设置初始空文本
            
            // 创建装饰性点
            for(int i = 0; i < 12; i++) {
                // 主要刻度点
                lv_obj_t* dot = lv_obj_create(bgContainer);
                lv_obj_set_size(dot, i % 3 == 0 ? 6 : 4, i % 3 == 0 ? 6 : 4);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, i % 3 == 0 ? lv_color_hex(0x0066FF) : lv_color_hex(0x666666), 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                
                float angle = i * 30 * 3.14159f / 180;
                int x = 70 * cos(angle);
                int y = 70 * sin(angle);
                lv_obj_align(dot, LV_ALIGN_CENTER, x, y);
                
                // 外圈装饰点
                if(i % 3 == 0) {
                    lv_obj_t* outerDot = lv_obj_create(bgContainer);
                    lv_obj_set_size(outerDot, 3, 3);
                    lv_obj_set_style_radius(outerDot, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(outerDot, lv_color_hex(0x0066FF), 0);
                    lv_obj_set_style_border_width(outerDot, 0, 0);
                    
                    x = 85 * cos(angle);
                    y = 85 * sin(angle);
                    lv_obj_align(outerDot, LV_ALIGN_CENTER, x, y);
                }
            }
        }
        // 水平布局 (0度或180度旋转)
        else {
            // 在水平布局中，我们使用更炫彩的矩形设计
            lv_obj_t* timeBox = lv_obj_create(bgContainer);
            lv_obj_set_size(timeBox, 150, 120);
            lv_obj_set_style_radius(timeBox, 15, 0);
            lv_obj_set_style_bg_color(timeBox, lv_color_hex(0x1A1A3A), 0);
            lv_obj_set_style_border_width(timeBox, 2, 0);
            lv_obj_set_style_border_color(timeBox, lv_color_hex(0x4B55FF), 0);
            lv_obj_align(timeBox, LV_ALIGN_CENTER, 0, 0);
            
            // 添加发光效果
            lv_obj_set_style_shadow_width(timeBox, 20, 0);
            lv_obj_set_style_shadow_color(timeBox, lv_color_hex(0x2233CC), 0);
            lv_obj_set_style_shadow_opa(timeBox, 100, 0);
            
            // 创建渐变背景
            lv_obj_t* innerBox = lv_obj_create(timeBox);
            lv_obj_set_size(innerBox, 140, 110);
            lv_obj_set_style_radius(innerBox, 10, 0);
            lv_obj_set_style_bg_color(innerBox, lv_color_hex(0x2D2D6D), 0);
            lv_obj_set_style_bg_grad_color(innerBox, lv_color_hex(0x000033), 0);
            lv_obj_set_style_bg_grad_dir(innerBox, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_border_width(innerBox, 1, 0);
            lv_obj_set_style_border_color(innerBox, lv_color_hex(0x8A8AFF), 0);
            lv_obj_align(innerBox, LV_ALIGN_CENTER, 0, 0);
            
            // 添加顶部装饰图案
            lv_obj_t* topDecor = lv_obj_create(bgContainer);
            lv_obj_set_size(topDecor, 172, 20);
            lv_obj_set_style_bg_color(topDecor, lv_color_hex(0x000022), 0);
            lv_obj_set_style_bg_grad_color(topDecor, lv_color_hex(0x000088), 0);
            lv_obj_set_style_bg_grad_dir(topDecor, LV_GRAD_DIR_HOR, 0);
            lv_obj_set_style_border_width(topDecor, 0, 0);
            lv_obj_align(topDecor, LV_ALIGN_TOP_MID, 0, 0);
            
            // 添加底部装饰图案
            lv_obj_t* bottomDecor = lv_obj_create(bgContainer);
            lv_obj_set_size(bottomDecor, 172, 20);
            lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x000088), 0);
            lv_obj_set_style_bg_grad_color(bottomDecor, lv_color_hex(0x000022), 0);
            lv_obj_set_style_bg_grad_dir(bottomDecor, LV_GRAD_DIR_HOR, 0);
            lv_obj_set_style_border_width(bottomDecor, 0, 0);
            lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
            
            // 添加装饰性弧线
            for(int i = 0; i < 2; i++) {
                lv_obj_t* arc = lv_arc_create(bgContainer);
                lv_obj_set_size(arc, 100, 100);
                lv_arc_set_rotation(arc, i * 180);
                lv_arc_set_bg_angles(arc, 0, 120);
                lv_arc_set_angles(arc, 0, 120);
                lv_obj_set_style_arc_color(arc, lv_color_hex(0x222266), LV_PART_MAIN);
                lv_obj_set_style_arc_color(arc, lv_color_hex(0x6A64FF), LV_PART_INDICATOR);
                lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
                lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
                lv_obj_align(arc, LV_ALIGN_CENTER, i == 0 ? -90 : 90, 0);
            }
            
            // 水平布局的时间标签 - 使用更好看的字体和颜色
            timeLabel = lv_label_create(timeContainer);
            lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xCCDDFF), LV_PART_MAIN);
            lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_32, LV_PART_MAIN);
            lv_obj_set_style_text_align(timeLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_width(timeLabel, LV_PCT(100));
            lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -15);
            
            // 水平布局的日期标签 - 使用更好看的字体和颜色
            dateLabel = lv_label_create(timeContainer);
            lv_obj_set_style_text_color(dateLabel, lv_color_hex(0x77AAFF), LV_PART_MAIN);
            lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 30);
            lv_label_set_text(dateLabel, ""); // 设置初始空文本
            
            // 添加闪烁的装饰点
            for(int i = 0; i < 4; i++) {
                lv_obj_t* dot = lv_obj_create(bgContainer);
                lv_obj_set_size(dot, 4, 4);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, lv_color_hex(0x44AAFF), 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                
                // 分布在四个角
                int x = (i % 2 == 0) ? -70 : 70;
                int y = (i < 2) ? -140 : 140;
                lv_obj_align(dot, LV_ALIGN_CENTER, x, y);
            }
            
            // 添加边缘装饰条
            for(int i = 0; i < 2; i++) {
                lv_obj_t* sideBar = lv_obj_create(bgContainer);
                lv_obj_set_size(sideBar, 10, 320);
                lv_obj_set_style_bg_color(sideBar, lv_color_hex(0x000044), 0);
                lv_obj_set_style_bg_grad_color(sideBar, lv_color_hex(0x000022), 0);
                lv_obj_set_style_bg_grad_dir(sideBar, LV_GRAD_DIR_VER, 0);
                lv_obj_set_style_border_width(sideBar, 0, 0);
                lv_obj_align(sideBar, LV_ALIGN_CENTER, i == 0 ? -81 : 81, 0);
            }
        }
    }
    
    // 显示时间容器
    lv_obj_clear_flag(timeContainer, LV_OBJ_FLAG_HIDDEN);
    timeScreenActive = true;
    screenSwitchTime = millis();
    
    giveLvglLock();
    
    // 更新时间显示
    updateTimeScreen();
    
    // 设置为低亮度
    setScreenBrightness(BRIGHTNESS_DIM);
}

void DisplayManager::createPowerMonitorScreen() {
    printf("[Display] Creating power monitor screen\n");
    
    takeLvglLock();
    
    if (powerMonitorScreenActive) {
        printf("[Display] Power monitor screen already active\n");
        giveLvglLock();
        return;
    }
    
    hideAllContainers();
    
    // 如果容器不存在，创建它
    if (powerMonitorContainer == nullptr) {
        powerMonitorContainer = lv_obj_create(mainScreen);
        lv_obj_set_size(powerMonitorContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(powerMonitorContainer, lv_color_black(), 0);
        lv_obj_set_style_border_width(powerMonitorContainer, 0, 0);
        // 禁用滑动滑块
        lv_obj_clear_flag(powerMonitorContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        // 创建电源监控内容
        if (!createPowerMonitorContent()) {
            printf("[Display] Failed to create power monitor content\n");
            giveLvglLock();
            return;
        }
    }
    
    // 显示电源监控容器
    lv_obj_clear_flag(powerMonitorContainer, LV_OBJ_FLAG_HIDDEN);
    powerMonitorScreenActive = true;
    
    // 设置为正常亮度
    setScreenBrightness(BRIGHTNESS_NORMAL);
    
    giveLvglLock();
    printf("[Display] Power monitor screen created successfully\n");
}

void DisplayManager::createScanScreen() {
    printf("[Display] Creating scan screen\n");
    
    takeLvglLock();
    
    if (scanScreenActive) {
        printf("[Display] Scan screen already active\n");
        giveLvglLock();
        return;
    }
    
    hideAllContainers();
    
    // 获取当前屏幕方向
    int rotation = getCurrentRotation();
    bool isVertical = (rotation == 90 || rotation == 270);
    
    // 如果容器不存在，创建它
    if (scanContainer == nullptr) {
        scanContainer = lv_obj_create(mainScreen);
        lv_obj_set_size(scanContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(scanContainer, lv_color_black(), 0);
        lv_obj_set_style_border_width(scanContainer, 0, 0);
        
        if (isVertical) {
            // 垂直布局 (90度或270度旋转) - 与水平风格保持一致
            // 禁用scanContainer的滚动功能
            lv_obj_clear_flag(scanContainer, LV_OBJ_FLAG_SCROLLABLE);
            
            // 创建背景 - 确保精确匹配屏幕尺寸
            lv_obj_t* scanBg = lv_obj_create(scanContainer);
            lv_obj_set_size(scanBg, 320, 172);
            lv_obj_set_style_bg_color(scanBg, lv_color_hex(0x001828), 0);
            lv_obj_set_style_border_width(scanBg, 0, 0);
            lv_obj_align(scanBg, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(scanBg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(scanBg, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            lv_obj_move_background(scanBg);
            
            // 创建标题栏 - 确保宽度匹配屏幕
            lv_obj_t* titleBar = lv_obj_create(scanContainer);
            lv_obj_set_size(titleBar, 320, 25);
            lv_obj_set_style_radius(titleBar, 0, 0);
            lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x00AA55), 0);
            lv_obj_set_style_border_width(titleBar, 0, 0);
            lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_clear_flag(titleBar, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建扫描标题
            scanLabel = lv_label_create(scanContainer);
            lv_label_set_text(scanLabel, "Scanning...");
            lv_obj_set_style_text_color(scanLabel, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(scanLabel, &lv_font_montserrat_16, 0);
            lv_obj_align(scanLabel, LV_ALIGN_TOP_MID, 0, 5);
            
            // 创建状态文本框 - 位置和尺寸调整以适应屏幕
            lv_obj_t* statusBox = lv_obj_create(scanContainer);
            lv_obj_set_size(statusBox, 220, 50);
            lv_obj_set_style_radius(statusBox, 5, 0);
            lv_obj_set_style_bg_color(statusBox, lv_color_hex(0x003322), 0);
            lv_obj_set_style_border_width(statusBox, 1, 0);
            lv_obj_set_style_border_color(statusBox, lv_color_hex(0x00AA55), 0);
            lv_obj_align(statusBox, LV_ALIGN_TOP_MID, 0, 60);
            lv_obj_clear_flag(statusBox, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建状态文本
            scanStatus = lv_label_create(statusBox);
            lv_label_set_text(scanStatus, "Looking for cp02...");
            lv_obj_set_style_text_color(scanStatus, lv_color_hex(0x00FF77), 0);
            lv_obj_set_style_text_font(scanStatus, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(scanStatus, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(scanStatus, 200);
            lv_label_set_long_mode(scanStatus, LV_LABEL_LONG_WRAP);
            lv_obj_center(scanStatus);
            
            // 添加底部装饰
            lv_obj_t* bottomDecor = lv_obj_create(scanContainer);
            lv_obj_set_size(bottomDecor, 320, 3);
            lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x00AA55), 0);
            lv_obj_set_style_border_width(bottomDecor, 0, 0);
            lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
        } else {
            // 水平布局 (0度或180度旋转) - 适配172*320分辨率
            // 创建背景 - 确保精确匹配屏幕尺寸
            lv_obj_t* scanBg = lv_obj_create(scanContainer);
            lv_obj_set_size(scanBg, 172, 320);
            lv_obj_set_style_bg_color(scanBg, lv_color_hex(0x001828), 0);
            lv_obj_set_style_border_width(scanBg, 0, 0);
            lv_obj_align(scanBg, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(scanBg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(scanBg, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            lv_obj_move_background(scanBg);
            
            // 禁用scanContainer的滚动功能
            lv_obj_clear_flag(scanContainer, LV_OBJ_FLAG_SCROLLABLE);
            
            // 创建标题栏 - 确保宽度匹配屏幕
            lv_obj_t* titleBar = lv_obj_create(scanContainer);
            lv_obj_set_size(titleBar, 172, 25);
            lv_obj_set_style_radius(titleBar, 0, 0);
            lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x00AA55), 0);
            lv_obj_set_style_border_width(titleBar, 0, 0);
            lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_clear_flag(titleBar, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建扫描标题
            scanLabel = lv_label_create(scanContainer);
            lv_label_set_text(scanLabel, "Scanning...");
            lv_obj_set_style_text_color(scanLabel, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(scanLabel, &lv_font_montserrat_16, 0); // 略微减小字体
            lv_obj_align(scanLabel, LV_ALIGN_TOP_MID, 0, 5);
            
            // 创建中央扫描图标 - 位置调整为不超出边界
            lv_obj_t* scanIcon = lv_obj_create(scanContainer);
            lv_obj_set_size(scanIcon, 60, 60); // 略微减小尺寸
            lv_obj_set_style_radius(scanIcon, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(scanIcon, lv_color_hex(0x002211), 0);
            lv_obj_set_style_border_width(scanIcon, 2, 0);
            lv_obj_set_style_border_color(scanIcon, lv_color_hex(0x00AA55), 0);
            lv_obj_align(scanIcon, LV_ALIGN_TOP_MID, 0, 50);
            lv_obj_clear_flag(scanIcon, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建扫描指示符 - 中心点
            lv_obj_t* centerDot = lv_obj_create(scanIcon);
            lv_obj_set_size(centerDot, 6, 6);
            lv_obj_set_style_radius(centerDot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(centerDot, lv_color_hex(0x00FF77), 0);
            lv_obj_center(centerDot);
            
            // 创建4个扫描点，替代复杂的圆环和线条
            for (int i = 0; i < 4; i++) {
                lv_obj_t* scanDot = lv_obj_create(scanIcon);
                lv_obj_set_size(scanDot, 4, 4);
                lv_obj_set_style_radius(scanDot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(scanDot, lv_color_hex(0x00FF77), 0);
                
                // 计算位置 - 均匀分布在圆周上
                int x = 0, y = 0;
                switch(i) {
                    case 0: x = 20; y = 0; break;   // 右
                    case 1: x = 0; y = 20; break;   // 下
                    case 2: x = -20; y = 0; break;  // 左
                    case 3: x = 0; y = -20; break;  // 上
                }
                
                lv_obj_align(scanDot, LV_ALIGN_CENTER, x, y);
            }
            
            // 创建状态文本框 - 位置和尺寸调整以适应屏幕
            lv_obj_t* statusBox = lv_obj_create(scanContainer);
            lv_obj_set_size(statusBox, 140, 40);
            lv_obj_set_style_radius(statusBox, 5, 0);
            lv_obj_set_style_bg_color(statusBox, lv_color_hex(0x003322), 0);
            lv_obj_set_style_border_width(statusBox, 1, 0);
            lv_obj_set_style_border_color(statusBox, lv_color_hex(0x00AA55), 0);
            lv_obj_align(statusBox, LV_ALIGN_TOP_MID, 0, 130);
            lv_obj_clear_flag(statusBox, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 创建状态文本
            scanStatus = lv_label_create(statusBox);
            lv_label_set_text(scanStatus, "Looking for cp02...");
            lv_obj_set_style_text_color(scanStatus, lv_color_hex(0x00FF77), 0);
            lv_obj_set_style_text_font(scanStatus, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(scanStatus, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(scanStatus, 130);
            lv_label_set_long_mode(scanStatus, LV_LABEL_LONG_WRAP);
            lv_obj_center(scanStatus);
            
            // 添加进度指示条 - 保持在屏幕内
            lv_obj_t* progressBar = lv_obj_create(scanContainer);
            lv_obj_set_size(progressBar, 140, 5);
            lv_obj_set_style_radius(progressBar, 2, 0);
            lv_obj_set_style_bg_color(progressBar, lv_color_hex(0x005533), 0);
            lv_obj_align(progressBar, LV_ALIGN_TOP_MID, 0, 190);
            lv_obj_clear_flag(progressBar, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
            
            // 进度条指示器
            lv_obj_t* progressIndicator = lv_obj_create(progressBar);
            lv_obj_set_size(progressIndicator, 50, 5);
            lv_obj_set_style_radius(progressIndicator, 2, 0);
            lv_obj_set_style_bg_color(progressIndicator, lv_color_hex(0x00FF77), 0);
            lv_obj_align(progressIndicator, LV_ALIGN_LEFT_MID, 0, 0);
            
            // 添加提示标签
            lv_obj_t* hintLabel = lv_label_create(scanContainer);
            lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x00CC66), 0);
            lv_label_set_text(hintLabel, "mDNS discovery");
            lv_obj_align(hintLabel, LV_ALIGN_TOP_MID, 0, 210);
            
            // 添加底部装饰
            lv_obj_t* bottomDecor = lv_obj_create(scanContainer);
            lv_obj_set_size(bottomDecor, 172, 3);
            lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x00AA55), 0);
            lv_obj_set_style_border_width(bottomDecor, 0, 0);
            lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
    }
    
    // 显示扫描容器
    lv_obj_clear_flag(scanContainer, LV_OBJ_FLAG_HIDDEN);
    scanScreenActive = true;
    
    giveLvglLock();
    
    // 设置为正常亮度
    setScreenBrightness(BRIGHTNESS_NORMAL);
}

// 删除屏幕函数修改为隐藏对应容器
void DisplayManager::deleteWiFiErrorScreen() {
    if (wifiErrorContainer != nullptr) {
        takeLvglLock();
        lv_obj_add_flag(wifiErrorContainer, LV_OBJ_FLAG_HIDDEN);
        wifiErrorScreenActive = false;
        giveLvglLock();
    }
}

void DisplayManager::deleteTimeScreen() {
    if (timeContainer != nullptr) {
        takeLvglLock();
        lv_obj_add_flag(timeContainer, LV_OBJ_FLAG_HIDDEN);
        timeScreenActive = false;
        giveLvglLock();
    }
}

void DisplayManager::deletePowerMonitorScreen() {
    if (powerMonitorContainer != nullptr) {
        takeLvglLock();
        lv_obj_add_flag(powerMonitorContainer, LV_OBJ_FLAG_HIDDEN);
        powerMonitorScreenActive = false;
        giveLvglLock();
    }
}

void DisplayManager::deleteScanScreen() {
    if (scanContainer != nullptr) {
        takeLvglLock();
        lv_obj_add_flag(scanContainer, LV_OBJ_FLAG_HIDDEN);
        scanScreenActive = false;
        giveLvglLock();
    }
}

void DisplayManager::createAPScreen(const char* ssid, const char* ip) {
    printf("[Display] Creating AP screen\n");
    
    takeLvglLock();
    
    if (apScreenActive) {
        printf("[Display] AP screen already active\n");
        giveLvglLock();
        return;
    }
    
    hideAllContainers();
    
    // 如果容器不存在，创建它
    if (apContainer == nullptr) {
        apContainer = lv_obj_create(mainScreen);
        lv_obj_set_size(apContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(apContainer, lv_color_black(), 0);
        lv_obj_set_style_border_width(apContainer, 0, 0);
        // 禁用滚动功能
        lv_obj_clear_flag(apContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        // 创建屏幕内容
        createAPScreenContent(ssid, ip);
    }
    
    // 显示AP容器
    lv_obj_clear_flag(apContainer, LV_OBJ_FLAG_HIDDEN);
    apScreenActive = true;
    
    giveLvglLock();
    
    // 设置为正常亮度
    setScreenBrightness(BRIGHTNESS_NORMAL);
}

void DisplayManager::createAPScreenContent(const char* ssid, const char* ip) {
    // 获取当前屏幕方向
    int rotation = getCurrentRotation();
    bool isVertical = (rotation == 90 || rotation == 270);
    
    if (isVertical) {
        // 垂直布局 (90度或270度旋转) - 与水平布局风格一致
        // 创建渐变背景
        lv_obj_t* bgGradient = lv_obj_create(apContainer);
        lv_obj_set_size(bgGradient, 320, 172);
        lv_obj_set_style_bg_color(bgGradient, lv_color_hex(0x001050), 0);
        lv_obj_set_style_bg_grad_color(bgGradient, lv_color_hex(0x003088), 0);
        lv_obj_set_style_bg_grad_dir(bgGradient, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(bgGradient, 0, 0);
        lv_obj_align(bgGradient, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(bgGradient, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(bgGradient, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        lv_obj_move_background(bgGradient);
        
        // 创建标题栏背景
        lv_obj_t* titleBar = lv_obj_create(apContainer);
        lv_obj_set_size(titleBar, 320, 30);
        lv_obj_set_style_radius(titleBar, 0, 0);
        lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x0055AA), 0);
        lv_obj_set_style_bg_grad_color(titleBar, lv_color_hex(0x0088CC), 0);
        lv_obj_set_style_bg_grad_dir(titleBar, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(titleBar, 0, 0);
        lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
        
        // 创建标题
        apTitle = lv_label_create(apContainer);
        lv_label_set_text(apTitle, "WiFi Setup");
        lv_obj_align(apTitle, LV_ALIGN_TOP_MID, 0, 5);
        lv_obj_set_style_text_color(apTitle, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(apTitle, &lv_font_montserrat_18, 0);
        
        // 创建闪亮WiFi图标
        lv_obj_t* wifiIconBg = lv_obj_create(apContainer);
        lv_obj_set_size(wifiIconBg, 60, 60);
        lv_obj_set_style_radius(wifiIconBg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(wifiIconBg, lv_color_hex(0x004488), 0);
        lv_obj_set_style_border_width(wifiIconBg, 2, 0);
        lv_obj_set_style_border_color(wifiIconBg, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_shadow_width(wifiIconBg, 15, 0);
        lv_obj_set_style_shadow_color(wifiIconBg, lv_color_hex(0x0088FF), 0);
        lv_obj_set_style_shadow_opa(wifiIconBg, 100, 0);
        lv_obj_align(wifiIconBg, LV_ALIGN_TOP_MID, 0, 55);
        
        // 创建WiFi符号
        lv_obj_t* wifiSymbol = lv_label_create(wifiIconBg);
        lv_label_set_text(wifiSymbol, "WiFi");
        lv_obj_set_style_text_color(wifiSymbol, lv_color_hex(0x00DDFF), 0);
        lv_obj_set_style_text_font(wifiSymbol, &lv_font_montserrat_14, 0);
        lv_obj_center(wifiSymbol);
        
        // 创建装饰性背景框
        lv_obj_t* decorBox = lv_obj_create(apContainer);
        lv_obj_set_size(decorBox, 280, 80);
        lv_obj_set_style_radius(decorBox, 15, 0);
        lv_obj_set_style_bg_color(decorBox, lv_color_hex(0x002050), 0);
        lv_obj_set_style_bg_grad_color(decorBox, lv_color_hex(0x003070), 0);
        lv_obj_set_style_bg_grad_dir(decorBox, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(decorBox, 2, 0);
        lv_obj_set_style_border_color(decorBox, lv_color_hex(0x00AAFF), 0);
        lv_obj_align(decorBox, LV_ALIGN_BOTTOM_MID, 0, -25);
        lv_obj_clear_flag(decorBox, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        
        // 创建内部信息容器
        apContent = lv_obj_create(decorBox);
        lv_obj_set_size(apContent, 260, 60);
        lv_obj_align(apContent, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(apContent, lv_color_hex(0x001540), 0);
        lv_obj_set_style_radius(apContent, 10, 0);
        lv_obj_set_style_border_width(apContent, 1, 0);
        lv_obj_set_style_border_color(apContent, lv_color_hex(0x0088DD), 0);
        lv_obj_set_style_pad_all(apContent, 5, 0);
        lv_obj_clear_flag(apContent, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        
        // 添加内容标题
        lv_obj_t* contentTitle = lv_label_create(apContent);
        lv_label_set_text(contentTitle, "Connect to:");
        lv_obj_set_style_text_color(contentTitle, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_text_font(contentTitle, &lv_font_montserrat_14, 0);
        lv_obj_align(contentTitle, LV_ALIGN_TOP_LEFT, 5, 5);
        
        // 创建SSID信息
        lv_obj_t* ssidLabel = lv_label_create(apContent);
        lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ssidLabel, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(ssidLabel, ssid);
        lv_obj_align(ssidLabel, LV_ALIGN_TOP_RIGHT, -5, 5);
        
        // 创建URL标签
        lv_obj_t* urlTitle = lv_label_create(apContent);
        lv_label_set_text(urlTitle, "Setup URL:");
        lv_obj_set_style_text_color(urlTitle, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_text_font(urlTitle, &lv_font_montserrat_14, 0);
        lv_obj_align(urlTitle, LV_ALIGN_BOTTOM_LEFT, 5, -5);
        
        // 创建IP信息
        lv_obj_t* ipLabel = lv_label_create(apContent);
        lv_obj_set_style_text_font(ipLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ipLabel, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(ipLabel, ip);
        lv_obj_align(ipLabel, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
        
        // 添加说明提示
        lv_obj_t* hintLabel = lv_label_create(apContainer);
        lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x88CCFF), 0);
        lv_label_set_text(hintLabel, "Connect to network and open URL");
        lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -5);
        
        // 添加底部装饰
        lv_obj_t* bottomDecor = lv_obj_create(apContainer);
        lv_obj_set_size(bottomDecor, 320, 5);
        lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_border_width(bottomDecor, 0, 0);
        lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
        
        // 添加装饰波浪线
        for (int i = 0; i < 3; i++) {
            lv_obj_t* wave = lv_obj_create(apContainer);
            lv_obj_set_size(wave, 320, 2);
            lv_obj_set_style_bg_color(wave, lv_color_hex(0x0088CC), 0);
            lv_obj_set_style_bg_opa(wave, 150, 0);
            lv_obj_set_style_border_width(wave, 0, 0);
            lv_obj_align(wave, LV_ALIGN_BOTTOM_MID, 0, -10 - i * 3);
        }
    } else {
        // 水平布局 (0度或180度旋转)
        // 创建渐变背景
        lv_obj_t* bgGradient = lv_obj_create(apContainer);
        lv_obj_set_size(bgGradient, 172, 320);
        lv_obj_set_style_bg_color(bgGradient, lv_color_hex(0x001050), 0);
        lv_obj_set_style_bg_grad_color(bgGradient, lv_color_hex(0x003088), 0);
        lv_obj_set_style_bg_grad_dir(bgGradient, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(bgGradient, 0, 0);
        lv_obj_align(bgGradient, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(bgGradient, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(bgGradient, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        lv_obj_move_background(bgGradient);
        
        // 创建标题栏背景
        lv_obj_t* titleBar = lv_obj_create(apContainer);
        lv_obj_set_size(titleBar, 172, 40);
        lv_obj_set_style_radius(titleBar, 0, 0);
        lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x0055AA), 0);
        lv_obj_set_style_bg_grad_color(titleBar, lv_color_hex(0x0088CC), 0);
        lv_obj_set_style_bg_grad_dir(titleBar, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(titleBar, 0, 0);
        lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
        
        // 创建标题 - 调整位置
        apTitle = lv_label_create(apContainer);
        lv_label_set_text(apTitle, "WiFi Setup");
        lv_obj_align(apTitle, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_set_style_text_color(apTitle, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(apTitle, &lv_font_montserrat_18, 0);
        
        // 创建闪亮WiFi图标
        lv_obj_t* wifiIconBg = lv_obj_create(apContainer);
        lv_obj_set_size(wifiIconBg, 60, 60);
        lv_obj_set_style_radius(wifiIconBg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(wifiIconBg, lv_color_hex(0x004488), 0);
        lv_obj_set_style_border_width(wifiIconBg, 2, 0);
        lv_obj_set_style_border_color(wifiIconBg, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_shadow_width(wifiIconBg, 15, 0);
        lv_obj_set_style_shadow_color(wifiIconBg, lv_color_hex(0x0088FF), 0);
        lv_obj_set_style_shadow_opa(wifiIconBg, 100, 0);
        lv_obj_align(wifiIconBg, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_clear_flag(wifiIconBg, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        
        // 创建WiFi符号
        lv_obj_t* wifiSymbol = lv_label_create(wifiIconBg);
        lv_label_set_text(wifiSymbol, "WiFi");
        lv_obj_set_style_text_color(wifiSymbol, lv_color_hex(0x00DDFF), 0);
        lv_obj_set_style_text_font(wifiSymbol, &lv_font_montserrat_14, 0);
        lv_obj_center(wifiSymbol);
        
        // 创建装饰性背景框
        lv_obj_t* decorBox = lv_obj_create(apContainer);
        lv_obj_set_size(decorBox, 150, 160);
        lv_obj_set_style_radius(decorBox, 15, 0);
        lv_obj_set_style_bg_color(decorBox, lv_color_hex(0x002050), 0);
        lv_obj_set_style_bg_grad_color(decorBox, lv_color_hex(0x003070), 0);
        lv_obj_set_style_bg_grad_dir(decorBox, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(decorBox, 2, 0);
        lv_obj_set_style_border_color(decorBox, lv_color_hex(0x00AAFF), 0);
        lv_obj_align(decorBox, LV_ALIGN_TOP_MID, 0, 118);
        lv_obj_clear_flag(decorBox, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        
        // 创建内部信息容器
        apContent = lv_obj_create(decorBox);
        lv_obj_set_size(apContent, 130, 140);
        lv_obj_align(apContent, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(apContent, lv_color_hex(0x001540), 0);
        lv_obj_set_style_radius(apContent, 10, 0);
        lv_obj_set_style_border_width(apContent, 1, 0);
        lv_obj_set_style_border_color(apContent, lv_color_hex(0x0088DD), 0);
        lv_obj_set_style_pad_all(apContent, 5, 0);
        lv_obj_clear_flag(apContent, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
        
        // 添加内容标题
        lv_obj_t* contentTitle = lv_label_create(apContent);
        lv_label_set_text(contentTitle, "Connect to:");
        lv_obj_set_style_text_color(contentTitle, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_text_font(contentTitle, &lv_font_montserrat_14, 0);
        lv_obj_align(contentTitle, LV_ALIGN_TOP_MID, 0, 5);
        
        // 创建SSID信息 - 调整布局
        lv_obj_t* ssidLabel = lv_label_create(apContent);
        lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ssidLabel, lv_color_hex(0xFFFFFF), 0);
        String ssidText = String(ssid);
        lv_label_set_text(ssidLabel, ssidText.c_str());
        lv_obj_align(ssidLabel, LV_ALIGN_TOP_MID, 0, 25);
        
        // 创建URL标签
        lv_obj_t* urlTitle = lv_label_create(apContent);
        lv_label_set_text(urlTitle, "Setup URL:");
        lv_obj_set_style_text_color(urlTitle, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_text_font(urlTitle, &lv_font_montserrat_14, 0);
        lv_obj_align(urlTitle, LV_ALIGN_TOP_MID, 0, 55);
        
        // 创建IP信息 - 调整布局
        lv_obj_t* ipLabel = lv_label_create(apContent);
        lv_obj_set_style_text_font(ipLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ipLabel, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(ipLabel, ip);
        lv_obj_align(ipLabel, LV_ALIGN_TOP_MID, 0, 75);
        
        // 添加说明提示
        lv_obj_t* hintLabel = lv_label_create(apContainer);
        lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x88CCFF), 0);
        lv_label_set_text(hintLabel, "Connect to network");
        lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -42);

        lv_obj_t* hintLabel2 = lv_label_create(apContainer);
        lv_obj_set_style_text_font(hintLabel2, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(hintLabel2, lv_color_hex(0x88CCFF), 0);
        lv_label_set_text(hintLabel2, "and open URL");
        lv_obj_align(hintLabel2, LV_ALIGN_BOTTOM_MID, 0, -30);
        
        // 添加底部装饰
        lv_obj_t* bottomDecor = lv_obj_create(apContainer);
        lv_obj_set_size(bottomDecor, 172, 5);
        lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_border_width(bottomDecor, 0, 0);
        lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 0);
        
        // 添加装饰波浪线
        for (int i = 0; i < 3; i++) {
            lv_obj_t* wave = lv_obj_create(apContainer);
            lv_obj_set_size(wave, 172, 2);
            lv_obj_set_style_bg_color(wave, lv_color_hex(0x0088CC), 0);
            lv_obj_set_style_bg_opa(wave, 150, 0);
            lv_obj_set_style_border_width(wave, 0, 0);
            lv_obj_align(wave, LV_ALIGN_TOP_MID, 0, 300 - i * 8);
            lv_obj_clear_flag(wave, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

void DisplayManager::deleteAPScreen() {
    if (apContainer != nullptr) {
        takeLvglLock();
        lv_obj_add_flag(apContainer, LV_OBJ_FLAG_HIDDEN);
        apScreenActive = false;
        giveLvglLock();
    }
}

bool DisplayManager::isAPScreenActive() {
    return apScreenActive;
}

bool DisplayManager::isWiFiErrorScreenActive() {
    return currentScreen == wifiErrorContainer;
}

bool DisplayManager::isTimeScreenActive() {
    return timeScreenActive;
}

void DisplayManager::updateTimeScreen() {
    if (!timeScreenActive || !timeLabel) return;
    
    // 获取当前时间
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // 检查时间是否发生变化
    if (timeinfo.tm_hour == lastHour && 
        timeinfo.tm_min == lastMin && 
        timeinfo.tm_sec == lastSec) {
        return;  // 时间没有变化，不更新显示
    }
    
    // 更新记录的时间
    lastHour = timeinfo.tm_hour;
    lastMin = timeinfo.tm_min;
    lastSec = timeinfo.tm_sec;
    
    takeLvglLock();
    
    // 格式化时间字符串
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
             timeinfo.tm_hour,
             timeinfo.tm_min, 
             timeinfo.tm_sec);
    
    // 更新时间显示
    lv_label_set_text(timeLabel, timeStr);

    // 更新日期显示
    if (dateLabel != nullptr) {
        char dateStr[32];
        snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
                timeinfo.tm_year + 1900,
                timeinfo.tm_mon + 1,
                timeinfo.tm_mday);
        lv_label_set_text(dateLabel, dateStr);
    }
    
    giveLvglLock();
}

bool DisplayManager::createPowerMonitorContent() {
    // 先清除所有现有内容
    if (powerMonitorContainer != nullptr) {
        lv_obj_clean(powerMonitorContainer);
    }

    // 获取当前屏幕方向
    int rotation = getCurrentRotation();
    bool isVertical = (rotation == 90 || rotation == 270);

    // 垂直布局 (90度或270度旋转)
    if (isVertical) {
        // 创建渐变背景
        lv_obj_t* bgDecor = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(bgDecor, 320, 172);
        lv_obj_set_style_bg_color(bgDecor, lv_color_hex(0x001848), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(bgDecor, lv_color_hex(0x301060), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(bgDecor, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(bgDecor, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(bgDecor, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(bgDecor, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(bgDecor, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_background(bgDecor);
        
        // 创建顶部装饰条
        lv_obj_t* topBar = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(topBar, 320, 20);
        lv_obj_set_style_bg_color(topBar, lv_color_hex(0x0066AA), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(topBar, lv_color_hex(0x2200AA), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(topBar, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(topBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(topBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, -10);
        lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);
        
        // 标题
        ui_title = lv_label_create(powerMonitorContainer);
        if (ui_title == nullptr) return false;
        
        lv_label_set_text(ui_title, "Power Monitor");
        lv_obj_set_style_text_color(ui_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, -10);
        
        // 创建总功率显示区域
        lv_obj_t* totalPowerContainer = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(totalPowerContainer, 80, 110);
        lv_obj_set_style_bg_color(totalPowerContainer, lv_color_hex(0x102040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(totalPowerContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(totalPowerContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(totalPowerContainer, LV_ALIGN_TOP_LEFT, 10, 35);
        lv_obj_clear_flag(totalPowerContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        // 创建总功率显示区域 - 使用圆形仪表盘
        lv_obj_t* totalPowerCircle = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(totalPowerCircle, 65, 65);
        lv_obj_set_style_radius(totalPowerCircle, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(totalPowerCircle, lv_color_hex(0x102040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(totalPowerCircle, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(totalPowerCircle, lv_color_hex(0x4466FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(totalPowerCircle, LV_ALIGN_TOP_LEFT, 0, 50);
        lv_obj_clear_flag(totalPowerCircle, LV_OBJ_FLAG_SCROLLABLE);
        
        // 总功率标签
        ui_total_label = lv_label_create(powerMonitorContainer);
        if (ui_total_label == nullptr) return false;
        
        lv_label_set_text(ui_total_label, "0W");
        lv_obj_set_style_text_color(ui_total_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_total_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_total_label, LV_ALIGN_TOP_LEFT, 10, 75);
        
        // 创建总功率弧形进度条
        ui_total_bar = lv_arc_create(powerMonitorContainer);
        if (ui_total_bar == nullptr) return false;
        
        lv_obj_set_size(ui_total_bar, 80, 80);
        lv_obj_align(ui_total_bar, LV_ALIGN_TOP_LEFT, -8, 42);
        lv_arc_set_rotation(ui_total_bar, 135);
        lv_arc_set_bg_angles(ui_total_bar, 0, 270);
        lv_arc_set_range(ui_total_bar, 0, 100);
        lv_arc_set_value(ui_total_bar, 0);
        lv_obj_set_style_arc_width(ui_total_bar, 7, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui_total_bar, lv_color_hex(0x222266), LV_PART_MAIN);
        lv_obj_set_style_arc_width(ui_total_bar, 7, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ui_total_bar, lv_color_hex(0xf039fb), LV_PART_INDICATOR);
        lv_obj_clear_flag(ui_total_bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(ui_total_bar, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(ui_total_bar, LV_OBJ_FLAG_SCROLLABLE);

        //调试看效果值
        //lv_arc_set_value(ui_total_bar, 100);
        
        // 创建端口显示容器
        lv_obj_t* portsContainer = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(portsContainer, 230, 168);
        lv_obj_set_style_bg_color(portsContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(portsContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(portsContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(portsContainer, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_clear_flag(portsContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        // 垂直布局参数 - 现代化设计
        uint8_t port_y_start = 0;
        uint8_t port_height = 28;
        uint8_t bar_width = 160;
        
        // 为每个端口创建UI元素 - 垂直排列但使用现代风格
        for (int i = 0; i < MAX_PORTS; i++) {
            int y_pos = port_y_start + i * port_height;
            
            // 创建端口背景
            lv_obj_t* portBg = lv_obj_create(portsContainer);
            lv_obj_set_size(portBg, 210, 24);
            lv_obj_set_style_radius(portBg, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(portBg, lv_color_hex(0x102030), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(portBg, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(portBg, lv_color_hex(0x3355CC), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(portBg, LV_ALIGN_TOP_MID, 10, y_pos);
            lv_obj_clear_flag(portBg, LV_OBJ_FLAG_SCROLLABLE);
            
            // 功率进度条 - 水平显示
            ui_power_bars[i] = lv_bar_create(portsContainer);
            if (ui_power_bars[i] == nullptr) return false;
            
            lv_obj_set_size(ui_power_bars[i], 206, 20);
            lv_obj_align(ui_power_bars[i], LV_ALIGN_TOP_MID, 10, y_pos + 2);
            lv_bar_set_range(ui_power_bars[i], 0, 100);
            lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);
            lv_obj_clear_flag(ui_power_bars[i], LV_OBJ_FLAG_SCROLLABLE);
            
            // 设置进度条样式 - 渐变色
            lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x88FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_dir(ui_power_bars[i], LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_color(ui_power_bars[i], lv_color_hex(0xFF8800), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        
            //调试看效果值
            //lv_bar_set_value(ui_power_bars[i], 20 + i * 20, LV_ANIM_ON);

            // 端口名称标签
            ui_port_labels[i] = lv_label_create(portsContainer);
            if (ui_port_labels[i] == nullptr) return false;
            
            lv_label_set_text_fmt(ui_port_labels[i], "%s", portInfos[i].name);
            lv_obj_set_style_text_color(ui_port_labels[i], lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(ui_port_labels[i], &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(ui_port_labels[i], LV_ALIGN_TOP_LEFT, 15, y_pos + 4);
            
            // 功率值标签
            ui_power_values[i] = lv_label_create(portsContainer);
            if (ui_power_values[i] == nullptr) return false;
            
            lv_label_set_text(ui_power_values[i], "0.00W");
            lv_obj_set_style_text_color(ui_power_values[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(ui_power_values[i], &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(ui_power_values[i], LV_ALIGN_TOP_RIGHT, -2, y_pos + 4);
        }
        
        // 添加底部装饰
        lv_obj_t* bottomDecor = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(bottomDecor, 320, 3);
        lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x00AAFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(bottomDecor, lv_color_hex(0xAA00FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(bottomDecor, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(bottomDecor, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_clear_flag(bottomDecor, LV_OBJ_FLAG_SCROLLABLE);
    } 
    // 水平布局 (0度或180度旋转)
    else {
        // 在水平布局下创建更炫彩的界面
        // 创建渐变背景
        lv_obj_t* bgDecor = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(bgDecor, 172, 320);
        lv_obj_set_style_bg_color(bgDecor, lv_color_hex(0x001848), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(bgDecor, lv_color_hex(0x301060), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(bgDecor, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(bgDecor, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(bgDecor, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(bgDecor, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(bgDecor, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_background(bgDecor);
        
        // 创建顶部装饰条
        lv_obj_t* topBar = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(topBar, 172, 30);
        lv_obj_set_style_bg_color(topBar, lv_color_hex(0x0066AA), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(topBar, lv_color_hex(0x2200AA), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(topBar, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(topBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(topBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 0);
        
        // 标题
        ui_title = lv_label_create(powerMonitorContainer);
        if (ui_title == nullptr) return false;
        
        lv_label_set_text(ui_title, "Power Monitor");
        lv_obj_set_style_text_color(ui_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, 5);
        
            // 创建总功率显示区域
        lv_obj_t* totalPowerContainer = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(totalPowerContainer, 110, 80);
        lv_obj_set_style_bg_color(totalPowerContainer, lv_color_hex(0x102040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(totalPowerContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(totalPowerContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(totalPowerContainer, LV_ALIGN_TOP_LEFT, 10, 35);
        lv_obj_clear_flag(totalPowerContainer, LV_OBJ_FLAG_SCROLLABLE);

        // 创建总功率显示区域 - 使用圆形仪表盘
        lv_obj_t* totalPowerCircle = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(totalPowerCircle, 70, 70);
        lv_obj_set_style_radius(totalPowerCircle, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(totalPowerCircle, lv_color_hex(0x102040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(totalPowerCircle, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(totalPowerCircle, lv_color_hex(0x4466FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(totalPowerCircle, LV_ALIGN_TOP_MID, 0, 40);
        // 禁用滑动滑块
        lv_obj_clear_flag(totalPowerCircle, LV_OBJ_FLAG_SCROLLABLE);
        
        // 总功率标签
        ui_total_label = lv_label_create(powerMonitorContainer);
        if (ui_total_label == nullptr) return false;
        
        lv_label_set_text(ui_total_label, "0W");
        lv_obj_set_style_text_color(ui_total_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_total_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ui_total_label, LV_ALIGN_TOP_MID, 0, 63);
        
        // 创建总功率弧形进度条
        ui_total_bar = lv_arc_create(powerMonitorContainer);
        if (ui_total_bar == nullptr) return false;
        
        lv_obj_set_size(ui_total_bar, 80, 80);
        lv_obj_align(ui_total_bar, LV_ALIGN_TOP_MID, 0, 35);
        lv_arc_set_rotation(ui_total_bar, 135);
        lv_arc_set_bg_angles(ui_total_bar, 0, 270);
        lv_arc_set_range(ui_total_bar, 0, 100);
        lv_arc_set_value(ui_total_bar, 0);
        lv_obj_set_style_arc_width(ui_total_bar, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui_total_bar, lv_color_hex(0x222266), LV_PART_MAIN);
        lv_obj_set_style_arc_width(ui_total_bar, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ui_total_bar, lv_color_hex(0xf039fb), LV_PART_INDICATOR);
        lv_obj_clear_flag(ui_total_bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(ui_total_bar, NULL, LV_PART_KNOB);
        // 禁用滑动滑块
        lv_obj_clear_flag(ui_total_bar, LV_OBJ_FLAG_SCROLLABLE);
        //调试看效果值
        //lv_arc_set_value(ui_total_bar, 100);
        
        // 创建端口显示容器
        lv_obj_t* portsContainer = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(portsContainer, 172, 300);
        lv_obj_set_style_bg_color(portsContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(portsContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(portsContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(portsContainer, LV_ALIGN_TOP_RIGHT, 5, 0);
        // 禁用滑动滑块
        lv_obj_clear_flag(portsContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        // 水平布局参数
        uint8_t port_y_start = 112;
        uint8_t port_height = 35;
        uint8_t bar_width = 160;
        
        // 为每个端口创建UI元素 - 横向排列
        for (int i = 0; i < MAX_PORTS; i++) {
            int y_pos = port_y_start + i * port_height;
            
            // 创建端口背景
            lv_obj_t* portBg = lv_obj_create(portsContainer);
            lv_obj_set_size(portBg, 160, 23);
            lv_obj_set_style_radius(portBg, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(portBg, lv_color_hex(0x102030), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(portBg, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(portBg, lv_color_hex(0x3355CC), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(portBg, LV_ALIGN_TOP_MID, 5, y_pos);
            // 禁用滑动滑块
            lv_obj_clear_flag(portBg, LV_OBJ_FLAG_SCROLLABLE);
                        
            // 功率进度条 - 水平显示
            ui_power_bars[i] = lv_bar_create(portsContainer);
            if (ui_power_bars[i] == nullptr) return false;
            
            lv_obj_set_size(ui_power_bars[i], 158, 20);
            lv_obj_align(ui_power_bars[i], LV_ALIGN_TOP_MID, 5, y_pos + 1);
            lv_bar_set_range(ui_power_bars[i], 0, 100);
            lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);
            // 禁用滑动滑块
            lv_obj_clear_flag(ui_power_bars[i], LV_OBJ_FLAG_SCROLLABLE);
            
            // 设置进度条样式 - 渐变色
            lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui_power_bars[i], lv_color_hex(0x88FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_dir(ui_power_bars[i], LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_grad_color(ui_power_bars[i], lv_color_hex(0xFF8800), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            //调试看效果值
            //lv_bar_set_value(ui_power_bars[i], 20 + i * 20, LV_ANIM_ON);

            // 端口名称标签
            ui_port_labels[i] = lv_label_create(portsContainer);
            if (ui_port_labels[i] == nullptr) return false;
            
            lv_label_set_text_fmt(ui_port_labels[i], "%s", portInfos[i].name);
            lv_obj_set_style_text_color(ui_port_labels[i], lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(ui_port_labels[i], &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(ui_port_labels[i], LV_ALIGN_TOP_LEFT, 5, y_pos + 4);
            
            // 功率值标签
            ui_power_values[i] = lv_label_create(portsContainer);
            if (ui_power_values[i] == nullptr) return false;
            
            lv_label_set_text(ui_power_values[i], "0.00W");
            lv_obj_set_style_text_color(ui_power_values[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(ui_power_values[i], &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(ui_power_values[i], LV_ALIGN_TOP_RIGHT, 0, y_pos + 4);
        }
        
        // 添加底部装饰
        lv_obj_t* bottomDecor = lv_obj_create(powerMonitorContainer);
        lv_obj_set_size(bottomDecor, 172, 5);
        lv_obj_set_style_bg_color(bottomDecor, lv_color_hex(0x00AAFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(bottomDecor, lv_color_hex(0xAA00FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(bottomDecor, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(bottomDecor, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(bottomDecor, LV_ALIGN_BOTTOM_MID, 0, 7);
        // 禁用滑动滑块
        lv_obj_clear_flag(bottomDecor, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // 设置总功率进度条背景色
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 设置进度条指示器颜色为绿黄色
    lv_obj_set_style_bg_color(ui_total_bar, lv_color_hex(0xf039fb), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 启用水平渐变
    lv_obj_set_style_bg_grad_dir(ui_total_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    // 设置渐变终止颜色为红黄色
    lv_obj_set_style_bg_grad_color(ui_total_bar, lv_color_hex(0xfb3a39), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    
    return true;
}

bool DisplayManager::isPowerMonitorScreenActive() {
    return powerMonitorScreenActive;
}

void DisplayManager::setScreenBrightness(uint8_t brightness) {
    Set_Backlight(brightness);
    printf("[Display] Brightness set to %d\n", brightness);
}

bool DisplayManager::isScanScreenActive() {
    return scanScreenActive;
}

void DisplayManager::updateScanStatus(const char* status) {
    if (scanStatus != nullptr) {
        takeLvglLock();
        lv_label_set_text(scanStatus, status);
        giveLvglLock();
    }
}

void DisplayManager::updatePowerMonitorScreen() {
    if (!powerMonitorScreenActive || powerMonitorContainer == nullptr) {
        return;
    }

    takeLvglLock();

    static char text_buf[128];
    bool isVertical = (getCurrentRotation() == 90 || getCurrentRotation() == 270);
    
    // 更新每个端口的显示
    for (int i = 0; i < MAX_PORTS; i++) {
        if (ui_power_values[i] == nullptr) continue;

        // 启用标签的重着色功能
        lv_label_set_recolor(ui_power_values[i], true);

        if (dataError) {
            lv_label_set_text(ui_power_values[i], "#888888 --.-W#");
            if (ui_power_bars[i] != nullptr) {
                lv_bar_set_value(ui_power_bars[i], 0, LV_ANIM_OFF);
            }
            continue;
        }

        // 根据电压设置颜色
        const char* color_code;
        int voltage_mv = portInfos[i].voltage;
        
        if (voltage_mv > 21000) {
            color_code = "#FF00FF";
        } else if (voltage_mv > 16000) {
            color_code = "#FF0000";
        } else if (voltage_mv > 13000) {
            color_code = "#FF8800";
        } else if (voltage_mv > 10000) {
            color_code = "#FFFF00";
        } else if (voltage_mv > 6000) {
            color_code = "#00FF00";
        } else if (voltage_mv >= 0) {
            color_code = "#FFFFFF";
        } else {
            color_code = "#888888";
        }

        // 更新功率值
        int power_int = (int)(portInfos[i].power * 100);
        // 增加缓冲区大小检查，确保有足够空间
        int needed_size = strlen(color_code) + 20; // 颜色代码 + 数字 + 格式字符
        if (needed_size < sizeof(text_buf)) {
            snprintf(text_buf, sizeof(text_buf), "%s %d.%02dW#", 
                    color_code, 
                    power_int / 100, 
                    power_int % 100);
        } else {
            // 如果缓冲区不够，使用简化格式
            snprintf(text_buf, sizeof(text_buf), "#FFFFFF %d.%02dW#", 
                    power_int / 100, 
                    power_int % 100);
        }
        lv_label_set_text(ui_power_values[i], text_buf);

        // 更新进度条
        if (ui_power_bars[i] != nullptr) {
            int percent = (int)((portInfos[i].power / MAX_PORT_WATTS) * 100);
            if (portInfos[i].power > 0 && percent == 0) percent = 1;
            lv_bar_set_value(ui_power_bars[i], percent, LV_ANIM_ON);
        }
    }

    // 更新总功率显示
    if (ui_total_label != nullptr) {
        lv_label_set_recolor(ui_total_label, true);
        float totalPower = PowerMonitor_GetTotalPower();
        
        if (dataError) {
            // 根据布局调整显示格式
            if (isVertical) {
                lv_label_set_text(ui_total_label, "#888888 --.-W#");
            } else {
                lv_label_set_text(ui_total_label, "#888888 --.-W#");
            }
            
            if (ui_total_bar != nullptr) {
                // 处理不同类型的进度条
                if (lv_obj_check_type(ui_total_bar, &lv_arc_class)) {
                    // 弧形进度条
                    lv_arc_set_value(ui_total_bar, 0);
                } else {
                    // 标准进度条
                    lv_bar_set_value(ui_total_bar, 0, LV_ANIM_ON);
                }
            }
        } else {
            // 根据总功率值决定显示的小数位数
            if (totalPower < 10.0f) {
                // 小于10W时显示两位小数
                int total_power_int = (int)(totalPower * 100);
                
                if (isVertical) {
                    snprintf(text_buf, sizeof(text_buf), 
                            "#FFFFFF %d.%02dW#",
                            total_power_int / 100,
                            total_power_int % 100);
                } else {
                    snprintf(text_buf, sizeof(text_buf), 
                            "#FFFFFF %d.%02dW#",
                            total_power_int / 100,
                            total_power_int % 100);
                }
            } else if (totalPower < 100.0f) {
                // 10-100W之间显示一位小数
                int total_power_int = (int)(totalPower * 10);
                
                if (isVertical) {
                    snprintf(text_buf, sizeof(text_buf), 
                            "#FFFFFF %d.%dW#",
                            total_power_int / 10,
                            total_power_int % 10);
                } else {
                    snprintf(text_buf, sizeof(text_buf), 
                            "#FFFFFF %d.%dW#",
                            total_power_int / 10,
                            total_power_int % 10);
                }
            } else {
                // 大于等于100W时显示整数（四舍五入）
                int total_power_int = (int)(totalPower + 0.5f);
                
                if (isVertical) {
                    snprintf(text_buf, sizeof(text_buf), 
                            "#FFFFFF %dW#",
                            total_power_int);
                } else {
                    snprintf(text_buf, sizeof(text_buf), 
                            "#FFFFFF %dW#",
                            total_power_int);
                }
            }
            lv_label_set_text(ui_total_label, text_buf);

            if (ui_total_bar != nullptr) {
                // 计算百分比
                int totalPercent = (int)((totalPower / MAX_POWER_WATTS) * 100);
                if (totalPower > 0 && totalPercent == 0) totalPercent = 1;
                if (totalPercent > 100) totalPercent = 100;
                
                // 处理不同类型的进度条
                if (lv_obj_check_type(ui_total_bar, &lv_arc_class)) {
                    // 弧形进度条
                    lv_arc_set_value(ui_total_bar, totalPercent);
                } else {
                    // 标准进度条
                    lv_bar_set_value(ui_total_bar, totalPercent, LV_ANIM_ON);
                }
            }
        }
    }

    giveLvglLock();
}

void DisplayManager::takeLvglLock() {
    if (lvgl_mutex == nullptr) {
        printf("[Display] Error: LVGL mutex not initialized, recreating...\n");
        // 如果互斥锁未初始化，尝试重新创建
        lvgl_mutex = xSemaphoreCreateMutex();
        if (lvgl_mutex == nullptr) {
            printf("[Display] Fatal: Failed to create LVGL mutex\n");
            return;
        }
        printf("[Display] LVGL mutex recreated successfully\n");
    }
    
    // 检查当前任务是否已经持有锁（可选的调试信息）
    #ifdef DEBUG_DISPLAY_LOCKS
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    printf("[Display] Task %p acquiring LVGL lock\n", currentTask);
    #endif
    
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    
    #ifdef DEBUG_DISPLAY_LOCKS
    printf("[Display] Task %p acquired LVGL lock\n", currentTask);
    #endif
}

void DisplayManager::giveLvglLock() {
    if (lvgl_mutex != nullptr) {
        #ifdef DEBUG_DISPLAY_LOCKS
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        printf("[Display] Task %p releasing LVGL lock\n", currentTask);
        #endif
        
        xSemaphoreGive(lvgl_mutex);
        
        #ifdef DEBUG_DISPLAY_LOCKS
        printf("[Display] Task %p released LVGL lock\n", currentTask);
        #endif
    } else {
        printf("[Display] Warning: Attempting to give uninitialized mutex\n");
    }
}

void DisplayManager::handleLvglTask() {
    takeLvglLock();
    lv_timer_handler();
    giveLvglLock();
}

bool DisplayManager::isValidScreenState() {
    // 检查是否有多个屏幕同时处于活动状态
    int activeScreenCount = 0;
    if (apScreenActive) activeScreenCount++;
    if (wifiErrorScreenActive) activeScreenCount++;
    if (timeScreenActive) activeScreenCount++;
    if (powerMonitorScreenActive) activeScreenCount++;
    if (scanScreenActive) activeScreenCount++;
    
    if (activeScreenCount > 1) {
        printf("[Display] Warning: Multiple screens active simultaneously (%d)\n", activeScreenCount);
        return false;
    }
    
    return true;
}

void DisplayManager::resetAllScreenStates() {
    printf("[Display] Emergency: Resetting all screen states\n");
    
    takeLvglLock();
    
    // 强制重置所有状态标志
    apScreenActive = false;
    wifiErrorScreenActive = false;
    timeScreenActive = false;
    powerMonitorScreenActive = false;
    scanScreenActive = false;
    
    // 隐藏所有容器
    if (apContainer) lv_obj_add_flag(apContainer, LV_OBJ_FLAG_HIDDEN);
    if (wifiErrorContainer) lv_obj_add_flag(wifiErrorContainer, LV_OBJ_FLAG_HIDDEN);
    if (timeContainer) lv_obj_add_flag(timeContainer, LV_OBJ_FLAG_HIDDEN);
    if (powerMonitorContainer) lv_obj_add_flag(powerMonitorContainer, LV_OBJ_FLAG_HIDDEN);
    if (scanContainer) lv_obj_add_flag(scanContainer, LV_OBJ_FLAG_HIDDEN);
    
    giveLvglLock();
    
    printf("[Display] All screen states reset\n");
}

// 新增：获取当前屏幕方向函数
int DisplayManager::getCurrentRotation() {
    return currentRotation;
}

// 修改: 应用屏幕方向设置
void DisplayManager::applyScreenRotation(int rotation) {
    printf("[Display] Applying screen rotation: %d degrees\n", rotation);
    
    // 保存当前旋转角度
    currentRotation = rotation;
    
    // 调用LVGL驱动的屏幕方向设置函数
    Lvgl_SetRotation(rotation);
    
    printf("[Display] Screen rotation applied\n");
} 