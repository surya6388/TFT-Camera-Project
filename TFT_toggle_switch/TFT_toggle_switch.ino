#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <TinyGPS++.h>

// ---- Display config (unchanged) ----
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488  _panel_instance;
  lgfx::Bus_Parallel8  _bus_instance;
  lgfx::Light_PWM      _light_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.port       = 0;
      cfg.freq_write = 16000000;
      cfg.pin_wr     = 35;
      cfg.pin_rd     = -1;
      cfg.pin_rs     = 37;
      cfg.pin_d0     = 1;
      cfg.pin_d1     = 2;
      cfg.pin_d2     = 3;
      cfg.pin_d3     = 4;
      cfg.pin_d4     = 5;
      cfg.pin_d5     = 6;
      cfg.pin_d6     = 7;
      cfg.pin_d7     = 8;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 38;
      cfg.pin_rst          = 39;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 40;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 1;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

// ---- Pins ----
#define BTN_CAPTURE   14
#define BTN_SAVE      15
#define SD_CS         10
#define SD_MOSI       11
#define SD_CLK        12
#define SD_MISO       13
#define GPS_RX        16

// ---- Screen layout ----
#define IMG_Y         0
#define IMG_H         362
#define STATUS_Y      (IMG_H + 2)
#define STATUS_H      26
#define BTN_Y         (STATUS_Y + STATUS_H + 4)
#define BTN_H         80
#define BTN_W         140
#define BTN_CAP_X     10
#define BTN_SAVE_X    170

// ---- AP credentials ----
const char* AP_SSID     = "CAM_AP";
const char* AP_PASSWORD = "12345678";
const char* CAM_URL     = "http://192.168.4.1/capture";

// ---- Globals ----
LGFX tft;
SPIClass sdSPI(HSPI);
bool     sdReady    = false;
bool     frozen     = false;
#define  BUF_SIZE  (60 * 1024)
uint8_t* imgBuf     = nullptr;
size_t   imgLen     = 0;

// GPS
TinyGPSPlus gps;
bool gpsFix = false;
double gpsLat = 0, gpsLon = 0, gpsAlt = 0, gpsSpeed = 0;
String gpsDate = "", gpsTime = "";

// Button struct
struct Button {
  int pin;
  bool lastState;
  unsigned long lastTime;
};
Button btnCap = {BTN_CAPTURE, HIGH, 0};
Button btnSave = {BTN_SAVE, HIGH, 0};

bool isButtonPressed(Button &btn) {
  bool current = digitalRead(btn.pin);
  if (current == LOW && btn.lastState == HIGH && (millis() - btn.lastTime) > 200) {
    btn.lastState = current;
    btn.lastTime = millis();
    return true;
  }
  btn.lastState = current;
  return false;
}

// ---- UI ----
void showStatus(const char* msg, uint32_t color = TFT_WHITE) {
  tft.fillRect(0, STATUS_Y, 320, STATUS_H, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(4, STATUS_Y + 4);
  tft.print(msg);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t color, bool pressed) {
  tft.fillRect(x, y, w, h, pressed ? color : TFT_BLACK);
  tft.drawRect(x, y, w, h, color);
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, color);
  tft.setTextColor(pressed ? TFT_BLACK : color, pressed ? color : TFT_BLACK);
  tft.setTextSize(2);
  int textW = strlen(label) * 12;
  tft.setCursor(x + (w - textW) / 2, y + h / 2 - 8);
  tft.print(label);
}

void drawButtons(bool capPressed = false, bool savePressed = false) {
  drawButton(BTN_CAP_X,  BTN_Y, BTN_W, BTN_H, frozen ? "RESUME" : "CAPTURE", TFT_YELLOW, capPressed);
  drawButton(BTN_SAVE_X, BTN_Y, BTN_W, BTN_H, "SAVE",                        TFT_GREEN,  savePressed);
}

// ---- Fetch JPEG ----
bool fetchFrame() {
  HTTPClient http;
  http.begin(CAM_URL);
  http.setTimeout(3000);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  WiFiClient* stream = http.getStreamPtr();
  int total = http.getSize();
  if (total <= 0 || total > BUF_SIZE) total = BUF_SIZE;
  imgLen = 0;
  uint32_t t = millis();
  while (http.connected() && imgLen < (size_t)total) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = min(avail, (size_t)(BUF_SIZE - imgLen));
      imgLen += stream->readBytes(imgBuf + imgLen, toRead);
    }
    if (millis() - t > 3000) break;
  }
  http.end();
  return imgLen > 100;
}

// ---- Display image ----
void displayFrame() {
  tft.drawJpg(imgBuf, imgLen, 0, IMG_Y, 320, IMG_H);
}

