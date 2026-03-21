#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "secrets.h" // Muss WIFI_SSID, WIFI_PASSWORD und DEVICE_HOSTNAME enthalten

// Pinbelegung für ESP32-CAM / AI Thinker (OV3660 kompatibel)
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

// --- OPTIMIERUNGEN ---
static const framesize_t STREAM_FRAME_SIZE = FRAMESIZE_SVGA; // 640x480 für OV3660         HVGA
static const int STREAM_JPEG_QUALITY = 12;                 // 10-12 ist ideal für VGA       10
static const int TARGET_FPS = 30;                          // Begrenzung schont den Chip   16
// ---------------------

static httpd_handle_t gHttpServer = nullptr;
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

String makeIndexHtml() {
  return R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <title>Birdwatch Live</title>
  <style>
    /* Entfernt Ränder und verhindert Scrollen */
    html, body { 
      margin: 0; 
      padding: 0; 
      width: 100%; 
      height: 100%; 
      overflow: hidden; 
      background: #000; 
      display: flex; 
      align-items: center; 
      justify-content: center; 
    }
    
    /* Maximiert das Bild unter Beibehaltung des Seitenverhältnisses */
    img { 
      width: 100%; 
      height: 100%; 
      object-fit: contain; /* Verhindert Strecken und Beschneiden */
      display: block; 
    }
  </style>
</head>
<body>
  <img src="/stream" alt="Birdwatch Live Stream">
</body>
</html>
)HTML";
}

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  // Stabile 16MHz für OV3660 (weniger Artefakte)
  config.xclk_freq_hz = 19500000; 
  config.pixel_format = PIXFORMAT_JPEG;
  
  if (psramFound()) {
    config.frame_size = STREAM_FRAME_SIZE;
    config.jpeg_quality = STREAM_JPEG_QUALITY; // Etwas höherer Wert (15 statt 12) reduziert Datenfehler
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST; 
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  if (s != nullptr) {
    // --- BILD-OPTIMIERUNG ---
    s->set_vflip(s, 1);       
    s->set_brightness(s, 1);   // Leicht erhöhen (-2 bis 2)
    s->set_contrast(s, 1);     // Kontrast für Federn erhöhen
    s->set_sharpness(s, 1);    // Hardware-Nachschärfung
    
    // --- LICHT-MANAGEMENT (Wichtig!) ---
    s->set_gain_ctrl(s, 1);    // Automatische Verstärkung an
    s->set_exposure_ctrl(s, 1);// Automatische Belichtung an
    s->set_awb_gain(s, 1);     // Weißabgleich-Verstärkung an
    
    // AEC-Modus für Nistkästen (0: Auto, 1: Spot)
    // Spot-Messung hilft, wenn das Zentrum (Vogel) dunkel ist
    s->set_aec2(s, 1);         
    s->set_ae_level(s, 2);     // Belichtung pauschal etwas anheben (0-4)
    
    // Gamma-Korrektur einschalten für bessere Details in dunklen Bereichen
    s->set_dcw(s, 1); 
  }
  return true;
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(WIFI_PS_NONE); // Maximale WLAN-Performance

  Serial.print("Verbindung wird aufgebaut");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nWiFi verbunden.");
}

esp_err_t handleIndex(httpd_req_t* req) {
  String html = makeIndexHtml();
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html.c_str(), html.length());
}

esp_err_t handleStream(httpd_req_t* req) {
  camera_fb_t* fb = nullptr;
  esp_err_t res = ESP_OK;
  char partBuffer[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  const int64_t frame_interval = 1000000 / TARGET_FPS;

  while (true) {
    int64_t now = esp_timer_get_time();
    
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Kamera Frame Fehler");
      continue;
    }

    size_t hlen = snprintf(partBuffer, sizeof(partBuffer), STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, partBuffer, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    if (res != ESP_OK) break; 

    int64_t process_time = esp_timer_get_time() - now;
    if (process_time < frame_interval) {
        delay((frame_interval - process_time) / 1000);
    }
  }
  return res;
}

bool startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_open_sockets = 4; 
  config.stack_size = 16384;

  if (httpd_start(&gHttpServer, &config) == ESP_OK) {
    httpd_uri_t indexUri = { "/", HTTP_GET, handleIndex, NULL };
    httpd_uri_t streamUri = { "/stream", HTTP_GET, handleStream, NULL };
    httpd_register_uri_handler(gHttpServer, &indexUri);
    httpd_register_uri_handler(gHttpServer, &streamUri);
    return true;
  }
  return false;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Brownout deaktivieren
  
  Serial.begin(115200);
  
  if (!initCamera()) {
    Serial.println("Kamera Initialisierung fehlgeschlagen!");
    return;
  }

  connectWifi();

  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  if (startWebServer()) {
    Serial.print("Stream bereit: http://");
    Serial.print(WiFi.localIP());
    Serial.print(" oder http://");
    Serial.print(DEVICE_HOSTNAME);
    Serial.println(".local");
  }
}

void loop() {
  delay(1000);
}