/**
 * @file     IP_Scanner.c
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-03-19
 * @brief    IP Scanner Module Implementation
 */

#include "IP_Scanner.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static const char *TAG = "IP_SCANNER";

// NVS相关定义
#define NVS_NAMESPACE "ip_scanner"
#define NVS_KEY_IP "saved_ip"

// 修改超时配置，区分连接和读取超时
#define TCP_CONNECT_TIMEOUT_MS 500   // TCP连接超时时间 (ms)
#define HTTP_READ_TIMEOUT_MS 1000    // HTTP读取超时时间 (ms)
#define HTTP_MAX_RESPONSE_SIZE 2048  // 最大HTTP响应大小
#define IP_CHECK_RETRY_COUNT 1       // IP检查重试次数

// 并行任务相关定义 - 增加栈大小
#define SCAN_TASK_STACK_SIZE 8192   // 增加到8KB
#define SCAN_TASK_PRIORITY 5
#define MAX_PARALLEL_TASKS 3         // 减少任务数量以降低内存占用

// 全局变量定义
nvs_handle_t ip_scanner_nvs;
bool ip_scanner_initialized = false;
SemaphoreHandle_t scan_semaphore = NULL;
ip_scan_callback_t global_callback = NULL;
char global_base_ip[16] = {0};
int found_device_count = 0;

// 添加任务完成跟踪变量
SemaphoreHandle_t task_completion_semaphore = NULL;
EventGroupHandle_t task_completion_group = NULL;

// 使用静态分配的缓冲区来减少栈使用
static char http_response_buffer[HTTP_MAX_RESPONSE_SIZE];

// 外部变量声明
extern bool WIFI_Connection;
extern bool WIFI_GotIP;

// 扫描任务结构体
typedef struct {
    int start_ip;
    int end_ip;
    TaskHandle_t task_handle;
    int task_id;  // 添加任务ID用于事件位
} scan_task_params_t;

// 添加HTTP响应调试函数
static void log_http_response(const char* ip, const char* response, int len) {
    ESP_LOGI(TAG, "[%s] HTTP响应 (%d 字节):", ip, len);
    
    // 找到第一行（HTTP状态行）
    char* eol = strchr(response, '\n');
    if (eol) {
        // 使用临时变量而非栈上分配的大型数组
        char status_line[64] = {0};
        int status_len = eol - response;
        status_len = status_len < 63 ? status_len : 63;
        memcpy(status_line, response, status_len);
        ESP_LOGI(TAG, "[%s] 状态行: %s", ip, status_line);
    }
    
    // 尝试多种方式查找响应体开始位置
    const char* body = NULL;
    
    // 方法1: 标准HTTP分隔符 \r\n\r\n
    body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4; // 跳过"\r\n\r\n"
    } else {
        // 方法2: 替代分隔符 \n\n
        body = strstr(response, "\n\n");
        if (body) {
            body += 2; // 跳过"\n\n"
        }
    }
    
    // 如果找到响应体
    if (body) {
        int body_len = len - (body - response);
        if (body_len > 0) {
            // 输出前100个字符的响应体，增加调试信息
            char preview[101] = {0};
            int preview_len = body_len < 100 ? body_len : 100;
            memcpy(preview, body, preview_len);
            ESP_LOGI(TAG, "[%s] 响应体 (%d 字节): %s%s", 
                    ip, body_len, preview, body_len > 100 ? "..." : "");
        } else {
            ESP_LOGI(TAG, "[%s] 响应体为空", ip);
        }
    } else {
        ESP_LOGW(TAG, "[%s] 无法找到标准响应体分隔符，尝试直接分析整个响应", ip);
        // 打印整个响应供调试
        char preview[101] = {0};
        int preview_len = len < 100 ? len : 100;
        memcpy(preview, response, preview_len);
        ESP_LOGD(TAG, "[%s] 完整响应: %s%s", ip, preview, len > 100 ? "..." : "");
    }
    
    // 不管能否找到响应体，都检查整个响应中是否包含关键字
    if (strstr(response, "ionbridge_port_current") != NULL) {
        ESP_LOGI(TAG, "[%s] 找到关键字: ionbridge_port_current", ip);
    } else {
        ESP_LOGI(TAG, "[%s] 未找到关键字: ionbridge_port_current", ip);
    }
}

