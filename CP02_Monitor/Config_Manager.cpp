#include "Config_Manager.h"
#include "RGB_lamp.h"  // 添加RGB_lamp头文件
#include "Power_Monitor.h"

WebServer ConfigManager::server(80);
DNSServer ConfigManager::dnsServer;
Preferences ConfigManager::preferences;
bool ConfigManager::configured = false;
bool ConfigManager::apStarted = false;
const char* ConfigManager::AP_SSID = "ESP32_Config";
const char* ConfigManager::NVS_NAMESPACE = "wifi_config";
const char* ConfigManager::NVS_SSID_KEY = "ssid";
const char* ConfigManager::NVS_PASS_KEY = "password";
const char* ConfigManager::NVS_RGB_KEY = "rgb_enabled";
const char* ConfigManager::NVS_MONITOR_URL_KEY = "monitor_url";
const char* DEFAULT_MONITOR_URL = "http://192.168.32.2/metrics";
const char* URL_PREFIX = "http://";
const char* URL_SUFFIX = "/metrics";

void ConfigManager::begin() {
    printf("[Config] Initializing configuration manager...\n");
    
    // 初始化Preferences
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        printf("[Config] Failed to initialize preferences\n");
        delay(1000);
        return;
    }
    
    // 检查是否已配置
    String ssid = preferences.getString(NVS_SSID_KEY, "");
    String monitorUrl = preferences.getString(NVS_MONITOR_URL_KEY, "");
    
    // 如果没有保存过监控地址，设置默认值
    if (monitorUrl.length() == 0) {
        printf("[Config] Setting default monitor URL\n");
        preferences.putString(NVS_MONITOR_URL_KEY, DEFAULT_MONITOR_URL);
    }
    
    if (ssid.length() > 0) {
        configured = true;
        printf("[WiFi] Found saved configuration for SSID: %s\n", ssid.c_str());
        delay(100);
        
        // 先关闭WiFi，然后重新初始化
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
        
        // 设置WiFi模式为AP+STA
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        
        // 连接到保存的WiFi
        String password = preferences.getString(NVS_PASS_KEY, "");
        printf("[WiFi] Attempting to connect to saved network...\n");
        delay(100);
        
        WiFi.begin(ssid.c_str(), password.c_str());
        delay(100);
        
        // 等待WiFi连接（最多等待5秒）
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            printf(".");
            attempts++;
        }
        printf("\n");
        
        if (WiFi.status() == WL_CONNECTED) {
            printf("[WiFi] Connected successfully\n");
            //DisplayManager::showMonitorScreen();
        } else {
            printf("[WiFi] Connection failed, showing error screen\n");
            DisplayManager::createWiFiErrorScreen();
        }
    } else {
        printf("[WiFi] No saved configuration found\n");
        delay(100);
        
        // 初始化AP模式
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
        
        WiFi.mode(WIFI_AP);
        delay(100);
        
        // 未配置时显示AP配置屏幕
        DisplayManager::createAPScreen(AP_SSID, WiFi.softAPIP().toString().c_str());
    }
    
    delay(100);
    
    // 启动AP和配置门户
    startConfigPortal();
    
    printf("[Config] Initialization complete\n");
    delay(100);
}

void ConfigManager::startConfigPortal() {
    if (!apStarted) {
        delay(100);  // 添加延时
        setupAP();
        apStarted = true;
    }
}

void ConfigManager::handle() {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // 定期更新显示
    static unsigned long lastDisplayUpdate = 0;
    static bool lastWiFiStatus = false;
    
    if (millis() - lastDisplayUpdate >= 1000) {  // 每秒更新一次显示
        bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);
        
        // 检查WiFi状态变化
        if (currentWiFiStatus != lastWiFiStatus) {
            if (!currentWiFiStatus && configured) {
                // 已配置但WiFi断开连接时，显示错误屏幕
                printf("[WiFi] Connection lost, showing error screen\n");
                DisplayManager::createWiFiErrorScreen();
            } else if (currentWiFiStatus) {
                // WiFi连接成功时，删除错误屏幕并显示监控屏幕
                printf("[WiFi] Connection established\n");
                if (DisplayManager::isWiFiErrorScreenActive()) {
                    DisplayManager::deleteWiFiErrorScreen();
                    //DisplayManager::showMonitorScreen();
                }
            }
            lastWiFiStatus = currentWiFiStatus;
        }
        
        lastDisplayUpdate = millis();
    }
}

