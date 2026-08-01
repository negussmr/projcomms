#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// ===== WiFi Credentials =====
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ===== Python Server IP =====
const char* pythonServer = "192.168.1.100";  // CHANGE THIS!
const int pythonPort = 5000;

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
    <title>ESP32 YOLO</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; background: #0a0a0a; color: white; text-align: center; padding: 20px; }
        h1 { color: #00ff88; }
        .frame { background: #111; border-radius: 10px; padding: 10px; max-width: 800px; margin: auto; border: 2px solid #00ff88; }
        #stream { width: 100%; height: auto; border-radius: 5px; }
        .detections { background: #1a1a1a; padding: 15px; border-radius: 10px; max-width: 800px; margin: 15px auto; }
        .badge { display: inline-block; background: #00ff88; color: #000; padding: 4px 12px; border-radius: 20px; margin: 3px; }
        .status { color: #888; margin: 10px; }
        .error { color: #ff4444; }
    </style>
</head>
<body>
    <h1>🎯 ESP32 + YOLO</h1>
    <div class="frame">
        <img id="stream" src="/capture">
    </div>
    <div class="detections">
        <h3>🔍 Detections</h3>
        <div id="detections">Waiting...</div>
    </div>
    <div class="status" id="status">Loading...</div>
    
    <script>
        const stream = document.getElementById('stream');
        const detectionsDiv = document.getElementById('detections');
        const statusDiv = document.getElementById('status');
        
        // Refresh stream every 200ms
        setInterval(() => {
            stream.src = '/capture?' + Date.now();
        }, 200);
        
        // Get detections every 500ms
        setInterval(async () => {
            try {
                const response = await fetch('/detections');
                const data = await response.json();
                
                if (data.error) {
                    detectionsDiv.innerHTML = '<span class="error">⚠️ ' + data.error + '</span>';
                    return;
                }
                
                if (data.detections && data.detections.length > 0) {
                    let html = '';
                    data.detections.forEach(d => {
                        html += `<span class="badge">${d.class} ${Math.round(d.confidence*100)}%</span>`;
                    });
                    detectionsDiv.innerHTML = html + ` (${data.detections.length} objects)`;
                    statusDiv.textContent = '✅ Active';
                    statusDiv.style.color = '#00ff88';
                } else {
                    detectionsDiv.innerHTML = '👀 No objects detected';
                    statusDiv.textContent = '✅ Active';
                    statusDiv.style.color = '#00ff88';
                }
            } catch (e) {
                detectionsDiv.innerHTML = '<span class="error">⚠️ Python server offline</span>';
                statusDiv.textContent = '❌ Python server not connected';
                statusDiv.style.color = '#ff4444';
            }
        }, 500);
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 YOLO System ===");

    // ===== Connect WiFi =====
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
        Serial.print("📡 IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("❌ WiFi failed!");
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
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Camera init failed: 0x%x\n", err);
        ESP.restart();
    }
    Serial.println("✅ Camera ready!");

    // ===== WEB SERVER ROUTES (FIXED) =====
    
    // Home page
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", index_html);  // ← FIXED: server.send_P works here
    });

    // Capture image
    server.on("/capture", HTTP_GET, []() {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            server.send(500, "text/plain", "Camera capture failed");  // ← FIXED: no _P
            return;
        }
        // ✅ FIXED: Use server.send_P (not server.send)
        server.send_P(200, "image/jpeg", fb->buf, fb->len);  // ← FIXED: send_P with 3 params
        esp_camera_fb_return(fb);
    });

    // Get detections from Python server
    server.on("/detections", HTTP_GET, []() {
        // Take photo
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            server.send(500, "application/json", "{\"error\":\"Camera failed\"}");  // ← FIXED
            return;  // ← FIXED: Added missing return
        }

        // Send to Python server
        WiFiClient client;
        if (!client.connect(pythonServer, pythonPort)) {
            esp_camera_fb_return(fb);
            server.send(500, "application/json", "{\"error\":\"Python server offline\"}");  // ← FIXED
            return;
        }

        // Build multipart POST request
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

        // Extract JSON from HTTP response
        int jsonStart = response.indexOf('{');
        int jsonEnd = response.lastIndexOf('}');
        if (jsonStart != -1 && jsonEnd != -1) {
            server.send(200, "application/json", response.substring(jsonStart, jsonEnd + 1));  // ← FIXED
        } else {
            server.send(500, "application/json", "{\"error\":\"Invalid response\"}");  // ← FIXED
        }
    });

    server.begin();
    Serial.println("✅ Web server started!");
    Serial.println("🌐 Open http://" + WiFi.localIP().toString());
}

void loop() {
    server.handleClient();
    delay(10);
}
