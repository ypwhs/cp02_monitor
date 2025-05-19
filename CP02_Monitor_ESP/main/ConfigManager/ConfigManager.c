#include "ConfigManager.h"
#include "Display_Manager.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_http_server.h"
#include <string.h>
#include "esp_mac.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "dhcpserver/dhcpserver.h"
#include "RGB/RGB.h"

static const char *TAG = "CONFIG_MANAGER";

// URL解码函数
static void urldecode(const char* src, char* dst, size_t dst_size);

// 定义全局配置管理器实例
config_manager_t config_manager = {
    .server = NULL,
    .ap_ssid = "ESP32_Config",
    .nvs_namespace = "wifi_config",
    .nvs_ssid_key = "ssid",
    .nvs_pass_key = "password",
    .nvs_rgb_key = "rgb_enabled",
    .nvs_monitor_url_key = "monitor_url",
    .configured = false,
    .ap_started = false
};

// 默认监控URL
static char* DEFAULT_MONITOR_URL = "http://192.168.32.2/metrics";
static const char* URL_PREFIX = "http://";
static const char* URL_SUFFIX = "/metrics";

// 存储URL和IP地址的缓冲区
static char monitor_url_buffer[256] = {0};
static char ssid_buffer[65] = {0};
static char password_buffer[129] = {0};
static char ip_buffer[64] = {0};

// AP IP地址
static ip4_addr_t ap_ip;

// HTTP服务器处理程序声明
static esp_err_t handle_root_get(httpd_req_t *req);
static esp_err_t handle_save_post(httpd_req_t *req);
static esp_err_t handle_status_get(httpd_req_t *req);
static esp_err_t handle_rgb_post(httpd_req_t *req);
static esp_err_t handle_reset_post(httpd_req_t *req);
static esp_err_t handle_not_found(httpd_req_t *req, httpd_err_code_t err);

// 生成带有唯一ID的AP SSID
static void generate_unique_ap_ssid(void) {
    uint8_t mac[6];
    // 获取ESP32的MAC地址作为唯一标识
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        // 使用MAC地址后四位作为后缀
        snprintf(config_manager.ap_ssid, sizeof(config_manager.ap_ssid), 
                "ESP32_Config_%02X%02X", mac[4], mac[5]);
        ESP_LOGI(TAG, "生成唯一AP SSID: %s", config_manager.ap_ssid);
    } else {
        ESP_LOGE(TAG, "无法获取MAC地址，使用默认SSID");
    }
}

