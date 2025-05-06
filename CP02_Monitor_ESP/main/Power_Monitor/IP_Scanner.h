/**
 * @file     IP_Scanner.h
 * @author   Claude AI
 * @version  V1.0
 * @date     2024-03-19
 * @brief    IP Scanner Module Header
 */

#ifndef IP_SCANNER_H
#define IP_SCANNER_H

#include <stdbool.h>
#include "esp_err.h"

// IP扫描器配置
#define SCAN_BATCH_SIZE 10          // 每批扫描的IP数量
#define MAX_RETRY_COUNT 1           // 最大重试次数

// 扫描结果回调函数类型
typedef void (*ip_scan_callback_t)(const char* ip, bool success);

/**
 * @brief 初始化IP扫描器
 * @return esp_err_t ESP_OK表示成功，其他表示错误
 */
esp_err_t IP_Scanner_Init(void);

/**
 * @brief 检查指定IP是否可访问
 * @param ip 要检查的IP地址
 * @return bool true表示可访问，false表示不可访问
 */
bool IP_Scanner_CheckIP(const char* ip);

/**
 * @brief 扫描指定网段内的所有IP
 * @param base_ip 基础IP地址前缀（例如："192.168.50."），必须提供有效的网段前缀
 * @param callback 扫描结果回调函数
 * @return esp_err_t ESP_OK表示成功，其他表示错误
 */
esp_err_t IP_Scanner_ScanNetwork(const char* base_ip, ip_scan_callback_t callback);

/**
 * @brief 保存有效的IP地址到持久化存储
 * @param ip 要保存的IP地址
 * @return esp_err_t ESP_OK表示成功，其他表示错误
 */
esp_err_t IP_Scanner_SaveIP(const char* ip);

/**
 * @brief 从持久化存储加载IP地址
 * @param ip_buffer 用于存储IP地址的缓冲区
 * @param buffer_size 缓冲区大小
 * @return bool true表示成功加载，false表示失败
 */
bool IP_Scanner_LoadIP(char* ip_buffer, size_t buffer_size);

#endif // IP_SCANNER_H 