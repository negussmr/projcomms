#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// ===== WiFi Credentials =====
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ===== Python Server IP =====
const char* pythonServer = "http://192.168.1.100:5000/detect";  // CHANGE THIS!

// ===== Camera Pins (AI-THINKER ESP32-CAM) =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80);

// ===== HTML Dashboard =====
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 YOLO Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Arial, sans-serif;
            background: #0a0a0a;
            color: white;
            text-align: center;
            padding: 20px;
        }
        h1 {
            color: #00ff88;
            font-size: 28px;
            margin-bottom: 10px;
            text-shadow: 0 0 20px rgba(0,255,136,0.3);
        }
        .status-bar {
            background: #1a1a1a;
            padding: 15px;
            border-radius: 10px;
            margin: 15px auto;
            max-width: 800px;
            border: 1px solid #333;
        }
        .status-bar span {
            color: #00ff88;
            font-weight: bold;
        }
        .frame-container {
            background: #111;
            border-radius: 10px;
            padding: 10px;
            margin: 15px auto;
            max-width: 800px;
            border: 2px solid #00ff88;
            box-shadow: 0 0 50px rgba(0,255,136,0.1);
        }
        #stream {
            width: 100%;
            height: auto;
            border-radius: 5px;
            display: block;
        }
        .detection-box {
            background: #1a1a1a;
            padding: 15px;
            border-radius: 10px;
            margin: 15px auto;
            max-width: 800px;
            border-left: 4px solid #00ff88;
        }
        .detection-box h3 {
            color: #888;
            font-size: 14px;
            margin-bottom: 10px;
        }
        #detections {
            color: #00ff88;
            font-size: 16px;
            min-height: 30px;
            word-wrap: break-word;
        }
        .badge {
            display: inline-block;
            background: #00ff88;
            color: #000;
            padding: 4px 12px;
            border-radius: 20px;
            font-weight: bold;
            font-size: 14px;
            margin: 3px;
        }
        .footer {
            margin-top: 30px;
            color: #555;
            font-size: 12px;
        }
        .led-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #00ff88;
            animation: pulse 1.5s infinite;
            margin-right: 8px;
        }
        @keyframes pulse {
            0% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.5; transform: scale(0.8); }
            100% { opacity: 1; transform: scale(1); }
        }
        .error { color: #ff4444; }
    </style>
</head>
<body>
    <h1>🎯 ESP32 + YOLO</h1>
    
    <div class="status-bar">
        <span class="led-indicator"></span>
        Status: <span id="status">Connecting...</span>
        &nbsp;&nbsp;|&nbsp;&nbsp;
        📡 IP: <span id="ip">Loading...</span>
    </div>

    <div class="frame-container">
        <img id="stream" src="/capture" alt="Camera Feed">
    </div>

    <div class="detection-box">
        <h3>🔍 YOLO DETECTIONS</h3>
        <div id="detections">Waiting for detections...</div>
    </div>

    <div class="footer">
        ESP32-CAM | YOLOv8 | Real-time Detection
    </div>

    <script>
        const stream = document.getElementById('stream');
        const status = document.getElementById('status');
        const detections = document.getElementById('detections');
        const ipDisplay = document.getElementById('ip');
        
        // Get ESP32 IP
        fetch('/ip')
            .then(res => res.text())
            .then(ip => ipDisplay.textContent = ip)
            .catch(() => ipDisplay.textContent = 'Unknown');

        // Refresh stream every 150ms
        function refreshStream() {
            stream.src = '/capture?' + Date.now();
            status.textContent = 'Streaming';
        }
        setInterval(refreshStream, 150);

        // Fetch detections from Python server every 500ms
        async function fetchDetections() {
            try {
                const response = await fetch('/detections');
                const data = await response.json();
                
                if (data.error) {
                    detections.innerHTML = '<span class="error">⚠️ ' + data.error + '</span>';
                    return;
                }
                
                if (data.detections && data.detections.length > 0) {
                    let html = '';
                    data.detections.forEach(d => {
                        html += `<span class="badge">${d.class} ${Math.round(d.confidence*100)}%</span>`;
                    });
                    detections.innerHTML = html + ` <span style="color:#888;font-size:14px;">(${data.detections.length} objects)</span>`;
                } else {
                    detections.innerHTML = '👀 No objects detected';
                }
            } catch (error) {
                detections.innerHTML = '<span class="error">⚠️ Python server not connected</span>';
            }
        }
        setInterval(fetchDetections, 500);
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== ESP32-CAM YOLO System ===");

    // ===== Connect WiFi =====
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi Connected!");
        Serial.print("📡 IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("❌ WiFi connection failed!");
        Serial.println("Check SSID/password and restart.");
        delay(5000);
        ESP.restart();
    }

    // ===== Initialize Camera =====
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Camera init failed: 0x%x\n", err);
        Serial.println("Check wiring and restart.");
        delay(5000);
        ESP.restart();
    }
    Serial.println("✅ Camera ready!");

    // ===== Setup Web Server Routes =====
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", index_html);
    });

    server.on("/ip", HTTP_GET, []() {
        server.send(200, "text/plain", WiFi.localIP().toString());
    });

    server.on("/capture", HTTP_GET, []() {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            server.send(500, "text/plain", "Camera capture failed");
            return;
        }
        server.send_P(200, "image/jpeg", fb->buf, fb->len);
        esp_camera_fb_return(fb);
    });

    // Proxy to Python server for detections
    server.on("/detections", HTTP_GET, []() {
        // Take a photo
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            server.send(500, "application/json", "{\"error\":\"Camera failed\"}");
            return;
        }

        // Send to Python server
        WiFiClient client;
        if (!client.connect(pythonServer, 80)) {
            esp_camera_fb_return(fb);
            server.send(500, "application/json", "{\"error\":\"Python server offline\"}");
            return;
        }

        // Build HTTP POST request
        String boundary = "----ESP32Boundary";
        client.println("POST /detect HTTP/1.1");
        client.print("Host: ");
        client.println(pythonServer);
        client.println("Content-Type: multipart/form-data; boundary=" + boundary);
        client.print("Content-Length: ");
        client.println(fb->len + 200);
        client.println();
        client.println("--" + boundary);
        client.println("Content-Disposition: form-data; name=\"image\"; filename=\"image.jpg\"");
        client.println("Content-Type: image/jpeg");
        client.println();
        client.write(fb->buf, fb->len);
        client.println();
        client.println("--" + boundary + "--");
        client.println();
        
        esp_camera_fb_return(fb);

        // Read response
        String response = "";
        unsigned long timeout = millis() + 5000;
        while (client.connected() && millis() < timeout) {
            if (client.available()) {
                response += client.readString();
            }
        }
        client.stop();

        // Extract JSON from response
        int jsonStart = response.indexOf('{');
        int jsonEnd = response.lastIndexOf('}');
        if (jsonStart != -1 && jsonEnd != -1) {
            server.send(200, "application/json", response.substring(jsonStart, jsonEnd + 1));
        } else {
            server.send(500, "application/json", "{\"error\":\"Invalid response\"}");
        }
    });

    server.begin();
    Serial.println("✅ Web server started!");
    Serial.println("🌐 Open http://" + WiFi.localIP().toString() + " in your browser");
}

void loop() {
    server.handleClient();
    delay(10);
}