// 初始化配置管理器
void config_manager_init(void) {
    ESP_LOGI(TAG, "初始化配置管理器...");
    
    // 初始化RGB灯
    RGB_Init();
    
    // 生成唯一的AP SSID
    generate_unique_ap_ssid();
    
    // 初始化NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    // 打开NVS
    nvs_handle_t nvs_handle;
    err = nvs_open(config_manager.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }
    
    // 检查是否已配置
    size_t ssid_len = sizeof(ssid_buffer);
    err = nvs_get_str(nvs_handle, config_manager.nvs_ssid_key, ssid_buffer, &ssid_len);
    
    // 检查监控URL
    size_t url_len = sizeof(monitor_url_buffer);
    err = nvs_get_str(nvs_handle, config_manager.nvs_monitor_url_key, monitor_url_buffer, &url_len);
    if (err != ESP_OK || url_len == 0) {
        ESP_LOGI(TAG, "设置默认监控URL");
        strncpy(monitor_url_buffer, DEFAULT_MONITOR_URL, sizeof(monitor_url_buffer) - 1);
        monitor_url_buffer[sizeof(monitor_url_buffer) - 1] = '\0';
        nvs_set_str(nvs_handle, config_manager.nvs_monitor_url_key, monitor_url_buffer);
        nvs_commit(nvs_handle);
    }
    
    // 关闭NVS
    nvs_close(nvs_handle);
    
    // 如果SSID存在
    if (err == ESP_OK && ssid_len > 0) {
        config_manager.configured = true;
        ESP_LOGI(TAG, "发现保存的WiFi配置: %s", ssid_buffer);
        
        // 获取密码
        nvs_handle_t pass_handle;
        nvs_open(config_manager.nvs_namespace, NVS_READWRITE, &pass_handle);
        size_t pass_len = sizeof(password_buffer);
        nvs_get_str(pass_handle, config_manager.nvs_pass_key, password_buffer, &pass_len);
        nvs_close(pass_handle);
        
        // 设置WiFi模式为AP+STA
        ESP_ERROR_CHECK(esp_wifi_stop());
        vTaskDelay(100 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        
        // 先配置AP
        wifi_config_t ap_config = {0};
        strncpy((char *)ap_config.ap.ssid, config_manager.ap_ssid, sizeof(ap_config.ap.ssid) - 1);
        ap_config.ap.ssid_len = strlen(config_manager.ap_ssid);
        ap_config.ap.max_connection = 4;
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        
        // 然后配置STA
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid_buffer, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, password_buffer, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        
        // 重新启动WiFi并连接
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        
        ESP_LOGI(TAG, "已连接到WiFi: %s", ssid_buffer);
    } else {
        ESP_LOGI(TAG, "未找到保存的WiFi配置");
        
        // 停止当前WiFi
        ESP_ERROR_CHECK(esp_wifi_stop());
        vTaskDelay(100 / portTICK_PERIOD_MS);
        
        // 设置WiFi为AP模式
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        
        // 显示AP配置屏幕
        wifi_config_t ap_config = {0};
        
        // 清除AP配置内存并确保使用更新的SSID
        memset(&ap_config, 0, sizeof(wifi_config_t));
        ESP_LOGI(TAG, "设置AP SSID: %s", config_manager.ap_ssid);
        
        strncpy((char *)ap_config.ap.ssid, config_manager.ap_ssid, sizeof(ap_config.ap.ssid) - 1);
        ap_config.ap.ssid_len = strlen(config_manager.ap_ssid);
        ap_config.ap.max_connection = 4;
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        
        // 重新启动WiFi
        ESP_ERROR_CHECK(esp_wifi_start());
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        // 获取AP IP地址
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_get_ip_info(netif, &ip_info);
        
        char ip_addr[16];
        sprintf(ip_addr, IPSTR, IP2STR(&ip_info.ip));
        
        // 保存AP IP地址
        ap_ip.addr = ip_info.ip.addr;
        
        // 配置DHCP服务器，强制DNS服务器为AP的IP
        // 这样客户端连接时会使用AP IP作为DNS服务器
        esp_netif_dhcps_stop(netif);
        
        // 设置DNS选项
        dhcps_offer_t offer = OFFER_DNS;
        esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
        
        // 设置我们自己的IP作为DNS服务器
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = ip_info.ip.addr;
        dns.ip.type = IPADDR_TYPE_V4;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
        
        // 启动DHCP服务器
        esp_netif_dhcps_start(netif);
        
        display_manager_create_ap_screen(config_manager.ap_ssid, ip_addr);
    }
    
    // 启动配置门户
    config_manager_start_portal();
    
    ESP_LOGI(TAG, "配置管理器初始化完成");
}

// 处理配置
void config_manager_handle(void) {
    // 定期更新显示
    static unsigned long last_display_update = 0;
    static bool last_wifi_status = false;
    
    unsigned long current_time = esp_log_timestamp();
    if (current_time - last_display_update >= 1000) {  // 每秒更新一次显示
        // 修改这里：检查WiFi状态，不要重新连接
        bool current_wifi_status = WIFI_Connection;
        
        // 检查WiFi状态变化
        if (current_wifi_status != last_wifi_status) {
            if (!current_wifi_status && config_manager.configured) {
                // 已配置但WiFi断开连接时，显示错误屏幕
                ESP_LOGI(TAG, "WiFi连接丢失，显示错误屏幕");
                display_manager_create_wifi_error_screen();
            } else if (current_wifi_status) {
                // WiFi连接成功时，删除错误屏幕并显示监控屏幕
                ESP_LOGI(TAG, "WiFi连接建立");
                if (display_manager_is_wifi_error_screen_active()) {
                    display_manager_delete_wifi_error_screen();
                    display_manager_show_monitor_screen();
                }
            }
            last_wifi_status = current_wifi_status;
        }
        
        last_display_update = current_time;
    }
}

// 启动配置门户
void config_manager_start_portal(void) {
    if (!config_manager.ap_started) {
        // 配置HTTP服务器
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.max_uri_handlers = 10;
        config.max_resp_headers = 64;  // 增加响应头数量
        config.max_open_sockets = 7;
        config.recv_wait_timeout = 10;
        config.send_wait_timeout = 10;
        config.uri_match_fn = httpd_uri_match_wildcard;
        // 增加请求处理buffer大小来处理更大的请求
        config.stack_size = 32768;     // 增加栈大小
        config.server_port = 80;
        
        // 启动HTTP服务器
        ESP_LOGI(TAG, "启动Web服务器");
        esp_err_t err = httpd_start(&config_manager.server, &config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "启动服务器失败: %s", esp_err_to_name(err));
            return;
        }
        
        // 注册URI处理程序
        httpd_uri_t root_uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = handle_root_get,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(config_manager.server, &root_uri);
        
        httpd_uri_t save_uri = {
            .uri      = "/save",
            .method   = HTTP_POST,
            .handler  = handle_save_post,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(config_manager.server, &save_uri);
        
        httpd_uri_t status_uri = {
            .uri      = "/status",
            .method   = HTTP_GET,
            .handler  = handle_status_get,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(config_manager.server, &status_uri);
        
        httpd_uri_t rgb_uri = {
            .uri      = "/rgb",
            .method   = HTTP_POST,
            .handler  = handle_rgb_post,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(config_manager.server, &rgb_uri);
        
        httpd_uri_t reset_uri = {
            .uri      = "/reset",
            .method   = HTTP_POST,
            .handler  = handle_reset_post,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(config_manager.server, &reset_uri);
        
        // 注册404处理程序
        httpd_register_err_handler(config_manager.server, HTTPD_404_NOT_FOUND, handle_not_found);
        
        config_manager.ap_started = true;
        ESP_LOGI(TAG, "配置门户已启动");
    }
}

// 检查是否已配置
bool config_manager_is_configured(void) {
    return config_manager.configured;
}

// 检查WiFi是否已连接
bool config_manager_is_connected(void) {
    wifi_ap_record_t info;
    return (esp_wifi_sta_get_ap_info(&info) == ESP_OK);
}

// 检查RGB灯是否已启用
bool config_manager_is_rgb_enabled(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }
    
    uint8_t enabled = 0;
    err = nvs_get_u8(nvs_handle, config_manager.nvs_rgb_key, &enabled);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && enabled == 1);
}

// 设置RGB灯启用状态
void config_manager_set_rgb_enabled(bool enabled) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开NVS: %s", esp_err_to_name(err));
        return;
    }
    
    nvs_set_u8(nvs_handle, config_manager.nvs_rgb_key, enabled ? 1 : 0);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    // 立即应用RGB灯状态
    if (enabled) {
        ESP_LOGI(TAG, "RGB灯已启用");
        RGB_Loop(1);
    } else {
        ESP_LOGI(TAG, "RGB灯已禁用");
        RGB_Off();
    }
}

