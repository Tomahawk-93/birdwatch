#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SD_MMC.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "secrets.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "secrets.h nicht gefunden – es werden Platzhalter aus secrets.example.h verwendet."
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID ist nicht definiert. Bitte secrets.h anlegen."
#endif

// Pinbelegung fuer ESP32-CAM / AI Thinker-kompatible Module mit Kamera und TF-Slot.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

static WebServer server(80);

static const char* CAPTURE_DIR = "/captures";
static const uint8_t GALLERY_SIZE = 12;
static const uint16_t MAX_STORED_CAPTURES = 48;
static const bool AUTO_CAPTURE_ENABLED = false;
static const unsigned long AUTO_CAPTURE_INTERVAL_MS = 180000UL;
static const framesize_t DEFAULT_FRAME_SIZE = FRAMESIZE_VGA;
static const int DEFAULT_JPEG_QUALITY = 12;

struct BirdStatus {
  String name = "Erkennung deaktiviert";
  float confidence = 0.0f;
  String lastAnalysis = "Noch keine Analyse";
  String state = "Initialisierung";
  bool autoCaptureEnabled = AUTO_CAPTURE_ENABLED;
  String lastSavedImage = "";
};

struct GalleryEntry {
  String name;
  size_t size = 0;
};

static BirdStatus gStatus;
static uint32_t gCaptureCounter = 0;
static unsigned long gLastCaptureAttempt = 0;

String uptimeString() {
  const unsigned long seconds = millis() / 1000UL;
  const unsigned long minutes = seconds / 60UL;
  const unsigned long hours = minutes / 60UL;

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "Uptime %02lu:%02lu:%02lu", hours, minutes % 60UL, seconds % 60UL);
  return String(buffer);
}

void touchAnalysisTimestamp() {
  gStatus.lastAnalysis = uptimeString();
}

void setDetectionStatus(const String& name, float confidence, const String& state) {
  gStatus.name = name;
  gStatus.confidence = confidence;
  gStatus.state = state;
  touchAnalysisTimestamp();
}

void setSystemState(const String& state) {
  gStatus.state = state;
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
  return out;
}

String extractJsonString(const String& json, const char* key) {
  const String needle = String("\"") + key + "\":";
  const int start = json.indexOf(needle);
  if (start < 0) {
    return "";
  }

  int firstQuote = json.indexOf('"', start + needle.length());
  if (firstQuote < 0) {
    return "";
  }

  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return "";
  }

  return json.substring(firstQuote + 1, secondQuote);
}

float extractJsonFloat(const String& json, const char* key) {
  const String needle = String("\"") + key + "\":";
  const int start = json.indexOf(needle);
  if (start < 0) {
    return 0.0f;
  }

  int valueStart = start + needle.length();
  while (valueStart < static_cast<int>(json.length()) && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    ++valueStart;
  }

  int valueEnd = valueStart;
  while (valueEnd < static_cast<int>(json.length())) {
    const char c = json[valueEnd];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
      ++valueEnd;
    } else {
      break;
    }
  }

  return json.substring(valueStart, valueEnd).toFloat();
}

String makeStatusJson() {
  String json = "{";
  json += "\"name\":\"" + jsonEscape(gStatus.name) + "\",";
  json += "\"confidence\":" + String(gStatus.confidence, 2) + ",";
  json += "\"lastAnalysis\":\"" + jsonEscape(gStatus.lastAnalysis) + "\",";
  json += "\"state\":\"" + jsonEscape(gStatus.state) + "\",";
  json += "\"autoCaptureEnabled\":" + String(gStatus.autoCaptureEnabled ? "true" : "false") + ",";
  json += "\"lastSavedImage\":\"" + jsonEscape(gStatus.lastSavedImage) + "\"";
  json += "}";
  return json;
}

