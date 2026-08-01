#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_camera.h"

// ===== WiFi Credentials =====
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

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

// ===== HTML Page with JavaScript for Streaming =====
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 YOLO Stream</title>
    <style>
        body { font-family: Arial; text-align: center; background: #1a1a1a; color: white; }
        img { border: 2px solid #00ff88; border-radius: 10px; max-width: 90%; }
        .info { margin: 20px; padding: 15px; background: #333; border-radius: 10px; }
        #detections { color: #00ff88; font-size: 18px; }
    </style>
</head>
<body>
    <h1>📷 ESP32-CAM + YOLO</h1>
    <div class="info">
        <p>Live Stream: <span id="status">Loading...</span></p>
        <p id="detections">Detections: None</p>
    </div>
    <img id="stream" src="/stream" width="640">
    <script>
        const img = document.getElementById('stream');
        const status = document.getElementById('status');
        const detections = document.getElementById('detections');
        
        // Reload image every 100ms (10 FPS)
        setInterval(() => {
            img.src = '/capture?' + new Date().getTime();
            status.textContent = 'Streaming';
        }, 100);
        
        // Fetch detections from Python server (update every 500ms)
        setInterval(async () => {
            try {
                const resp = await fetch('http://YOUR_PYTHON_IP:5000/latest');
                const data = await resp.json();
                if (data.detections && data.detections.length > 0) {
                    const labels = data.detections.map(d => 
                        `${d.class} (${Math.round(d.confidence*100)}%)`
                    ).join(', ');
                    detections.textContent = '🔍 ' + labels;
                } else {
                    detections.textContent = '🔍 No objects detected';
                }
            } catch(e) {
                detections.textContent = '⚠️ Server not connected';
            }
        }, 500);
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    
    // Connect WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("📡 ESP32 IP: ");
    Serial.println(WiFi.localIP());

    // Init Camera
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
        Serial.printf("❌ Camera init failed: 0x%x", err);
        return;
    }
    Serial.println("✅ Camera Ready!");

    // Routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    
    server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            request->send(500, "text/plain", "Camera capture failed");
            return;
        }
        request->send_P(200, "image/jpeg", fb->buf, fb->len);
        esp_camera_fb_return(fb);
    });

    server.begin();
    Serial.println("🌐 Web server started!");
}

void loop() {
    server.handleClient();
    delay(10);
}