// 重置配置
void config_manager_reset(void) {
    ESP_LOGI(TAG, "重置所有配置...");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开NVS: %s", esp_err_to_name(err));
        return;
    }
    
    // 清除所有配置
    nvs_erase_all(nvs_handle);
    nvs_commit(nvs_handle);
    
    // 重新设置默认的监控URL
    nvs_set_str(nvs_handle, config_manager.nvs_monitor_url_key, DEFAULT_MONITOR_URL);
    nvs_commit(nvs_handle);
    
    nvs_close(nvs_handle);
    
    // 断开WiFi连接
    esp_wifi_disconnect();
    esp_wifi_stop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_wifi_start();
    
    config_manager.configured = false;
    ESP_LOGI(TAG, "所有配置已重置");
    
    // 更新显示
    config_manager_update_display();
}

// 获取SSID
char* config_manager_get_ssid(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return NULL;
    }
    
    size_t len = sizeof(ssid_buffer);
    err = nvs_get_str(nvs_handle, config_manager.nvs_ssid_key, ssid_buffer, &len);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        return NULL;
    }
    
    return ssid_buffer;
}

// 获取密码
char* config_manager_get_password(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return NULL;
    }
    
    size_t len = sizeof(password_buffer);
    err = nvs_get_str(nvs_handle, config_manager.nvs_pass_key, password_buffer, &len);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        return NULL;
    }
    
    return password_buffer;
}

