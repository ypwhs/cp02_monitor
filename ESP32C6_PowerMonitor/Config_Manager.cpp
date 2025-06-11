#include "Config_Manager.h"
#include "RGB_lamp.h"  // 添加RGB_lamp头文件
#include "Power_Monitor.h"
#include "Wireless.h"

WebServer ConfigManager::server(80);
DNSServer ConfigManager::dnsServer;
Preferences ConfigManager::preferences;
bool ConfigManager::configured = false;
bool ConfigManager::apStarted = false;
TaskHandle_t ConfigManager::webServerTaskHandle = NULL;
const char* ConfigManager::AP_SSID = "ESP32_Config";
const char* ConfigManager::NVS_NAMESPACE = "wifi_config";
const char* ConfigManager::NVS_SSID_KEY = "ssid";
const char* ConfigManager::NVS_PASS_KEY = "password";
const char* ConfigManager::NVS_RGB_KEY = "rgb_enabled";
const char* ConfigManager::NVS_MONITOR_URL_KEY = "monitor_url";
const char* ConfigManager::NVS_SCREEN_ROTATION_KEY = "screen_rotation";
const char* DEFAULT_MONITOR_URL = "http://192.168.32.2/metrics";
const char* URL_PREFIX = "http://";
const char* URL_SUFFIX = "/metrics";

// 添加WiFi状态防抖计数器
static uint8_t wifiDisconnectCount = 0;
static uint8_t wifiConnectCount = 0;
static const uint8_t WIFI_STATE_THRESHOLD = 3;  // 状态变化阈值

