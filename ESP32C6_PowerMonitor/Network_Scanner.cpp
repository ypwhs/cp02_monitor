#include "Network_Scanner.h"

bool NetworkScanner::findMetricsServer(String& outUrl, bool printLog) {
    if (WiFi.status() != WL_CONNECTED) {
        if(printLog) printf("[Scanner] WiFi not connected\n");
        return false;
    }

    if(printLog) printf("[Scanner] Starting mDNS scan for cp02 device...\n");
    
    // 首先尝试使用mDNS查找cp02设备
    String deviceIP;
    if (findDeviceByMDNS("cp02", deviceIP, printLog)) {
        if(printLog) printf("[Scanner] Found cp02 via mDNS at IP: %s\n", deviceIP.c_str());
        
        // 验证设备是否提供metrics端点
        if (testMetricsEndpoint(deviceIP, printLog)) {
            outUrl = deviceIP;
            if(printLog) printf("[Scanner] cp02 metrics server confirmed at: %s\n", outUrl.c_str());
            return true;
        } else {
            if(printLog) printf("[Scanner] cp02 found but no metrics endpoint available\n");
        }
    }
    
    if(printLog) printf("[Scanner] mDNS scan failed, falling back to IP scan...\n");
    
    // 如果mDNS查找失败，回退到传统的IP扫描方式
    return fallbackIPScan(outUrl, printLog);
}

bool NetworkScanner::testMetricsEndpoint(const String& ip, bool printLog) {
    if(printLog) printf("[Scanner] Testing IP: %s\n", ip.c_str());
    
    HTTPClient http;
    String url = "http://" + ip + "/metrics";
    
    http.begin(url);
    http.setTimeout(500); // 稍微增加超时时间以确保可靠性
    
    int httpCode = http.GET();
    bool success = (httpCode == HTTP_CODE_OK);
    
    if(printLog) {
        if(success) {
            printf("[Scanner] Success: %s responded with metrics data\n", ip.c_str());
        } else {
            printf("[Scanner] Failed: %s (HTTP code: %d)\n", ip.c_str(), httpCode);
        }
    }
    
    http.end();
    return success;
}

bool NetworkScanner::findDeviceByMDNS(const char* hostname, String& outIP, bool printLog) {
    if(printLog) printf("[Scanner] Starting mDNS lookup for hostname: %s\n", hostname);
    
    // 确保mDNS已初始化
    if (!MDNS.begin("esp32_power_monitor")) {
        if(printLog) printf("[Scanner] Failed to start mDNS\n");
        return false;
    }
    
    // 查找指定主机名的设备
    if(printLog) printf("[Scanner] Querying mDNS for %s.local...\n", hostname);
    
    IPAddress resolvedIP = MDNS.queryHost(hostname);
    
    if (resolvedIP == INADDR_NONE) {
        if(printLog) printf("[Scanner] Failed to resolve %s.local via mDNS\n", hostname);
        
        // 尝试浏览HTTP服务来查找设备
        if(printLog) printf("[Scanner] Browsing for HTTP services...\n");
        
        int n = MDNS.queryService("http", "tcp");
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                String serviceName = MDNS.hostname(i);
                if(printLog) printf("[Scanner] Found HTTP service: %s\n", serviceName.c_str());
                
                // 检查服务名是否包含我们要查找的主机名
                if (serviceName.indexOf(hostname) >= 0) {
                    IPAddress serviceIP = MDNS.address(i);
                    outIP = serviceIP.toString();
                    if(printLog) printf("[Scanner] Found %s via service discovery at IP: %s\n", hostname, outIP.c_str());
                    return true;
                }
            }
        }
        
        if(printLog) printf("[Scanner] No matching services found\n");
        return false;
    }
    
    outIP = resolvedIP.toString();
    if(printLog) printf("[Scanner] Successfully resolved %s.local to IP: %s\n", hostname, outIP.c_str());
    return true;
}

bool NetworkScanner::fallbackIPScan(String& outUrl, bool printLog) {
    if(printLog) printf("[Scanner] Starting fallback IP scan...\n");
    
    // 获取本机IP地址
    IPAddress localIP = WiFi.localIP();
    String baseIP = String(localIP[0]) + "." + String(localIP[1]) + "." + String(localIP[2]) + ".";
    int localLastOctet = localIP[3];
    
    if(printLog) printf("[Scanner] Scanning network from %s\n", baseIP.c_str());
    
    // 先扫描本机IP附近的范围（前后10个IP）
    int startNearby = max(1, localLastOctet - 10);
    int endNearby = min(254, localLastOctet + 10);
    
    // 扫描附近IP
    for(int i = startNearby; i <= endNearby; i++) {
        if(i == localLastOctet) continue; // 跳过本机IP
        
        String testIP = baseIP + String(i);
        if(testMetricsEndpoint(testIP, printLog)) {
            outUrl = testIP;
            if(printLog) printf("[Scanner] Found metrics server at IP: %s\n", outUrl.c_str());
            return true;
        }
    }
    
    // 如果附近没找到，扫描剩余的IP范围
    for(int i = 1; i <= 254; i++) {
        // 跳过已经扫描过的范围
        if(i >= startNearby && i <= endNearby) continue;
        
        String testIP = baseIP + String(i);
        if(testMetricsEndpoint(testIP, printLog)) {
            outUrl = testIP;
            if(printLog) printf("[Scanner] Found metrics server at IP: %s\n", outUrl.c_str());
            return true;
        }
    }
    
    if(printLog) printf("[Scanner] No metrics server found in fallback scan\n");
    return false;
} 