String makeIndexHtml() {
  String html = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Birdwatch</title>
  <style>
    :root {
      color-scheme: light dark;
      --bg: #eef3ed;
      --card: rgba(255, 255, 255, 0.9);
      --text: #1f2b1e;
      --muted: #5a6b58;
      --accent: #2d6a4f;
      --accent-strong: #1f513b;
      --border: rgba(45, 106, 79, 0.14);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: Inter, Arial, sans-serif;
      background: linear-gradient(180deg, #f5f8f2 0%, var(--bg) 100%);
      color: var(--text);
    }
    .wrap {
      max-width: 1120px;
      margin: 0 auto;
      padding: 20px 16px 40px;
    }
    .hero {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      align-items: flex-start;
      margin-bottom: 18px;
      flex-wrap: wrap;
    }
    h1 {
      margin: 0;
      font-size: clamp(1.8rem, 4vw, 2.5rem);
    }
    .subtitle {
      margin: 8px 0 0;
      color: var(--muted);
      max-width: 720px;
      line-height: 1.45;
    }
    .host-badge {
      padding: 10px 14px;
      border-radius: 999px;
      background: rgba(45, 106, 79, 0.1);
      border: 1px solid var(--border);
      font-weight: 600;
      white-space: nowrap;
    }
    .grid {
      display: grid;
      grid-template-columns: minmax(0, 2fr) minmax(280px, 1fr);
      gap: 18px;
    }
    .card {
      background: var(--card);
      backdrop-filter: blur(8px);
      border: 1px solid var(--border);
      border-radius: 22px;
      padding: 18px;
      box-shadow: 0 16px 40px rgba(34, 52, 38, 0.08);
    }
    img.stream {
      width: 100%;
      border-radius: 16px;
      background: #122016;
      aspect-ratio: 4 / 3;
      object-fit: cover;
      display: block;
    }
    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-top: 14px;
    }
    button {
      border: none;
      border-radius: 12px;
      padding: 12px 16px;
      font-size: 0.98rem;
      font-weight: 600;
      cursor: pointer;
      background: var(--accent);
      color: white;
      transition: transform .12s ease, background .12s ease;
    }
    button.secondary {
      background: rgba(45, 106, 79, 0.12);
      color: var(--text);
    }
    button:hover { background: var(--accent-strong); transform: translateY(-1px); }
    button.secondary:hover { background: rgba(45, 106, 79, 0.22); }
    .bird-name {
      font-size: 1.5rem;
      font-weight: 800;
      margin: 16px 0 4px;
    }
    .bird-state {
      color: var(--muted);
      line-height: 1.45;
    }
    .meta {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(190px, 1fr));
      gap: 10px;
      margin-top: 16px;
    }
    .meta .pill {
      padding: 12px 14px;
      border-radius: 14px;
      background: rgba(255, 255, 255, 0.72);
      border: 1px solid rgba(0, 0, 0, 0.05);
    }
    .meta strong {
      display: block;
      font-size: 0.82rem;
      text-transform: uppercase;
      letter-spacing: 0.04em;
      color: var(--muted);
      margin-bottom: 5px;
    }
    .hint {
      margin-top: 14px;
      color: var(--muted);
      font-size: 0.95rem;
      line-height: 1.45;
    }
    .gallery-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
      margin-top: 14px;
    }
    .gallery-item {
      display: block;
      text-decoration: none;
      color: inherit;
      border-radius: 16px;
      overflow: hidden;
      background: rgba(255, 255, 255, 0.7);
      border: 1px solid rgba(0, 0, 0, 0.05);
    }
    .gallery-item img {
      width: 100%;
      aspect-ratio: 1 / 1;
      object-fit: cover;
      display: block;
      background: #d7ded4;
    }
    .gallery-item .caption {
      padding: 8px 10px 10px;
      font-size: 0.86rem;
      color: var(--muted);
    }
    .empty {
      color: var(--muted);
      margin-top: 10px;
      font-size: 0.95rem;
    }
    @media (max-width: 900px) {
      .grid { grid-template-columns: 1fr; }
      .gallery-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }
    }
    @media (max-width: 640px) {
      .gallery-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <div>
        <h1>Birdwatch am Futterhaus</h1>
        <p class="subtitle">Lokaler Live-Stream mit manuellen Schnappschüssen, SD-Galerie und optionaler späterer Vogelerkennung. Die Oberfläche bleibt bewusst minimal und funktioniert komplett im Heimnetz.</p>
      </div>
      <div class="host-badge">)HTML";

  html += DEVICE_HOSTNAME;
  html += R"HTML(.local</div>
    </div>

    <div class="grid">
      <section class="card">
        <img class="stream" src="/stream" alt="Livebild vom Vogelhaus">
        <div class="bird-name" id="bird-name">Lade Status …</div>
        <div class="bird-state" id="bird-state">Status wird geladen …</div>

        <div class="meta">
          <div class="pill"><strong>Vertrauen</strong><span id="bird-confidence">–</span></div>
          <div class="pill"><strong>Letzte Analyse</strong><span id="bird-time">–</span></div>
          <div class="pill"><strong>Auto-Speicherung</strong><span id="bird-auto">Deaktiviert</span></div>
          <div class="pill"><strong>Letztes Bild</strong><span id="bird-image">–</span></div>
        </div>

        <div class="actions">
          <button onclick="captureNow()">Schnappschuss auslösen</button>
          <button class="secondary" onclick="refreshGallery()">Galerie aktualisieren</button>
        </div>

        <div class="hint">Hinweis: Ohne externen Klassifikationsdienst bleibt die Vogelart-Erkennung deaktiviert. Der Live-Stream und die manuell gespeicherten Bilder funktionieren trotzdem vollständig lokal.</div>
      </section>

      <aside class="card">
        <h2 style="margin:0;">Letzte Bilder</h2>
        <p class="subtitle" style="margin-top:8px;">Es werden nur die neuesten Aufnahmen gehalten, damit die SD-Karte nicht mit zu vielen fast identischen Bildern vollläuft.</p>
        <div id="gallery-empty" class="empty">Noch keine Bilder gespeichert.</div>
        <div id="gallery" class="gallery-grid"></div>
      </aside>
    </div>
  </div>

  <script>
    async function updateStatus() {
      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        const data = await response.json();
        document.getElementById('bird-name').textContent = data.name || 'Erkennung deaktiviert';
        document.getElementById('bird-state').textContent = data.state || 'Unbekannter Zustand';
        document.getElementById('bird-confidence').textContent = typeof data.confidence === 'number'
          ? Math.round(data.confidence * 100) + ' %'
          : '–';
        document.getElementById('bird-time').textContent = data.lastAnalysis || '–';
        document.getElementById('bird-auto').textContent = data.autoCaptureEnabled ? 'Aktiv' : 'Deaktiviert';
        document.getElementById('bird-image').textContent = data.lastSavedImage || '–';
      } catch (error) {
        document.getElementById('bird-state').textContent = 'Status konnte nicht geladen werden';
      }
    }

    async function refreshGallery() {
      const gallery = document.getElementById('gallery');
      const empty = document.getElementById('gallery-empty');
      try {
        const response = await fetch('/api/gallery', { cache: 'no-store' });
        const data = await response.json();
        gallery.innerHTML = '';
        if (!Array.isArray(data.items) || data.items.length === 0) {
          empty.style.display = 'block';
          return;
        }
        empty.style.display = 'none';
        for (const item of data.items) {
          const link = document.createElement('a');
          link.className = 'gallery-item';
          link.href = item.url;
          link.target = '_blank';
          link.rel = 'noopener';
          link.innerHTML = `<img src="${item.url}" alt="${item.name}"><div class="caption">${item.name}</div>`;
          gallery.appendChild(link);
        }
      } catch (error) {
        empty.textContent = 'Galerie konnte nicht geladen werden.';
        empty.style.display = 'block';
      }
    }

    async function captureNow() {
      const response = await fetch('/capture', { method: 'POST' });
      if (response.ok) {
        await updateStatus();
        await refreshGallery();
      }
    }

    updateStatus();
    refreshGallery();
    setInterval(updateStatus, 5000);
  </script>