// ---- Update GPS ----
void updateGPS() {
  while (Serial2.available() > 0) {
    gps.encode(Serial2.read());
  }
  gpsFix = gps.location.isValid() && gps.time.isValid();
  if (gpsFix) {
    gpsLat = gps.location.lat();
    gpsLon = gps.location.lng();
    gpsAlt = gps.altitude.meters();
    gpsSpeed = gps.speed.kmph();
    if (gps.date.isValid()) {
      gpsDate = String(gps.date.year()) + "-" + String(gps.date.month()) + "-" + String(gps.date.day());
    } else gpsDate = "----";
    if (gps.time.isValid()) {
      char buf[10];
      sprintf(buf, "%02d-%02d-%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
      gpsTime = String(buf);
    } else gpsTime = "--:--:--";
  }
}

// ---- Sanitise filename (remove invalid characters) ----
String safeFilename(const String& str) {
  String safe = str;
  safe.replace('/', '_');
  safe.replace(':', '-');
  safe.replace(' ', '_');
  return safe;
}

// ---- Save image ----
void saveToSD() {
  if (imgLen == 0) {
    showStatus("Nothing to save!", TFT_RED);
    delay(1500);
    return;
  }
  if (!sdReady) {
    showStatus("SD not ready!", TFT_RED);
    delay(1500);
    return;
  }

  updateGPS();  // get latest

  // Build filename
  char fname[128];
  if (gpsFix) {
    String dateStr = safeFilename(gpsDate);
    String timeStr = safeFilename(gpsTime);
    snprintf(fname, sizeof(fname),
      "/IMG_%s_%s_Lat%.6f_Lon%.6f.jpg",
      dateStr.c_str(), timeStr.c_str(), gpsLat, gpsLon);
  } else {
    String dateStr = safeFilename(gpsDate);
    String timeStr = safeFilename(gpsTime);
    snprintf(fname, sizeof(fname),
      "/IMG_%s_%s_NoGPS.jpg",
      dateStr.c_str(), timeStr.c_str());
  }

  // Ensure filename length is not too long
  if (strlen(fname) > 64) {
    // fallback to shorter name using timestamp
    snprintf(fname, sizeof(fname), "/IMG_%lu.jpg", millis());
  }

  // Try to open file (retry up to 3 times)
  bool success = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    File f = SD.open(fname, FILE_WRITE);
    if (f) {
      f.write(imgBuf, imgLen);
      f.close();
      success = true;
      break;
    }
    delay(100);
  }

  if (!success) {
    showStatus("Write fail!", TFT_RED);
    delay(1500);
    return;
  }

  // Success message
  tft.fillRect(40, 90, 240, 50, TFT_BLACK);
  tft.drawRect(40, 90, 240, 50, TFT_GREEN);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(50, 108);
  tft.printf("Saved: %s", strrchr(fname, '/') + 1);
  delay(1500);
  displayFrame();
}

// ---- Connect to CAM AP ----
void connectToCAM() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Connecting to CAM_AP...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
    tft.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Connected!");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.println(WiFi.localIP());
    delay(1000);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(10, 80);
    tft.println("Failed! Retry...");
    delay(2000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_CAPTURE, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH); // ensure CS is high before init

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Booting...");

  imgBuf = (uint8_t*)malloc(BUF_SIZE);
  if (!imgBuf) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("RAM alloc fail!");
    while (1);
  }

  // SD init with diagnostics
  sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.println("SD init FAIL!");
    tft.setCursor(10, 60);
    tft.println("Check wiring");
    delay(3000);
    // continue but sdReady stays false
  } else {
    sdReady = true;
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.println("SD OK");
    // Check free space
    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();
    tft.setCursor(10, 60);
    tft.printf("Free: %.1f MB", (totalBytes - usedBytes) / 1024.0 / 1024.0);
    delay(1500);
  }

  // GPS
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, -1);

  connectToCAM();

  tft.fillScreen(TFT_BLACK);
  showStatus("CAPTURE to freeze", TFT_YELLOW);
  drawButtons();
}

void loop() {
  updateGPS();

  if (!frozen) {
    if (fetchFrame()) {
      displayFrame();
      char status[60];
      if (gpsFix) {
        snprintf(status, sizeof(status), "Live | %.6f,%.6f", gpsLat, gpsLon);
      } else {
        snprintf(status, sizeof(status), "Live | GPS: No Fix");
      }
      showStatus(status, gpsFix ? TFT_GREEN : TFT_RED);
    } else {
      showStatus("No frame - retrying...", TFT_RED);
      delay(500);
      if (WiFi.status() != WL_CONNECTED) {
        connectToCAM();
      }
    }
  }

  if (isButtonPressed(btnCap)) {
    drawButtons(true, false);
    frozen = !frozen;
    if (frozen) {
      showStatus("Frozen - SAVE to write SD", TFT_CYAN);
    } else {
      char status[60];
      if (gpsFix) {
        snprintf(status, sizeof(status), "Live | %.6f,%.6f", gpsLat, gpsLon);
      } else {
        snprintf(status, sizeof(status), "Live | GPS: No Fix");
      }
      showStatus(status, gpsFix ? TFT_GREEN : TFT_RED);
    }
    delay(150);
    drawButtons();
  }

  if (isButtonPressed(btnSave)) {
    drawButtons(false, true);
    delay(150);
    saveToSD();
    drawButtons();
  }

  delay(10);
}