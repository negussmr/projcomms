/*
 * esp32_cam.ino
 * =============
 * Firmware for the AI-Thinker ESP32-CAM module. Streams stable MJPEG video
 * over Wi-Fi at:
 *
 *      http://<ESP_IP>:81/stream
 *
 * which is the exact endpoint the Python server (server.py / config.py's
 * STREAM_URL) connects to.
 *
 * Built on top of the official Espressif "CameraWebServer" example
 * (esp32-camera library), extended with:
 *   - Configurable Wi-Fi credentials
 *   - Automatic Wi-Fi reconnect if the connection drops
 *   - Configurable resolution / JPEG quality / target frame rate
 *
 * Board settings (Arduino IDE):
 *   Board:        "AI Thinker ESP32-CAM"
 *   Upload Speed: 115200
 *   Partition:    "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 * Required library: "esp32" board package (installs the esp_camera driver).
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"          // Brownout detector control
#include "soc/rtc_cntl_reg.h" // Brownout detector control
#include "esp_http_server.h"

// ============================================================================
// USER CONFIGURATION -- edit these values for your setup
// ============================================================================

// --- Wi-Fi credentials --------------------------------------------------
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// --- Camera resolution ---------------------------------------------------
// Options (increasing size): FRAMESIZE_QQVGA (160x120), FRAMESIZE_QVGA
// (320x240), FRAMESIZE_CIF (400x296), FRAMESIZE_VGA (640x480),
// FRAMESIZE_SVGA (800x600), FRAMESIZE_XGA (1024x768),
// FRAMESIZE_SXGA (1280x1024), FRAMESIZE_UXGA (1600x1200)
// Lower resolution = higher frame rate and lower latency for detection.
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA

// --- JPEG quality ----------------------------------------------------------
// Range 0-63. LOWER number = HIGHER quality (and larger frame size / more
// bandwidth). 10-12 is a good balance of quality vs stream speed.
#define CAMERA_JPEG_QUALITY 12

// --- Target stream frame rate (approximate, best-effort) ------------------
// The stream loop will not send frames faster than this.
#define TARGET_FPS 20

// --- Wi-Fi reconnect behaviour ----------------------------------------------
#define WIFI_RECONNECT_INTERVAL_MS 5000
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ============================================================================
// AI-THINKER ESP32-CAM PIN MAP -- do not change unless using different hardware
// ============================================================================
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

// ============================================================================
// Globals
// ============================================================================
httpd_handle_t stream_httpd = NULL;
unsigned long last_wifi_check = 0;
const unsigned long min_frame_interval_ms = 1000 / TARGET_FPS;

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ============================================================================
// MJPEG stream handler -- served at http://ESP_IP:81/stream
// ============================================================================
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t jpg_buf_len = 0;
  uint8_t *jpg_buf = NULL;
  char part_buf[64];
  unsigned long last_frame_ms = 0;

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    // Respect TARGET_FPS so we never overload the ESP32 or the Wi-Fi link.
    unsigned long now = millis();
    if (now - last_frame_ms < min_frame_interval_ms) {
      delay(min_frame_interval_ms - (now - last_frame_ms));
    }
    last_frame_ms = millis();

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAMERA] Frame capture failed. Retrying...");
      res = ESP_FAIL;
      break;
    }

    if (fb->format != PIXFORMAT_JPEG) {
      bool converted = frame2jpg(fb, CAMERA_JPEG_QUALITY, &jpg_buf, &jpg_buf_len);
      esp_camera_fb_return(fb);
      fb = NULL;
      if (!converted) {
        Serial.println("[CAMERA] JPEG conversion failed.");
        res = ESP_FAIL;
        break;
      }
    } else {
      jpg_buf_len = fb->len;
      jpg_buf = fb->buf;
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t header_len = snprintf(part_buf, sizeof(part_buf), STREAM_PART, jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, header_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
    } else if (jpg_buf) {
      free(jpg_buf);
      jpg_buf = NULL;
    }

    if (res != ESP_OK) {
      Serial.println("[STREAM] Client disconnected or send failed.");
      break;
    }
  }

  return res;
}

// ============================================================================
// Simple index page so visiting http://ESP_IP/ confirms the device is alive
// ============================================================================
static esp_err_t index_handler(httpd_req_t *req) {
  const char *html =
      "<html><body><h2>ESP32-CAM is running</h2>"
      "<p>MJPEG stream is served on port 81 at /stream</p></body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, strlen(html));
}

// ============================================================================
// Start the HTTP servers: control server on port 80, stream server on 81
// ============================================================================
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL};

  httpd_handle_t control_httpd = NULL;
  if (httpd_start(&control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(control_httpd, &index_uri);
  }

  // Streaming server runs on its own port (81) exactly as required by
  // config.STREAM_URL in the Python project ("http://ESP_IP:81/stream").
  config.server_port = 81;
  config.ctrl_port = 32768;

  httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL};

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("[STREAM] Streaming server started on port 81.");
  } else {
    Serial.println("[STREAM] Failed to start streaming server!");
  }
}

// ============================================================================
// Camera initialization
// ============================================================================
bool initCamera() {
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

  // Use PSRAM if available for higher resolution / double-buffering.
  if (psramFound()) {
    config.frame_size = CAMERA_FRAME_SIZE;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = CAMERA_JPEG_QUALITY + 5; // slightly lower quality, save RAM
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Init failed with error 0x%x\n", err);
    return false;
  }

  // Basic sensor tuning for a more stable, well-exposed image.
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != NULL) {
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);
    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_aec2(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
    sensor->set_agc_gain(sensor, 0);
    sensor->set_gainceiling(sensor, (gainceiling_t)0);
  }

  return true;
}

// ============================================================================
// Wi-Fi connect with timeout + automatic reconnect support
// ============================================================================
bool connectWiFi() {
  Serial.printf("[WIFI] Connecting to '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Disable modem sleep for a more stable, low-latency stream
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("\n[WIFI] Connection timed out.");
      return false;
    }
  }

  Serial.println();
  Serial.print("[WIFI] Connected. IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("[WIFI] Stream URL:");
  Serial.print("       http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");

  return true;
}

void checkWiFiAndReconnect() {
  unsigned long now = millis();
  if (now - last_wifi_check < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }
  last_wifi_check = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Connection lost. Attempting to reconnect...");
    WiFi.disconnect();
    connectWiFi();
  }
}

// ============================================================================
// Arduino entry points
// ============================================================================
void setup() {
  // Disable brownout detector: the camera module can cause voltage dips
  // during flash/capture that would otherwise reset the board.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(false);
  delay(1000);

  Serial.println("\n=== ESP32-CAM YOLO Stream Firmware ===");

  if (!initCamera()) {
    Serial.println("[FATAL] Camera initialization failed. Halting.");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("[CAMERA] Initialized successfully.");

  if (!connectWiFi()) {
    Serial.println("[WIFI] Initial connection failed. Will keep retrying in loop().");
  }

  startCameraServer();
  Serial.println("[SETUP] Ready. Waiting for connections...");
}

void loop() {
  checkWiFiAndReconnect();
  delay(200);
}
