#include "Wireless.h"

uint16_t WIFI_NUM = 0;
bool WIFI_Connection = false;  // WiFi连接状态
int8_t WiFi_RSSI = 0;          // WiFi信号强度
bool WIFI_GotIP = false;

bool WiFi_Scan_Finish = 0;
static bool wifi_initialized = false;  // WiFi初始化标志

// WiFi事件处理函数
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI("WIFI", "WiFi模式已启动");
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI("WIFI", "WiFi已连接AP");
            WIFI_Connection = true;
            // 连接到AP后，IP状态仍为false，等待IP_EVENT
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI("WIFI", "WiFi连接断开，尝试重连...");
            WIFI_Connection = false;
            WIFI_GotIP = false; // 断开连接时重置IP状态
            esp_wifi_connect(); // 断开时自动重连
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI("WIFI", "获取IP地址: " IPSTR, IP2STR(&event->ip_info.ip));
            WIFI_GotIP = true; // 设置IP获取状态为true
        } else if (event_id == IP_EVENT_STA_LOST_IP) {
            ESP_LOGI("WIFI", "IP地址失效");
            WIFI_GotIP = false; // IP丢失时重置状态
        }
    }
}

// 初始化WiFi函数（同步版本）
static void Initialize_WiFi(void) {
    if (!wifi_initialized) {
        // 初始化NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        
        // 初始化WiFi
        ESP_LOGI("WIFI", "开始初始化WiFi");
        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        
        // 确保netif创建成功
        assert(sta_netif != NULL);
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        // 注册WiFi事件处理函数
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        wifi_initialized = true;
        ESP_LOGI("WIFI", "WiFi初始化完成");
    }
}

void Wireless_Init(void)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 创建WiFi初始化任务
    xTaskCreatePinnedToCore(
        WIFI_Init, 
        "WIFI task",
        8192, 
        NULL, 
        1, 
        NULL, 
        0);
}

void WIFI_Init(void *arg)
{
    Initialize_WiFi();  // 确保WiFi已初始化
    
    WIFI_NUM = WIFI_Scan();
    ESP_LOGI("WIFI", "找到WiFi: %d", WIFI_NUM);
    
    vTaskDelete(NULL);
}

uint16_t WIFI_Scan(void)
{
    uint16_t ap_count = 0;
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    esp_wifi_scan_stop();
    WiFi_Scan_Finish = 1;
    return ap_count;
}

void WiFi_Connect(const char* ssid, const char* password) {
    ESP_LOGI("WIFI", "正在连接WiFi: %s", ssid);
    
    // 确保WiFi已初始化
    Initialize_WiFi();
    
    // 初始化状态变量
    WIFI_Connection = false;
    WIFI_GotIP = false;
    
    // 配置WiFi
    wifi_config_t wifi_config = {0};
    
    // 复制SSID和密码
    memcpy((char *)wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy((char *)wifi_config.sta.password, password, strlen(password));
    
    // 配置认证模式
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    // 不再在这里等待和检查WiFi连接，从而减少阻塞
    // 连接状态将由事件处理函数更新
    ESP_LOGI("WIFI", "WiFi连接中...");
}