</body>
</html>
)HTML";

  return html;
}

bool ensureCaptureDir() {
  if (SD_MMC.exists(CAPTURE_DIR)) {
    return true;
  }
  return SD_MMC.mkdir(CAPTURE_DIR);
}

bool isValidCaptureFileName(const String& name) {
  return name.startsWith("capture-") && name.endsWith(".jpg") && name.indexOf("..") < 0 && name.indexOf('/') < 0;
}

uint32_t parseCaptureNumber(const String& name) {
  if (!isValidCaptureFileName(name)) {
    return 0;
  }

  const int start = 8;
  const int end = name.lastIndexOf('.');
  return name.substring(start, end).toInt();
}

void updateCaptureCounterFromSd() {
  if (!ensureCaptureDir()) {
    return;
  }

  File dir = SD_MMC.open(CAPTURE_DIR);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const String fullName = String(entry.name());
      const int slash = fullName.lastIndexOf('/');
      const String shortName = slash >= 0 ? fullName.substring(slash + 1) : fullName;
      const uint32_t number = parseCaptureNumber(shortName);
      if (number > gCaptureCounter) {
        gCaptureCounter = number;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
}

String nextCapturePath() {
  ++gCaptureCounter;
  char fileName[40];
  snprintf(fileName, sizeof(fileName), "%s/capture-%06lu.jpg", CAPTURE_DIR, static_cast<unsigned long>(gCaptureCounter));
  return String(fileName);
}

void insertGalleryEntry(GalleryEntry* entries, const String& name, size_t size) {
  const String fullPath = String(CAPTURE_DIR) + "/" + name;
  for (uint8_t i = 0; i < GALLERY_SIZE; ++i) {
    if (entries[i].name.length() == 0 || fullPath > (String(CAPTURE_DIR) + "/" + entries[i].name)) {
      for (int j = GALLERY_SIZE - 1; j > i; --j) {
        entries[j] = entries[j - 1];
      }
      entries[i].name = name;
      entries[i].size = size;
      return;
    }
  }
}

void pruneOldCaptures() {
  if (!ensureCaptureDir()) {
    return;
  }

  uint16_t count = 0;
  while (count > MAX_STORED_CAPTURES || count == 0) {
    File dir = SD_MMC.open(CAPTURE_DIR);
    if (!dir || !dir.isDirectory()) {
      return;
    }

    String oldestName = "";
    uint32_t oldestNumber = UINT32_MAX;
    count = 0;

    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        const String fullName = String(entry.name());
        const int slash = fullName.lastIndexOf('/');
        const String shortName = slash >= 0 ? fullName.substring(slash + 1) : fullName;
        const uint32_t number = parseCaptureNumber(shortName);
        if (number > 0) {
          ++count;
          if (number < oldestNumber) {
            oldestNumber = number;
            oldestName = shortName;
          }
        }
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();

    if (count <= MAX_STORED_CAPTURES || oldestName.length() == 0) {
      return;
    }

    SD_MMC.remove(String(CAPTURE_DIR) + "/" + oldestName);
  }
}

String saveFrameToSd(camera_fb_t* fb) {
  if (!fb || !fb->buf || fb->len == 0) {
    return "";
  }

  if (!ensureCaptureDir()) {
    return "";
  }

  const String path = nextCapturePath();
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return "";
  }

  const size_t written = file.write(fb->buf, fb->len);
  file.close();
  if (written != fb->len) {
    SD_MMC.remove(path);
    return "";
  }

  pruneOldCaptures();
  return path;
}

