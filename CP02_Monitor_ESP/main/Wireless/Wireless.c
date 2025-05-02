#include "Wireless.h"

uint16_t WIFI_NUM = 0;
bool WIFI_Connection = false;  // WiFi连接状态
int8_t WiFi_RSSI = 0;          // WiFi信号强度

bool WiFi_Scan_Finish = 0;
static bool wifi_initialized = false;  // WiFi初始化标志

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
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
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
    
    // 配置WiFi
    wifi_config_t wifi_config = {0};
    
    // 复制SSID和密码
    memcpy((char *)wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy((char *)wifi_config.sta.password, password, strlen(password));
    
    // 配置认证模式
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    // 检查连接状态
    wifi_ap_record_t ap_info;
    for (int i = 0; i < 20; i++) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
        if (err == ESP_OK) {
            WIFI_Connection = true;
            WiFi_RSSI = ap_info.rssi;
            ESP_LOGI("WIFI", "WiFi已连接, RSSI: %d", WiFi_RSSI);
            break;
        }
    }
    
    if (!WIFI_Connection) {
        ESP_LOGE("WIFI", "WiFi连接失败");
    }
}