// 保存配置
void config_manager_save(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开NVS: %s", esp_err_to_name(err));
        return;
    }
    
    nvs_set_str(nvs_handle, config_manager.nvs_ssid_key, ssid);
    nvs_set_str(nvs_handle, config_manager.nvs_pass_key, password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    config_manager.configured = true;
    ESP_LOGI(TAG, "新WiFi配置已保存");
    ESP_LOGI(TAG, "SSID: %s", ssid);
    
    // 保存后更新显示
    config_manager_update_display();
}

// 更新显示
void config_manager_update_display(void) {
    if (!config_manager.configured) {
        // 只有在未配置时才显示AP配置屏幕
        if (!display_manager_is_ap_screen_active()) {
            // 获取AP IP地址
            esp_netif_ip_info_t ip_info;
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            esp_netif_get_ip_info(netif, &ip_info);
            
            char ip_addr[16];
            sprintf(ip_addr, IPSTR, IP2STR(&ip_info.ip));
            
            display_manager_create_ap_screen(config_manager.ap_ssid, ip_addr);
        }
    } else {
        // 已配置WiFi时，显示监控屏幕
        if (display_manager_is_ap_screen_active()) {
            display_manager_delete_ap_screen();
            display_manager_show_monitor_screen();
        }
    }
}

// 获取监控服务器地址
char* config_manager_get_monitor_url(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return DEFAULT_MONITOR_URL;
    }
    
    size_t len = sizeof(monitor_url_buffer);
    err = nvs_get_str(nvs_handle, config_manager.nvs_monitor_url_key, monitor_url_buffer, &len);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        return DEFAULT_MONITOR_URL;
    }
    
    return monitor_url_buffer;
}

// 保存监控服务器地址
void config_manager_save_monitor_url(const char* ip) {
    if (ip == NULL || strlen(ip) == 0) {
        return;
    }
    
    // 构建完整URL
    char full_url[128];
    snprintf(full_url, sizeof(full_url), "%s%s%s", URL_PREFIX, ip, URL_SUFFIX);
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(config_manager.nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开NVS: %s", esp_err_to_name(err));
        return;
    }
    
    nvs_set_str(nvs_handle, config_manager.nvs_monitor_url_key, full_url);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "新监控URL已保存: %s", full_url);
}

// 从URL中提取IP地址
char* config_manager_extract_ip_from_url(const char* url) {
    if (url == NULL) {
        return NULL;
    }
    
    // 查找 ://
    const char* start = strstr(url, "://");
    if (start != NULL) {
        start += 3;  // 跳过 ://
        
        // 查找第一个 /
        const char* end = strchr(start, '/');
        if (end != NULL) {
            int len = end - start;
            if (len < sizeof(ip_buffer)) {
                strncpy(ip_buffer, start, len);
                ip_buffer[len] = '\0';
                return ip_buffer;
            }
        } else {
            // 如果没有 /，复制整个剩余部分
            strncpy(ip_buffer, start, sizeof(ip_buffer) - 1);
            ip_buffer[sizeof(ip_buffer) - 1] = '\0';
            return ip_buffer;
        }
    }
    
    // 如果无法提取IP，返回整个URL
    strncpy(ip_buffer, url, sizeof(ip_buffer) - 1);
    ip_buffer[sizeof(ip_buffer) - 1] = '\0';
    return ip_buffer;
}