bool classifyFrame(camera_fb_t* fb, const String& fallbackState) {
  if (!fb || strlen(CLASSIFIER_ENDPOINT) == 0) {
    setSystemState(fallbackState);
    return false;
  }

  HTTPClient http;
  http.begin(CLASSIFIER_ENDPOINT);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-Device-Name", DEVICE_HOSTNAME);
  if (strlen(CLASSIFIER_API_KEY) > 0) {
    http.addHeader("Authorization", String("Bearer ") + CLASSIFIER_API_KEY);
  }

  const int code = http.POST(fb->buf, fb->len);
  if (code <= 0) {
    setDetectionStatus("Keine Erkennung", 0.0f, fallbackState + "; Klassifikationsdienst nicht erreichbar");
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    setDetectionStatus("Keine Erkennung", 0.0f, fallbackState + "; Fehler " + String(code));
    return false;
  }

  const String name = extractJsonString(payload, "name");
  const float confidence = extractJsonFloat(payload, "confidence");
  if (name.length() == 0) {
    setDetectionStatus("Keine Erkennung", 0.0f, fallbackState + "; Antwort ohne Vogelname");
    return false;
  }

  setDetectionStatus(name, confidence, fallbackState + "; Vogel erfolgreich erkannt");
  return true;
}

void captureAndAnalyze(bool manualCapture) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    setSystemState("Kameraaufnahme fehlgeschlagen");
    return;
  }

  const String path = saveFrameToSd(fb);
  if (path.length() == 0) {
    setSystemState("SD-Speicherung fehlgeschlagen");
    esp_camera_fb_return(fb);
    return;
  }

  const int slash = path.lastIndexOf('/');
  gStatus.lastSavedImage = slash >= 0 ? path.substring(slash + 1) : path;
  const String state = String(manualCapture ? "Manueller Schnappschuss gespeichert" : "Automatischer Schnappschuss gespeichert") + ": " + gStatus.lastSavedImage;

  if (!classifyFrame(fb, state)) {
    touchAnalysisTimestamp();
  }

  esp_camera_fb_return(fb);
}