void ConfigManager::setupAP() {
    // 启动AP
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_AP);
        delay(100);
    }
    
    printf("[WiFi] Starting AP mode...\n");
    delay(100);
    
    // 配置AP
    WiFi.softAP(AP_SSID);
    delay(100);
    
    // 配置DNS服务器
    if (!dnsServer.start(53, "*", WiFi.softAPIP())) {
        printf("[DNS] Failed to start DNS server\n");
        delay(100);
    }
    
    // 启动Web服务器
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/rgb", HTTP_POST, handleRGBControl);
    server.on("/reset", HTTP_POST, handleReset);
    server.onNotFound(handleNotFound);
    
    server.begin();  // 直接调用，不检查返回值
    printf("[Web] Server started\n");
    delay(100);
}

void ConfigManager::handleRoot() {
    // 获取当前URL并提取IP地址
    String currentUrl = getMonitorUrl();
    String currentIP = extractIPFromUrl(currentUrl);
    
    printf("[Config] Current URL: %s, Extracted IP: %s\n", currentUrl.c_str(), currentIP.c_str());
    
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset='utf-8'>
        <title>ESP32 配置</title>
        <meta name='viewport' content='width=device-width, initial-scale=1'>
        <style>
            body { font-family: Arial; margin: 20px; background: #f0f0f0; }
            .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
            .status { margin-bottom: 20px; padding: 10px; border-radius: 5px; }
            .connected { background: #e8f5e9; color: #2e7d32; }
            .disconnected { background: #ffebee; color: #c62828; }
            input { width: 100%; padding: 8px; margin: 10px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
            button { width: 100%; padding: 10px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; margin-bottom: 10px; }
            button:hover { background: #45a049; }
            .danger-button { background: #f44336; }
            .danger-button:hover { background: #d32f2f; }
            .status-box { margin-top: 20px; }
            .switch { position: relative; display: inline-block; width: 60px; height: 34px; }
            .switch input { opacity: 0; width: 0; height: 0; }
            .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
            .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
            input:checked + .slider { background-color: #4CAF50; }
            input:checked + .slider:before { transform: translateX(26px); }
            .control-group { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }
            .modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }
            .modal-content { background-color: #fefefe; margin: 15% auto; padding: 20px; border-radius: 5px; max-width: 300px; text-align: center; }
            .modal-buttons { display: flex; justify-content: space-between; margin-top: 20px; }
            .modal-buttons button { width: 45%; margin: 0; }
            .cancel-button { background: #9e9e9e; }
            .cancel-button:hover { background: #757575; }
        </style>
    </head>
    <body>
        <div class='container'>
            <h2>ESP32 配置</h2>
            <div id='status' class='status'></div>
            
            <div class='control-group'>
                <h3>WiFi设置</h3>
                <form method='post' action='/save'>
                    WiFi名称:<br>
                    <input type='text' name='ssid'><br>
                    WiFi密码:<br>
                    <input type='password' name='password'><br>
                    小电拼服务器IP地址:<br>
                    <input type='text' name='monitor_url' value=')rawliteral";
    
    html += currentIP;
    
    html += R"rawliteral(' placeholder='例如: 192.168.32.2'><br>
                    <button type='submit'>保存配置</button>
                </form>
            </div>
            
            <div class='control-group'>
                <h3>RGB灯控制</h3>
                <label class='switch'>
                    <input type='checkbox' id='rgb-switch' onchange='toggleRGB()'>
                    <span class='slider'></span>
                </label>
                <span style='margin-left: 10px;'>RGB灯状态</span>
            </div>

            <div class='control-group'>
                <h3>系统设置</h3>
                <button class='danger-button' onclick='showResetConfirm()'>重置所有配置</button>
            </div>
        </div>

        <div id='resetModal' class='modal'>
            <div class='modal-content'>
                <h3>确认重置</h3>
                <p>这将清除所有配置并重启设备。确定要继续吗？</p>
                <div class='modal-buttons'>
                    <button class='cancel-button' onclick='hideResetConfirm()'>取消</button>
                    <button class='danger-button' onclick='doReset()'>确认重置</button>
                </div>
            </div>
        </div>
        <script>
            let lastUpdate = 0;
            let updateInterval = 2000;
            let statusUpdateTimeout = null;

            function updateStatus() {
                const now = Date.now();
                if (now - lastUpdate < updateInterval) {
                    return;
                }
                lastUpdate = now;

                fetch('/status')
                    .then(response => response.json())
                    .then(data => {
                        const statusBox = document.getElementById('status');
                        if (data.connected) {
                            statusBox.innerHTML = `已连接到WiFi: ${data.ssid}<br>IP地址: ${data.ip}`;
                            statusBox.className = 'status connected';
                        } else {
                            statusBox.innerHTML = '未连接到WiFi';
                            statusBox.className = 'status disconnected';
                        }
                        const rgbSwitch = document.getElementById('rgb-switch');
                        if (rgbSwitch.checked !== data.rgb_enabled) {
                            rgbSwitch.checked = data.rgb_enabled;
                        }
                    })
                    .catch(() => {
                        if (statusUpdateTimeout) {
                            clearTimeout(statusUpdateTimeout);
                        }
                        statusUpdateTimeout = setTimeout(updateStatus, updateInterval);
                    });
            }
            
            function toggleRGB() {
                const enabled = document.getElementById('rgb-switch').checked;
                fetch('/rgb', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'enabled=' + enabled
                }).then(() => {
                    lastUpdate = 0;
                    updateStatus();
                });
            }

            function showResetConfirm() {
                document.getElementById('resetModal').style.display = 'block';
            }

            function hideResetConfirm() {
                document.getElementById('resetModal').style.display = 'none';
            }

            function doReset() {
                hideResetConfirm();
                fetch('/reset', {
                    method: 'POST'
                }).then(() => {
                    alert('配置已重置，设备将重启...');
                    setTimeout(() => {
                        window.location.reload();
                    }, 5000);
                });
            }
            
            // 点击模态框外部时关闭
            window.onclick = function(event) {
                const modal = document.getElementById('resetModal');
                if (event.target == modal) {
                    hideResetConfirm();
                }
            }
            
            window.onload = updateStatus;
            setInterval(updateStatus, updateInterval);
        </script>
    </body>
    </html>)rawliteral";
    
    server.send(200, "text/html", html);
}

void ConfigManager::handleStatus() {
    String json = "{\"connected\":";
    json += WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"ssid\":\"";
    json += WiFi.SSID();
    json += "\",\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\",\"rgb_enabled\":";
    json += isRGBEnabled() ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
}

void ConfigManager::handleRGBControl() {
    if (server.hasArg("enabled")) {
        bool enabled = server.arg("enabled") == "true";
        setRGBEnabled(enabled);
        
        // 立即应用RGB灯状态
        if (enabled) {
            printf("RGB Light enabled\n");
            // 立即启动一次RGB效果
            RGB_Lamp_Loop(1);
        } else {
            printf("RGB Light disabled\n");
            // 立即关闭RGB灯
            RGB_Lamp_Off();
        }
        
        // 立即响应请求
        server.send(200, "text/plain", "OK");
    } else {
        printf("Missing RGB control parameter\n");
        server.send(400, "text/plain", "Missing enabled parameter");
    }
}

void ConfigManager::handleSave() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String monitorUrl = server.arg("monitor_url");
    
    bool needRestart = false;
    bool configChanged = false;
    
    if (ssid.length() > 0) {
        saveConfig(ssid.c_str(), password.c_str());
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        needRestart = true;
        configChanged = true;
    }
    
    if (monitorUrl.length() > 0) {
        String currentUrl = getMonitorUrl();
        String newIp = monitorUrl;
        String currentIp = extractIPFromUrl(currentUrl);
        
        if (currentIp != newIp) {
            saveMonitorUrl(newIp.c_str());
            needRestart = true;
            configChanged = true;
        }
    }
    
    if (configChanged) {
        String html = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset='utf-8'>
            <title>配置已保存</title>
            <meta name='viewport' content='width=device-width, initial-scale=1'>
            <style>
                body { font-family: Arial; margin: 20px; text-align: center; }
                .message { margin: 20px; padding: 20px; background: #e8f5e9; border-radius: 5px; }
                .countdown { font-size: 24px; margin: 20px; }
            </style>
            <script>
                let count = 5;
                function updateCountdown() {
                    document.getElementById('countdown').textContent = count;
                    if (count > 0) {
                        count--;
                        setTimeout(updateCountdown, 1000);
                    }
                }
                window.onload = function() {
                    updateCountdown();
                    setTimeout(function() {
                        window.location.href = '/';
                    }, 5000);
                }
            </script>
        </head>
        <body>
            <div class='message'>
                <h2>配置已保存</h2>
                <p>设备将在 <span id='countdown'>5</span> 秒后重启...</p>
            </div>
        </body>
        </html>)rawliteral";
        
        server.send(200, "text/html", html);
        delay(1000);
        if (needRestart) {
            ESP.restart();
        }
    } else {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    }
}

void ConfigManager::handleReset() {
    printf("[Config] Processing reset request...\n");
    
    // 先重置配置
    resetConfig();
    
    // 然后发送响应
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset='utf-8'>
        <title>重置配置</title>
        <meta name='viewport' content='width=device-width, initial-scale=1'>
        <style>
            body { font-family: Arial; margin: 20px; text-align: center; }
            .message { margin: 20px; padding: 20px; background: #ffebee; border-radius: 5px; }
            .countdown { font-size: 24px; margin: 20px; }
        </style>
        <script>
            let count = 5;
            function updateCountdown() {
                document.getElementById('countdown').textContent = count;
                if (count > 0) {
                    count--;
                    setTimeout(updateCountdown, 1000);
                }
            }
            window.onload = function() {
                updateCountdown();
                setTimeout(function() {
                    window.location.href = '/';
                }, 5000);
            }
        </script>
    </head>
    <body>
        <div class='message'>
            <h2>配置已重置</h2>
            <p>设备将在 <span id='countdown'>5</span> 秒后重启...</p>
        </div>
    </body>
    </html>)rawliteral";
    
    server.send(200, "text/html", html);
    
    // 等待响应发送完成
    delay(1000);
    
    // 最后重启设备
    ESP.restart();
}

void ConfigManager::handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

bool ConfigManager::isConfigured() {
    return configured;
}

bool ConfigManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool ConfigManager::isRGBEnabled() {
    return preferences.getBool(NVS_RGB_KEY, false);
}

void ConfigManager::setRGBEnabled(bool enabled) {
    preferences.putBool(NVS_RGB_KEY, enabled);
}

void ConfigManager::resetConfig() {
    printf("[Config] Resetting all configurations...\n");
    
    // 清除所有配置
    preferences.clear();
    
    // 重新设置默认的监控URL
    preferences.putString(NVS_MONITOR_URL_KEY, DEFAULT_MONITOR_URL);
    printf("[Config] Reset monitor URL to default: %s\n", DEFAULT_MONITOR_URL);
    
    // 断开WiFi连接
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    configured = false;
    printf("[Config] All configurations have been reset\n");
    
    // 更新显示
    updateDisplay();
}

String ConfigManager::getSSID() {
    return preferences.getString(NVS_SSID_KEY, "");
}

String ConfigManager::getPassword() {
    return preferences.getString(NVS_PASS_KEY, "");
}

void ConfigManager::saveConfig(const char* ssid, const char* password) {
    preferences.putString(NVS_SSID_KEY, ssid);
    preferences.putString(NVS_PASS_KEY, password);
    configured = true;
    printf("New WiFi configuration saved\n");
    printf("SSID: %s\n", ssid);
    updateDisplay();
}

void ConfigManager::updateDisplay() {
    if (!configured) {
        // 只有在未配置时才显示AP配置屏幕
        if (!DisplayManager::isAPScreenActive()) {
            DisplayManager::createAPScreen(AP_SSID, WiFi.softAPIP().toString().c_str());
        }
    } else {
        // 已配置WiFi时，显示监控屏幕
        if (DisplayManager::isAPScreenActive()) {
            DisplayManager::deleteAPScreen();
            //DisplayManager::showMonitorScreen();
        }
    }
}

// 从完整URL中提取IP地址
String ConfigManager::extractIPFromUrl(const String& url) {
    int startPos = url.indexOf("://");
    if (startPos != -1) {
        startPos += 3;  // 跳过 "://"
        int endPos = url.indexOf("/", startPos);
        if (endPos != -1) {
            return url.substring(startPos, endPos);
        } else {
            return url.substring(startPos);
        }
    }
    return url;
}

// 获取监控服务器地址
String ConfigManager::getMonitorUrl() {
    String url = preferences.getString(NVS_MONITOR_URL_KEY, DEFAULT_MONITOR_URL);
    printf("[Config] Current monitor URL: %s\n", url.c_str());
    return url;
}

// 保存监控服务器地址
void ConfigManager::saveMonitorUrl(const char* ip) {
    if (strlen(ip) > 0) {
        String fullUrl = String(URL_PREFIX) + ip + URL_SUFFIX;
        preferences.putString(NVS_MONITOR_URL_KEY, fullUrl.c_str());
        printf("[Config] New monitor URL saved: %s\n", fullUrl.c_str());
    }
} 