void ConfigManager::begin() {
    printf("[Config] Initializing configuration manager...\n");
    
    // 初始化Preferences
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        printf("[Config] Failed to initialize preferences\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    
    // 如果没有保存过屏幕方向，设置默认值为90度
    int screenRotation = preferences.getInt(NVS_SCREEN_ROTATION_KEY, 90);
    if (screenRotation != 0 && screenRotation != 90 && screenRotation != 180 && screenRotation != 270) {
        printf("[Config] Setting default screen rotation to 90 degrees\n");
        preferences.putInt(NVS_SCREEN_ROTATION_KEY, 90);
    }
    
     // 先在配置中加载屏幕方向设置
    int savedRotation = getScreenRotation();
    printf("[Config] Applying saved screen rotation: %d degrees\n", savedRotation);
    DisplayManager::applyScreenRotation(savedRotation);

    if (ssid.length() > 0) {
        configured = true;
        printf("[WiFi] Found saved configuration for SSID: %s\n", ssid.c_str());
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 先关闭WiFi，然后重新初始化
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 设置WiFi模式为AP+STA
        WiFi.mode(WIFI_AP_STA);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 连接到保存的WiFi
        String password = preferences.getString(NVS_PASS_KEY, "");
        printf("[WiFi] Attempting to connect to saved network...\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        
        WiFi.begin(ssid.c_str(), password.c_str());
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 等待WiFi连接（最多等待5秒）
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            vTaskDelay(pdMS_TO_TICKS(500));
            printf(".");
            attempts++;
        }
        printf("\n");
        
        if (WiFi.status() == WL_CONNECTED) {
            printf("[WiFi] Connected successfully\n");
        } else {
            printf("[WiFi] Connection failed, showing error screen\n");
            DisplayManager::createWiFiErrorScreen();
        }
    } else {
        printf("[WiFi] No saved configuration found\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 初始化AP模式
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        WiFi.mode(WIFI_AP);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 未配置时显示AP配置屏幕
        DisplayManager::createAPScreen(AP_SSID, WiFi.softAPIP().toString().c_str());
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 启动AP和配置门户
    startConfigPortal();
    
    printf("[Config] Initialization complete\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    
}

void ConfigManager::startConfigPortal() {
    if (!apStarted) {
        vTaskDelay(pdMS_TO_TICKS(100));  // 添加延时
        setupAP();
        apStarted = true;
        
        // 创建web服务任务
        xTaskCreate(
            webServerTask,           // 任务函数
            "WebServerTask",         // 任务名称
            WEB_SERVER_STACK_SIZE,   // 堆栈大小
            NULL,                    // 任务参数
            WEB_SERVER_PRIORITY,     // 任务优先级
            &webServerTaskHandle     // 任务句柄
        );
    }
}

void ConfigManager::webServerTask(void* parameter) {
    printf("[WebServer] Task started\n");
    
    while(1) {
        dnsServer.processNextRequest();
        server.handleClient();
        
        // 定期更新显示和检查WiFi状态
        static unsigned long lastDisplayUpdate = 0;
        static unsigned long lastWiFiCheck = 0;
        static bool lastWiFiStatus = false;
        
        unsigned long currentMillis = millis();
        
        // 每200ms检查一次WiFi状态
        if (currentMillis - lastWiFiCheck >= 200) {
            bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);
            
            // 状态防抖处理
            if (currentWiFiStatus) {
                if (wifiConnectCount < WIFI_STATE_THRESHOLD) {
                    wifiConnectCount++;
                }
                wifiDisconnectCount = 0;
            } else {
                if (wifiDisconnectCount < WIFI_STATE_THRESHOLD) {
                    wifiDisconnectCount++;
                }
                wifiConnectCount = 0;
            }
            
            // 只有当状态计数达到阈值时才更新显示
            if (wifiConnectCount >= WIFI_STATE_THRESHOLD) {
                if (!lastWiFiStatus) {
                    printf("[WiFi] Connection stable\n");
                    if (DisplayManager::isWiFiErrorScreenActive()) {
                        DisplayManager::deleteWiFiErrorScreen();
                    }
                    lastWiFiStatus = true;
                    WIFI_Connection = true;
                }
            } else if (wifiDisconnectCount >= WIFI_STATE_THRESHOLD) {
                if (lastWiFiStatus && configured) {
                    printf("[WiFi] Connection lost (confirmed)\n");
                    DisplayManager::createWiFiErrorScreen();
                    lastWiFiStatus = false;
                    WIFI_Connection = false;
                    
                    // 尝试重新连接
                    String ssid = preferences.getString(NVS_SSID_KEY, "");
                    String password = preferences.getString(NVS_PASS_KEY, "");
                    if (ssid.length() > 0) {
                        WiFi.disconnect();
                        vTaskDelay(pdMS_TO_TICKS(100));
                        WiFi.begin(ssid.c_str(), password.c_str());
                    }
                }
            }
            
            lastWiFiCheck = currentMillis;
        }
        
        // 每秒更新一次显示（其他UI元素）
        if (currentMillis - lastDisplayUpdate >= 1000) {
            lastDisplayUpdate = currentMillis;
        }
        
        // 给其他任务一些执行时间
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ConfigManager::handle() {
    // 空实现，保持API兼容性
}

void ConfigManager::setupAP() {
    // 启动AP
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_AP);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("[WiFi] Starting AP mode...\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 配置AP
    WiFi.softAP(AP_SSID);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 配置DNS服务器
    if (!dnsServer.start(53, "*", WiFi.softAPIP())) {
        printf("[DNS] Failed to start DNS server\n");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 启动Web服务器
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/rgb", HTTP_POST, handleRGBControl);
    server.on("/screen_rotation", HTTP_POST, handleScreenRotation);
    server.on("/reset", HTTP_POST, handleReset);
    server.onNotFound(handleNotFound);
    
    server.begin();  // 直接调用，不检查返回值
    printf("[Web] Server started\n");
    vTaskDelay(pdMS_TO_TICKS(100));
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
                
                <div style='margin-top: 15px;'>
                    屏幕方向:<br>
                    <select name='screen_rotation' style='width: 100%; padding: 8px; margin: 10px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box;'>
                        <option value='0'>0度 (正常)</option>
                        <option value='90'>90度 (向右旋转)</option>
                        <option value='180'>180度 (倒置)</option>
                        <option value='270'>270度 (向左旋转)</option>
                    </select>
                    <small style='color: #666; font-size: 12px;'>屏幕方向修改后自动保存</small>
                </div>
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
                        
                        // 更新屏幕方向选择框（仅在用户未操作时）
                        const rotationSelect = document.querySelector('select[name="screen_rotation"]');
                        if (rotationSelect && !rotationSelect.hasAttribute('data-user-interacting')) {
                            console.log('Current rotation select value:', rotationSelect.value);
                            console.log('Server rotation value:', data.screen_rotation);
                            if (rotationSelect.value != data.screen_rotation.toString()) {
                                console.log('Updating rotation select from', rotationSelect.value, 'to', data.screen_rotation);
                                rotationSelect.value = data.screen_rotation.toString();
                            } else {
                                console.log('Rotation select already matches server value');
                            }
                        } else if (rotationSelect && rotationSelect.hasAttribute('data-user-interacting')) {
                            console.log('Skipping rotation update - user is interacting');
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
                const enabledStr = enabled ? 'true' : 'false';
                console.log('Toggling RGB to: ' + enabledStr);
                
                fetch('/rgb', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'enabled=' + enabledStr
                }).then(response => {
                    console.log('RGB toggle response:', response.status);
                    lastUpdate = 0;
                    updateStatus();
                }).catch(error => {
                    console.error('RGB toggle error:', error);
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
            
            // 初始化屏幕方向选择框
            function initializeRotationSelect() {
                console.log('Initializing rotation select...');
                const rotationSelect = document.querySelector('select[name="screen_rotation"]');
                if (rotationSelect) {
                    // 添加用户交互事件监听器
                    rotationSelect.addEventListener('mousedown', function() {
                        console.log('User started interacting with rotation select');
                        this.setAttribute('data-user-interacting', 'true');
                    });
                    
                    rotationSelect.addEventListener('change', function() {
                        const newRotation = this.value;
                        console.log('User changed rotation select to:', newRotation);
                        
                        // 立即发送AJAX请求保存屏幕方向
                        fetch('/screen_rotation', {
                            method: 'POST',
                            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                            body: 'rotation=' + newRotation
                        }).then(response => {
                            if (response.ok) {
                                console.log('Screen rotation saved successfully');
                                // 检查响应类型
                                const contentType = response.headers.get('content-type');
                                if (contentType && contentType.includes('text/html')) {
                                    // 如果服务器返回HTML，则替换当前页面内容
                                    response.text().then(html => {
                                        document.open();
                                        document.write(html);
                                        document.close();
                                    });
                                }
                            } else {
                                console.error('Failed to save screen rotation');
                            }
                            setTimeout(() => {
                                this.removeAttribute('data-user-interacting');
                                console.log('User interaction flag cleared');
                            }, 1000);
                        }).catch(error => {
                            console.error('Screen rotation save error:', error);
                            setTimeout(() => {
                                this.removeAttribute('data-user-interacting');
                                console.log('User interaction flag cleared');
                            }, 1000);
                        });
                    });
                    
                    rotationSelect.addEventListener('blur', function() {
                        setTimeout(() => {
                            this.removeAttribute('data-user-interacting');
                            console.log('Select blur - interaction flag cleared');
                        }, 1000);
                    });
                }
                updateStatus();
            }
            
            window.onload = function() {
                console.log('Page loaded, initializing...');
                initializeRotationSelect();
            };
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
    json += ",\"screen_rotation\":";
    json += getScreenRotation();
    json += "}";
    server.send(200, "application/json", json);
}

void ConfigManager::handleRGBControl() {
    if (server.hasArg("enabled")) {
        String enabledStr = server.arg("enabled");
        printf("[RGB] Received control request: enabled=%s\n", enabledStr.c_str());
        
        bool enabled = (enabledStr == "true");
        printf("[RGB] Setting RGB enabled state to: %s\n", enabled ? "true" : "false");
        
        setRGBEnabled(enabled);
        
        // 立即应用RGB灯状态
        if (enabled) {
            printf("[RGB] RGB Light enabled - activating\n");
            // 设置RGB灯状态为运行中
            RGB_Lamp_SetRunning(true);
            // 立即启动一次RGB效果以显示颜色
            RGB_Lamp_Loop(1);
        } else {
            printf("[RGB] RGB Light disabled - turning off\n");
            // 立即关闭RGB灯
            RGB_Lamp_Off();
        }
        
        // 立即响应请求
        server.send(200, "text/plain", "OK");
    } else {
        printf("[RGB] Missing RGB control parameter\n");
        server.send(400, "text/plain", "Missing enabled parameter");
    }
}

void ConfigManager::handleScreenRotation() {
    if (server.hasArg("rotation")) {
        String rotationStr = server.arg("rotation");
        int newRotation = rotationStr.toInt();
        int currentRotation = getScreenRotation();
        
        printf("[Config] Received screen rotation: %s (int: %d), current: %d\n", 
               rotationStr.c_str(), newRotation, currentRotation);
        
        if (newRotation != currentRotation && 
            (newRotation == 0 || newRotation == 90 || newRotation == 180 || newRotation == 270)) {
            printf("[Config] Screen rotation changing from %d to %d degrees\n", currentRotation, newRotation);
            setScreenRotation(newRotation);
            
            // 不再立即应用屏幕方向，保存后直接重启设备
            printf("[Config] Screen rotation saved, will be applied after restart\n");
            
            // 发送带有倒计时的HTML页面
            String html = R"rawliteral(
            <!DOCTYPE html>
            <html>
            <head>
                <meta charset='utf-8'>
                <title>屏幕方向已更改</title>
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
                    <h2>屏幕方向已更改</h2>
                    <p>设备将在 <span id='countdown'>5</span> 秒后重启以应用新设置...</p>
                </div>
            </body>
            </html>)rawliteral";
            
            // 确保设置正确的Content-Type头，禁用缓存
            server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            server.sendHeader("Pragma", "no-cache");
            server.sendHeader("Expires", "0");
            server.send(200, "text/html", html);
            
            // 等待响应发送完成
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 重启设备以应用新的屏幕方向设置
            ESP.restart();
        } else if (newRotation == currentRotation) {
            printf("[Config] Screen rotation unchanged: %d degrees\n", currentRotation);
            server.send(200, "text/plain", "OK");
        } else {
            printf("[Config] Invalid screen rotation value: %d\n", newRotation);
            server.send(400, "text/plain", "Invalid rotation value");
        }
    } else {
        printf("[Config] Missing screen rotation parameter\n");
        server.send(400, "text/plain", "Missing rotation parameter");
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
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    vTaskDelay(pdMS_TO_TICKS(1000));
    
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
    vTaskDelay(pdMS_TO_TICKS(100));
    
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
    //printf("[Config] Current monitor URL: %s\n", url.c_str());
    return url;
}

// 保存监控服务器地址
void ConfigManager::saveMonitorUrl(const char* ip) {
    if (strlen(ip) > 0) {
        String fullUrl = String(URL_PREFIX) + ip + URL_SUFFIX;
        preferences.putString(NVS_MONITOR_URL_KEY, fullUrl.c_str());
        printf("[Config] New monitor URL saved: %s (IP: %s)\n", fullUrl.c_str(), ip);
    }
}

// 获取屏幕方向
int ConfigManager::getScreenRotation() {
    return preferences.getInt(NVS_SCREEN_ROTATION_KEY, 90);
}

// 设置屏幕方向
void ConfigManager::setScreenRotation(int rotation) {
    if (rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) {
        preferences.putInt(NVS_SCREEN_ROTATION_KEY, rotation);
        printf("[Config] Screen rotation set to %d degrees\n", rotation);
    } else {
        printf("[Config] Invalid screen rotation value: %d\n", rotation);
    }
} 