String makeGalleryJson() {
  GalleryEntry entries[GALLERY_SIZE];
  if (ensureCaptureDir()) {
    File dir = SD_MMC.open(CAPTURE_DIR);
    if (dir && dir.isDirectory()) {
      File entry = dir.openNextFile();
      while (entry) {
        if (!entry.isDirectory()) {
          const String fullName = String(entry.name());
          const int slash = fullName.lastIndexOf('/');
          const String shortName = slash >= 0 ? fullName.substring(slash + 1) : fullName;
          if (isValidCaptureFileName(shortName)) {
            insertGalleryEntry(entries, shortName, entry.size());
          }
        }
        entry.close();
        entry = dir.openNextFile();
      }
      dir.close();
    }
  }

  String json = "{\"items\":[";
  bool first = true;
  for (uint8_t i = 0; i < GALLERY_SIZE; ++i) {
    if (entries[i].name.length() == 0) {
      continue;
    }
    if (!first) {
      json += ",";
    }
    first = false;
    json += "{";
    json += "\"name\":\"" + jsonEscape(entries[i].name) + "\",";
    json += "\"size\":" + String(entries[i].size) + ",";
    json += "\"url\":\"/capture-file?name=" + jsonEscape(entries[i].name) + "\"";
    json += "}";
  }
  json += "]}";
  return json;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", makeIndexHtml());
}

void handleStatus() {
  server.send(200, "application/json", makeStatusJson());
}

void handleGallery() {
  server.send(200, "application/json", makeGalleryJson());
}

void handleCapture() {
  captureAndAnalyze(true);
  server.send(200, "application/json", makeStatusJson());
}

void handleCaptureFile() {
  const String name = server.arg("name");
  if (!isValidCaptureFileName(name)) {
    server.send(400, "text/plain; charset=utf-8", "Ungültiger Dateiname");
    return;
  }

  const String path = String(CAPTURE_DIR) + "/" + name;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    server.send(404, "text/plain; charset=utf-8", "Bild nicht gefunden");
    return;
  }

  server.streamFile(file, "image/jpeg");
  file.close();
}

void handleNotFound() {
  server.send(404, "text/plain; charset=utf-8", "Nicht gefunden");
}

void handleJpgStream() {
  WiFiClient client = server.client();
  client.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
      "Cache-Control: no-store\r\n"
      "Connection: close\r\n\r\n");

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      delay(50);
      continue;
    }

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);

    if (!client.connected()) {
      break;
    }

    delay(70);
  }
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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? DEFAULT_FRAME_SIZE : FRAMESIZE_QVGA;
  config.jpeg_quality = DEFAULT_JPEG_QUALITY;
  config.fb_count = psramFound() ? 2 : 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -1);
    sensor->set_framesize(sensor, config.frame_size);
  }

  return true;
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("WLAN-Verbindung wird aufgebaut");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
}

void setupMdns() {
  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/stream", HTTP_GET, []() { handleJpgStream(); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/gallery", HTTP_GET, handleGallery);
  server.on("/capture", HTTP_POST, handleCapture);
  server.on("/capture-file", HTTP_GET, handleCaptureFile);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);

  gStatus.state = "System startet";
  gStatus.autoCaptureEnabled = AUTO_CAPTURE_ENABLED;

  if (!initCamera()) {
    setDetectionStatus("Fehler", 0.0f, "Kamera konnte nicht initialisiert werden");
    Serial.println("Kamera-Initialisierung fehlgeschlagen");
    return;
  }

  if (!SD_MMC.begin("/sdcard", true)) {
    setSystemState("SD-Karte nicht verfügbar");
    Serial.println("SD_MMC konnte nicht initialisiert werden");
  } else {
    ensureCaptureDir();
    updateCaptureCounterFromSd();
    pruneOldCaptures();
    Serial.println("SD-Karte bereit");
  }

  connectWifi();
  setupMdns();
  setupWebServer();

  if (strlen(CLASSIFIER_ENDPOINT) == 0) {
    setDetectionStatus("Erkennung deaktiviert", 0.0f, "Live-Stream aktiv");
  } else {
    setDetectionStatus("Bereit", 0.0f, "Live-Stream aktiv, Klassifikationsdienst konfiguriert");
  }

  Serial.print("Aufruf ueber IP: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Aufruf ueber mDNS: http://");
  Serial.print(DEVICE_HOSTNAME);
  Serial.println(".local");
}

void loop() {
  server.handleClient();
  MDNS.update();

  if (!AUTO_CAPTURE_ENABLED) {
    return;
  }

  const unsigned long now = millis();
  if (now - gLastCaptureAttempt >= AUTO_CAPTURE_INTERVAL_MS) {
    gLastCaptureAttempt = now;
    captureAndAnalyze(false);
  }
}