// HTTP处理函数
static esp_err_t handle_root_get(httpd_req_t *req) {
    // 获取当前URL并提取IP地址
    char* current_url = config_manager_get_monitor_url();
    char* current_ip = config_manager_extract_ip_from_url(current_url);
    
    ESP_LOGI(TAG, "当前URL: %s, 提取的IP: %s", current_url, current_ip);
    
    // 使用分块传输的方式发送HTML
    httpd_resp_set_type(req, "text/html");
    
    // 以下使用分块发送大型HTML
    // 第一部分 - 头部和样式
    const char* html_part1 = 
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <meta charset='utf-8'>\n"
    "    <title>ESP32 配置</title>\n"
    "    <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "    <style>\n"
    "        body { font-family: Arial; margin: 20px; background: #f0f0f0; }\n"
    "        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }\n"
    "        .status { margin-bottom: 20px; padding: 10px; border-radius: 5px; }\n"
    "        .connected { background: #e8f5e9; color: #2e7d32; }\n"
    "        .disconnected { background: #ffebee; color: #c62828; }\n"
    "        input { width: 100%; padding: 8px; margin: 10px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n"
    "        button { width: 100%; padding: 10px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; margin-bottom: 10px; }\n"
    "        button:hover { background: #45a049; }\n"
    "        .danger-button { background: #f44336; }\n"
    "        .danger-button:hover { background: #d32f2f; }\n"
    "        .status-box { margin-top: 20px; }\n"
    "        .switch { position: relative; display: inline-block; width: 60px; height: 34px; }\n"
    "        .switch input { opacity: 0; width: 0; height: 0; }\n"
    "        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }\n"
    "        .slider:before { position: absolute; content: \"\"; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }\n"
    "        input:checked + .slider { background-color: #4CAF50; }\n"
    "        input:checked + .slider:before { transform: translateX(26px); }\n"
    "        .control-group { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }\n"
    "        .modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }\n"
    "        .modal-content { background-color: #fefefe; margin: 15% auto; padding: 20px; border-radius: 5px; max-width: 300px; text-align: center; }\n"
    "        .modal-buttons { display: flex; justify-content: space-between; margin-top: 20px; }\n"
    "        .modal-buttons button { width: 45%; margin: 0; }\n"
    "        .cancel-button { background: #9e9e9e; }\n"
    "        .cancel-button:hover { background: #757575; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class='container'>\n"
    "        <h2>ESP32 配置</h2>\n"
    "        <div id='status' class='status'></div>\n"
    "        \n"
    "        <div class='control-group'>\n"
    "            <h3>WiFi设置</h3>\n"
    "            <form method='post' action='/save'>\n"
    "                WiFi名称:<br>\n"
    "                <input type='text' name='ssid'><br>\n"
    "                WiFi密码:<br>\n"
    "                <input type='password' name='password'><br>\n"
    "                小电拼服务器IP地址:<br>\n"
    "                <input type='text' name='monitor_url' value='";
    
    // 发送第一部分
    httpd_resp_send_chunk(req, html_part1, strlen(html_part1));
    
    // 发送IP地址
    httpd_resp_send_chunk(req, current_ip, strlen(current_ip));
    
    // 第二部分 - 表单结束和其他控件
    const char* html_part2 = 
    "' placeholder='例如: 192.168.32.2'><br>\n"
    "                <button type='submit'>保存配置</button>\n"
    "            </form>\n"
    "        </div>\n"
    "        \n"
    "        <div class='control-group'>\n"
    "            <h3>RGB灯控制</h3>\n"
    "            <label class='switch'>\n"
    "                <input type='checkbox' id='rgb-switch' onchange='toggleRGB()'>\n"
    "                <span class='slider'></span>\n"
    "            </label>\n"
    "            <span style='margin-left: 10px;'>RGB灯状态</span>\n"
    "        </div>\n"
    "\n"
    "        <div class='control-group'>\n"
    "            <h3>系统设置</h3>\n"
    "            <button class='danger-button' onclick='showResetConfirm()'>重置所有配置</button>\n"
    "        </div>\n"
    "    </div>\n";
    
    // 发送第二部分
    httpd_resp_send_chunk(req, html_part2, strlen(html_part2));
    
    // 第三部分 - 模态窗口和脚本
    const char* html_part3 = 
    "\n"
    "    <div id='resetModal' class='modal'>\n"
    "        <div class='modal-content'>\n"
    "            <h3>确认重置</h3>\n"
    "            <p>这将清除所有配置并重启设备。确定要继续吗？</p>\n"
    "            <div class='modal-buttons'>\n"
    "                <button class='cancel-button' onclick='hideResetConfirm()'>取消</button>\n"
    "                <button class='danger-button' onclick='doReset()'>确认重置</button>\n"
    "            </div>\n"
    "        </div>\n"
    "    </div>\n"
    "    <script>\n"
    "        let lastUpdate = 0;\n"
    "        let updateInterval = 2000;\n"
    "        let statusUpdateTimeout = null;\n"
    "\n"
    "        function updateStatus() {\n"
    "            const now = Date.now();\n"
    "            if (now - lastUpdate < updateInterval) {\n"
    "                return;\n"
    "            }\n"
    "            lastUpdate = now;\n"
    "\n"
    "            fetch('/status')\n"
    "                .then(response => response.json())\n"
    "                .then(data => {\n"
    "                    const statusBox = document.getElementById('status');\n"
    "                    if (data.connected) {\n"
    "                        statusBox.innerHTML = `已连接到WiFi: ${data.ssid}<br>IP地址: ${data.ip}`;\n"
    "                        statusBox.className = 'status connected';\n"
    "                    } else {\n"
    "                        statusBox.innerHTML = '未连接到WiFi';\n"
    "                        statusBox.className = 'status disconnected';\n"
    "                    }\n"
    "                    const rgbSwitch = document.getElementById('rgb-switch');\n"
    "                    if (rgbSwitch.checked !== data.rgb_enabled) {\n"
    "                        rgbSwitch.checked = data.rgb_enabled;\n"
    "                    }\n"
    "                })\n"
    "                .catch(() => {\n"
    "                    if (statusUpdateTimeout) {\n"
    "                        clearTimeout(statusUpdateTimeout);\n"
    "                    }\n"
    "                    statusUpdateTimeout = setTimeout(updateStatus, updateInterval);\n"
    "                });\n"
    "        }\n"
    "        \n"
    "        function toggleRGB() {\n"
    "            const enabled = document.getElementById('rgb-switch').checked;\n"
    "            fetch('/rgb', {\n"
    "                method: 'POST',\n"
    "                headers: {'Content-Type': 'application/x-www-form-urlencoded'},\n"
    "                body: 'enabled=' + enabled\n"
    "            }).then(() => {\n"
    "                lastUpdate = 0;\n"
    "                updateStatus();\n"
    "            });\n"
    "        }\n"
    "\n"
    "        function showResetConfirm() {\n"
    "            document.getElementById('resetModal').style.display = 'block';\n"
    "        }\n"
    "\n"
    "        function hideResetConfirm() {\n"
    "            document.getElementById('resetModal').style.display = 'none';\n"
    "        }\n"
    "\n"
    "        function doReset() {\n"
    "            hideResetConfirm();\n"
    "            fetch('/reset', {\n"
    "                method: 'POST'\n"
    "            }).then(() => {\n"
    "                alert('配置已重置，设备将重启...');\n"
    "                setTimeout(() => {\n"
    "                    window.location.reload();\n"
    "                }, 5000);\n"
    "            });\n"
    "        }\n"
    "        \n"
    "        // 点击模态框外部时关闭\n"
    "        window.onclick = function(event) {\n"
    "            const modal = document.getElementById('resetModal');\n"
    "            if (event.target == modal) {\n"
    "                hideResetConfirm();\n"
    "            }\n"
    "        }\n"
    "        \n"
    "        window.onload = updateStatus;\n"
    "        setInterval(updateStatus, updateInterval);\n"
    "    </script>\n"
    "</body>\n"
    "</html>";
    
    // 发送第三部分
    httpd_resp_send_chunk(req, html_part3, strlen(html_part3));
    
    // 发送结束标记
    httpd_resp_send_chunk(req, NULL, 0);
    
    return ESP_OK;
}