// 初始化IP扫描器
esp_err_t IP_Scanner_Init(void) {
    ESP_LOGI(TAG, "正在初始化IP扫描器...");
    
    if (ip_scanner_initialized) {
        ESP_LOGI(TAG, "IP扫描器已经初始化过");
        return ESP_OK;
    }

    // 初始化NVS
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &ip_scanner_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS句柄失败: %s", esp_err_to_name(err));
        return err;
    }
    
    // 创建信号量
    scan_semaphore = xSemaphoreCreateMutex();
    if (scan_semaphore == NULL) {
        ESP_LOGE(TAG, "创建扫描信号量失败");
        nvs_close(ip_scanner_nvs);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "NVS初始化成功");
    ip_scanner_initialized = true;
    return ESP_OK;
}

// 检查指定IP是否可访问
bool IP_Scanner_CheckIP(const char* ip) {
    if (!ip_scanner_initialized) {
        ESP_LOGE(TAG, "IP扫描器未初始化");
        return false;
    }

    // 增加重试机制，最多重试3次
    for (int retry = 0; retry < IP_CHECK_RETRY_COUNT; retry++) {
        if (retry > 0) {
            ESP_LOGI(TAG, "[%s] 第%d次重试检查...", ip, retry);
            // 重试间隔增加
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // 首先检查80端口是否开放
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGD(TAG, "[%s] 创建socket失败", ip);
            continue;  // 重试
        }

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(80),
            .sin_addr.s_addr = inet_addr(ip)
        };

        // 设置连接超时时间
        struct timeval connect_timeout = {
            .tv_sec = 0,
            .tv_usec = TCP_CONNECT_TIMEOUT_MS * 1000
        };

        // 设置非阻塞模式
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        // 尝试连接
        int ret = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
        
        if (ret < 0 && errno != EINPROGRESS) {
            ESP_LOGD(TAG, "[%s] 连接失败，错误码: %d", ip, errno);
            close(sock);  // 确保关闭socket
            continue;  // 重试
        }

        // 使用select等待连接完成
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sock, &fdset);
        
        ret = select(sock + 1, NULL, &fdset, NULL, &connect_timeout);
        if (ret <= 0) {
            ESP_LOGD(TAG, "[%s] 80端口连接超时或未就绪", ip);
            close(sock);  // 确保关闭socket
            continue;  // 重试
        }

        // 检查连接是否成功
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            ESP_LOGD(TAG, "[%s] 80端口连接检查失败，错误码: %d", ip, error);
            close(sock);  // 确保关闭socket
            continue;  // 重试
        }

        ESP_LOGI(TAG, "[%s] 80端口可访问，开始检查/metrics接口", ip);

        // 设置为阻塞模式并设置读取超时
        fcntl(sock, F_SETFL, flags); // 恢复原始标志（移除非阻塞）
        struct timeval read_timeout = {
            .tv_sec = HTTP_READ_TIMEOUT_MS / 1000,
            .tv_usec = (HTTP_READ_TIMEOUT_MS % 1000) * 1000
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &read_timeout, sizeof(read_timeout));

        // 如果80端口开放，尝试访问metrics接口
        char request[128];
        snprintf(request, sizeof(request), "GET /metrics HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", ip);
        
        if (send(sock, request, strlen(request), 0) < 0) {
            ESP_LOGD(TAG, "[%s] 发送HTTP请求失败, errno: %d", ip, errno);
            close(sock);  // 确保关闭socket
            continue;  // 重试
        }

        // 使用全局缓冲区接收HTTP响应
        memset(http_response_buffer, 0, sizeof(http_response_buffer));
        
        // 循环接收数据，确保获取完整响应
        int total_received = 0;
        int bytes_received = 0;
        
        do {
            bytes_received = recv(sock, http_response_buffer + total_received, 
                                sizeof(http_response_buffer) - total_received - 1, 0);
            if (bytes_received > 0) {
                total_received += bytes_received;
                // 防止缓冲区溢出
                if (total_received >= sizeof(http_response_buffer) - 1) {
                    break;
                }
            }
        } while (bytes_received > 0);
        
        if (total_received <= 0) {
            ESP_LOGD(TAG, "[%s] 接收HTTP响应失败或为空, errno: %d", ip, errno);
            close(sock);  // 确保关闭socket
            continue;  // 重试
        }

        ESP_LOGI(TAG, "=== 来自[%s]的HTTP响应，长度: %d 字节 ===", ip, total_received);
        
        // 确保有足够的数据
        http_response_buffer[total_received] = '\0';
        
        // 打印HTTP响应详情以便调试
        log_http_response(ip, http_response_buffer, total_received);
        
        // 关闭连接
        close(sock);

        // 检查响应是否包含metrics数据（直接检查整个响应）
        bool contains_metrics = strstr(http_response_buffer, "ionbridge_port_current") != NULL;
        
        if (contains_metrics) {
            ESP_LOGI(TAG, "[%s] 响应包含 ionbridge_port_current 字段，确认为小电拼设备", ip);
            xSemaphoreTake(scan_semaphore, portMAX_DELAY);
            found_device_count++;
            
            // 保存找到的第一个有效IP
            if (found_device_count == 1) {
                IP_Scanner_SaveIP(ip);
            }
            xSemaphoreGive(scan_semaphore);
            
            ESP_LOGI(TAG, "找到设备: %s", ip);
            return true;
        } else {
            ESP_LOGD(TAG, "[%s] 响应不包含小电拼特征字段", ip);
        }
    }
    
    // ESP_LOGW(TAG, "[%s] 经过%d次重试后仍未检测到有效设备", ip, IP_CHECK_RETRY_COUNT);
    return false;
}