static esp_err_t handle_status_get(httpd_req_t *req) {
    char json[256];
    
    bool is_connected = config_manager_is_connected();
    
    wifi_ap_record_t ap_info;
    if (is_connected == true && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        // 获取IP地址
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_get_ip_info(netif, &ip_info);
        
        char ip_addr[16];
        sprintf(ip_addr, IPSTR, IP2STR(&ip_info.ip));
        
        snprintf(json, sizeof(json), 
                 "{\"connected\":true,\"ssid\":\"%s\",\"ip\":\"%s\",\"rgb_enabled\":%s}",
                 (char*)ap_info.ssid,
                 ip_addr,
                 config_manager_is_rgb_enabled() ? "true" : "false");
    } else {
        snprintf(json, sizeof(json), 
                 "{\"connected\":false,\"ssid\":\"\",\"ip\":\"\",\"rgb_enabled\":%s}",
                 config_manager_is_rgb_enabled() ? "true" : "false");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    return ESP_OK;
}

static esp_err_t handle_rgb_post(httpd_req_t *req) {
    // 缓冲区存储请求数据
    char content[100];
    
    // 获取请求内容长度
    int total_len = req->content_len;
    int cur_len = 0;
    
    if (total_len >= sizeof(content)) {
        // 请求内容太长
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    // 读取请求内容
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive request data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    content[cur_len] = '\0';
    
    ESP_LOGI(TAG, "RGB控制请求: %s", content);
    
    // 解析请求，查找"enabled=true"或"enabled=false"
    bool enabled = false;
    if (strstr(content, "enabled=true") != NULL) {
        enabled = true;
    }
    
    // 设置RGB灯状态
    config_manager_set_rgb_enabled(enabled);
    
    // 发送响应
    httpd_resp_send(req, "OK", 2);
    
    return ESP_OK;
}

static esp_err_t handle_save_post(httpd_req_t *req) {
    // 获取请求内容长度
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 4096) { // 增加到4096允许更长的内容
        ESP_LOGW(TAG, "请求内容长度无效: %d", total_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }
    
    // 分配内存存储请求内容
    char* content = malloc(total_len + 1);
    if (content == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server error");
        return ESP_FAIL;
    }
    
    // 读取请求内容
    int cur_len = 0;
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(content);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    content[cur_len] = '\0';
    
    // 解析参数
    char ssid[65] = {0};  // 增加SSID缓冲区大小
    char password[129] = {0};  // 增加密码缓冲区大小
    char monitor_url[256] = {0};  // 增加URL缓冲区大小
    bool need_restart = false;
    bool config_changed = false;
    
    // 解析SSID
    char* ssid_param = strstr(content, "ssid=");
    if (ssid_param) {
        ssid_param += 5; // 跳过 "ssid="
        char* end = strchr(ssid_param, '&');
        if (end) {
            *end = '\0';
            urldecode(ssid_param, ssid, sizeof(ssid));
            *end = '&';
        } else {
            // 可能是最后一个参数
            urldecode(ssid_param, ssid, sizeof(ssid));
        }
    }
    
    // 解析密码
    char* pass_param = strstr(content, "password=");
    if (pass_param) {
        pass_param += 9; // 跳过 "password="
        char* end = strchr(pass_param, '&');
        if (end) {
            *end = '\0';
            urldecode(pass_param, password, sizeof(password));
            *end = '&';
        } else {
            // 可能是最后一个参数
            urldecode(pass_param, password, sizeof(password));
        }
    }
    
    // 解析监控URL
    char* url_param = strstr(content, "monitor_url=");
    if (url_param) {
        url_param += 12; // 跳过 "monitor_url="
        char* end = strchr(url_param, '&');
        if (end) {
            *end = '\0';
            urldecode(url_param, monitor_url, sizeof(monitor_url));
            *end = '&';
        } else {
            // 可能是最后一个参数
            urldecode(url_param, monitor_url, sizeof(monitor_url));
        }
    }
    
    // 释放内存
    free(content);
    
    ESP_LOGI(TAG, "解析参数: SSID=%s, URL=%s", ssid, monitor_url);
    
    // 保存配置
    if (strlen(ssid) > 0) {
        config_manager_save(ssid, password);
        
        // 设置WiFi
        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
        
        need_restart = true;
        config_changed = true;
    }
    
    // 保存监控URL
    if (strlen(monitor_url) > 0) {
        char* current_url = config_manager_get_monitor_url();
        char* current_ip = config_manager_extract_ip_from_url(current_url);
        
        if (strcmp(current_ip, monitor_url) != 0) {
            config_manager_save_monitor_url(monitor_url);
            need_restart = true;
            config_changed = true;
        }
    }
    
    // 返回成功页面
    if (config_changed) {
        const char* html = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <meta charset='utf-8'>\n"
        "    <title>配置已保存</title>\n"
        "    <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
        "    <style>\n"
        "        body { font-family: Arial; margin: 20px; text-align: center; }\n"
        "        .message { margin: 20px; padding: 20px; background: #e8f5e9; border-radius: 5px; }\n"
        "        .countdown { font-size: 24px; margin: 20px; }\n"
        "    </style>\n"
        "    <script>\n"
        "        let count = 5;\n"
        "        function updateCountdown() {\n"
        "            document.getElementById('countdown').textContent = count;\n"
        "            if (count > 0) {\n"
        "                count--;\n"
        "                setTimeout(updateCountdown, 1000);\n"
        "            }\n"
        "        }\n"
        "        window.onload = function() {\n"
        "            updateCountdown();\n"
        "            setTimeout(function() {\n"
        "                window.location.href = '/';\n"
        "            }, 5000);\n"
        "        }\n"
        "    </script>\n"
        "</head>\n"
        "<body>\n"
        "    <div class='message'>\n"
        "        <h2>配置已保存</h2>\n"
        "        <p>设备将在 <span id='countdown'>5</span> 秒后重启...</p>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";
        
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html, strlen(html));
        
        // 如果需要重启，等待5秒后重启
        if (need_restart) {
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            esp_restart();
        }
    } else {
        // 如果配置没有变化，重定向到主页
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
    }
    
    return ESP_OK;
}

static esp_err_t handle_reset_post(httpd_req_t *req) {
    ESP_LOGI(TAG, "处理重置请求...");
    
    const char* html = 
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <meta charset='utf-8'>\n"
    "    <title>重置配置</title>\n"
    "    <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "    <style>\n"
    "        body { font-family: Arial; margin: 20px; text-align: center; }\n"
    "        .message { margin: 20px; padding: 20px; background: #ffebee; border-radius: 5px; }\n"
    "        .countdown { font-size: 24px; margin: 20px; }\n"
    "    </style>\n"
    "    <script>\n"
    "        let count = 5;\n"
    "        function updateCountdown() {\n"
    "            document.getElementById('countdown').textContent = count;\n"
    "            if (count > 0) {\n"
    "                count--;\n"
    "                setTimeout(updateCountdown, 1000);\n"
    "            }\n"
    "        }\n"
    "        window.onload = function() {\n"
    "            updateCountdown();\n"
    "            setTimeout(function() {\n"
    "                window.location.href = '/';\n"
    "            }, 5000);\n"
    "        }\n"
    "    </script>\n"
    "</head>\n"
    "<body>\n"
    "    <div class='message'>\n"
    "        <h2>配置已重置</h2>\n"
    "        <p>设备将在 <span id='countdown'>5</span> 秒后重启...</p>\n"
    "    </div>\n"
    "</body>\n"
    "</html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    
    // 延迟一段时间后重置
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // 重置配置
    config_manager_reset();
    
    // 重启设备
    vTaskDelay(4000 / portTICK_PERIOD_MS);
    esp_restart();
    
    return ESP_OK;
}

static esp_err_t handle_not_found(httpd_req_t *req, httpd_err_code_t err) {
    ESP_LOGI(TAG, "捕获门户：截获请求 %s", req->uri);
    
    char host_buf[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host_buf, sizeof(host_buf)) == ESP_OK) {
        ESP_LOGI(TAG, "请求主机: %s", host_buf);
    }
    
    // 检查是否是重定向请求
    if (strstr(req->uri, "/generate_204") != NULL || 
        strstr(req->uri, "/success") != NULL ||
        strstr(req->uri, "/connecttest") != NULL ||
        strstr(req->uri, "/redirect") != NULL ||
        strstr(req->uri, "/hotspot-detect") != NULL ||
        strstr(req->uri, "/ncsi.txt") != NULL) {
        
        // 获取AP IP地址
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_get_ip_info(netif, &ip_info);
        
        char redirect_url[64];
        snprintf(redirect_url, sizeof(redirect_url), "http://"IPSTR"/", IP2STR(&ip_info.ip));
        
        // 直接重定向到主页
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", redirect_url);
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    // 构建捕获门户页面
    const char* html = 
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <meta charset='utf-8'>\n"
    "    <title>需要登录</title>\n"
    "    <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "    <style>\n"
    "        body { font-family: Arial; margin: 20px; text-align: center; }\n"
    "        .message { margin: 20px; padding: 20px; background: #e3f2fd; border-radius: 5px; }\n"
    "        .btn { background: #2196F3; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class='message'>\n"
    "        <h2>设备WiFi配置</h2>\n"
    "        <p>当前设备需要配置WiFi连接信息</p>\n"
    "        <p>请点击下方按钮进入配置页面</p>\n"
    "        <a href='/'><button class='btn'>进入配置</button></a>\n"
    "    </div>\n"
    "</body>\n"
    "</html>";
    
    // 发送捕获门户页面
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// URL解码函数
static void urldecode(const char* src, char* dst, size_t dst_size) {
    size_t src_len = strlen(src);
    size_t i, j = 0;
    
    for (i = 0; i < src_len && j < dst_size - 1; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            int value;
            if (sscanf(src + i + 1, "%2x", &value) == 1) {
                dst[j++] = value;
                i += 2;
            } else {
                dst[j++] = src[i];
            }
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    
    dst[j] = '\0';
} 