// 扫描任务函数
void scan_task_function(void *pvParameters) {
    scan_task_params_t *params = (scan_task_params_t *)pvParameters;
    char ip[32];
    
    for (int i = params->start_ip; i <= params->end_ip; i++) {
        snprintf(ip, sizeof(ip), "%s%d", global_base_ip, i);
        
        // 检查IP是否可访问
        bool is_accessible = IP_Scanner_CheckIP(ip);
        
        // 调用回调函数
        if (global_callback) {
            global_callback(ip, is_accessible);
        }
        
        // 短暂延时避免网络拥塞
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    // 通知任务已完成
    xEventGroupSetBits(task_completion_group, (1 << params->task_id));
    ESP_LOGI(TAG, "扫描任务 %d 完成 (IP范围: %d-%d)", params->task_id, params->start_ip, params->end_ip);
    
    vTaskDelete(NULL);
}

// 扫描网络中的设备
esp_err_t IP_Scanner_ScanNetwork(const char* base_ip, ip_scan_callback_t callback, bool skip_validation) {
    ESP_LOGI(TAG, "准备扫描网段...");
    
    if (!ip_scanner_initialized || !callback) {
        ESP_LOGE(TAG, "IP扫描器未初始化或回调函数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查WiFi状态，必须已连接且已获取IP
    if (!WIFI_Connection || !WIFI_GotIP) {
        ESP_LOGW(TAG, "WiFi未连接或未获取IP地址，暂不进行扫描");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // 首先尝试加载上次保存的IP
    char saved_ip[32] = {0};
    if (IP_Scanner_LoadIP(saved_ip, sizeof(saved_ip))) {
        ESP_LOGI(TAG, "找到已保存的IP地址: %s", saved_ip);
        
        // 如果外部已经验证过，则跳过内部验证
        if (skip_validation) {
            ESP_LOGI(TAG, "跳过IP验证（外部已验证）：%s", saved_ip);
            
            // 直接通知回调已找到有效IP
            if (callback) {
                ESP_LOGI(TAG, "通知回调已找到有效IP: %s", saved_ip);
                callback(saved_ip, true);
            }
            return ESP_OK;
        }
        
        // 添加明确的验证日志
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "开始对保存的IP地址进行稳定性验证: %s", saved_ip);
        ESP_LOGI(TAG, "========================================");
        
        // 在验证过程中锁定其他任务
        xSemaphoreTake(scan_semaphore, portMAX_DELAY);
        
        // 检查保存的IP是否仍然可用
        bool check_result = IP_Scanner_CheckIP(saved_ip);
        
        // 释放信号量
        xSemaphoreGive(scan_semaphore);
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "IP地址验证结果: %s", check_result ? "可用" : "不可用");
        ESP_LOGI(TAG, "========================================");
        
        if (check_result) {
            ESP_LOGI(TAG, "已保存的IP地址 %s 仍然可用，无需扫描网络", saved_ip);
            
            // 通知回调函数找到可用设备
            if (callback) {
                callback(saved_ip, true);
            }
            
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "已保存的IP地址 %s 不可用，需要重新扫描", saved_ip);
        }
    } else {
        ESP_LOGI(TAG, "未找到已保存的IP地址，需要扫描网络");
    }
    
    // 检查基础IP
    if (base_ip == NULL || strlen(base_ip) == 0) {
        ESP_LOGE(TAG, "必须提供有效的基础IP地址");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "开始扫描网段: %s*", base_ip);

    // 保存全局参数
    strncpy(global_base_ip, base_ip, sizeof(global_base_ip) - 1);
    global_callback = callback;
    found_device_count = 0;
    
    // 创建事件组，用于跟踪任务完成
    task_completion_group = xEventGroupCreate();
    if (task_completion_group == NULL) {
        ESP_LOGE(TAG, "创建任务完成事件组失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建多个扫描任务，减少并行任务数量
    int ips_per_task = 254 / MAX_PARALLEL_TASKS;
    scan_task_params_t params[MAX_PARALLEL_TASKS];
    
    // 定义等待的事件位掩码
    EventBits_t all_tasks_bits = 0;
    
    for (int i = 0; i < MAX_PARALLEL_TASKS; i++) {
        params[i].start_ip = i * ips_per_task + 1;
        params[i].end_ip = (i == MAX_PARALLEL_TASKS - 1) ? 254 : (i + 1) * ips_per_task;
        params[i].task_id = i;
        
        // 设置对应的事件位
        all_tasks_bits |= (1 << i);
        
        ESP_LOGI(TAG, "创建扫描任务 %d，扫描范围: %s%d - %s%d", 
                i, base_ip, params[i].start_ip, base_ip, params[i].end_ip);
        
        char task_name[32];
        snprintf(task_name, sizeof(task_name), "scan_task_%d", i);
        
        // 创建任务并检查是否成功
        BaseType_t ret = xTaskCreate(scan_task_function, task_name, SCAN_TASK_STACK_SIZE, 
                   &params[i], SCAN_TASK_PRIORITY, &params[i].task_handle);
        
        // 检查任务创建是否成功
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "创建扫描任务 %d 失败", i);
            // 停止已创建的任务
            for (int j = 0; j < i; j++) {
                if (params[j].task_handle != NULL) {
                    vTaskDelete(params[j].task_handle);
                }
            }
            // 删除事件组
            vEventGroupDelete(task_completion_group);
            return ESP_ERR_NO_MEM;
        }
        
        // 在任务创建之间添加短暂延迟，以避免同时启动多个任务
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // 等待所有任务完成
    ESP_LOGI(TAG, "等待所有扫描任务完成...");
    EventBits_t bits = xEventGroupWaitBits(
        task_completion_group,   // 事件组句柄
        all_tasks_bits,          // 等待的位
        pdTRUE,                  // 清除位
        pdTRUE,                  // 等待所有位
        portMAX_DELAY            // 无限等待
    );
    
    // 删除事件组
    vEventGroupDelete(task_completion_group);
    
    ESP_LOGI(TAG, "所有扫描任务已完成，共找到 %d 个有效IP", found_device_count);
    
    return ESP_OK;
}

// 保存IP地址到NVS
esp_err_t IP_Scanner_SaveIP(const char* ip) {
    ESP_LOGI(TAG, "正在保存IP地址到NVS: %s", ip);
    
    if (!ip_scanner_initialized) {
        ESP_LOGE(TAG, "IP扫描器未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_set_str(ip_scanner_nvs, NVS_KEY_IP, ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存IP到NVS失败: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_commit(ip_scanner_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "IP地址成功保存到NVS: %s", ip);
    return ESP_OK;
}

// 从NVS加载IP地址
bool IP_Scanner_LoadIP(char* ip_buffer, size_t buffer_size) {
    ESP_LOGI(TAG, "正在从NVS加载IP地址...");
    
    if (!ip_scanner_initialized || !ip_buffer || buffer_size == 0) {
        ESP_LOGE(TAG, "IP扫描器未初始化或缓冲区无效");
        return false;
    }

    size_t required_size = 0;
    esp_err_t err = nvs_get_str(ip_scanner_nvs, NVS_KEY_IP, NULL, &required_size);
    
    if (err != ESP_OK || required_size > buffer_size) {
        ESP_LOGE(TAG, "从NVS获取IP失败或缓冲区太小: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_get_str(ip_scanner_nvs, NVS_KEY_IP, ip_buffer, &buffer_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "从NVS读取IP失败: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "成功从NVS加载IP地址: %s", ip_buffer);
    return true;
} 