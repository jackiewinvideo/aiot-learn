#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

const char* WIFI_SSID = "802.1x_MCU";
const char* WIFI_USERNAME = "mcugust";
const char* WIFI_PASSWORD = "mcu18226";

const char* TZ_INFO = "CST-8";
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
const unsigned long NTP_SYNC_INTERVAL_MS = 3600000;

TFT_eSPI tft = TFT_eSPI();

bool wifiWasConnected = false;
bool wifiConnecting = false;
unsigned long lastWifiRetry = 0;
unsigned long lastNtpSync = 0;
int lastDisplayedMinute = -1;
int lastDisplayedSecond = -1;
char previousDigits[4] = {'\0', '\0', '\0', '\0'};

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiWasConnected = true;
    wifiConnecting = false;
    return;
  }

  if (wifiConnecting) {
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(
    WIFI_SSID,
    WPA2_AUTH_PEAP,
    WIFI_USERNAME,
    WIFI_USERNAME,
    WIFI_PASSWORD
  );
  wifiConnecting = true;
  Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
  lastWifiRetry = millis();
}

void retryWifiConnection() {
  WiFi.disconnect();
  wifiConnecting = false;
  delay(500);
  connectWifi();
}

bool synchronizeRtc() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTP] WiFi not connected");
    return false;
  }

  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 15000)) {
    Serial.println("[NTP] Synchronization failed");
    return false;
  }

  Serial.printf("[RTC] Adjusted: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeInfo.tm_year + 1900,
                timeInfo.tm_mon + 1,
                timeInfo.tm_mday,
                timeInfo.tm_hour,
                timeInfo.tm_min,
                timeInfo.tm_sec);
  lastNtpSync = millis();
  return true;
}

void drawFlipCard(int x, int y, int w, int h, const char* value, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 10, TFT_WHITE);
  tft.drawRoundRect(x, y, w, h, 10, TFT_BLACK);
  tft.drawFastHLine(x + 8, y + h / 2, w - 16, TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(1);
  tft.drawString(value, x + w / 2, y + h / 2 - 2, 8);

  tft.fillRect(x + w - 12, y + h / 2 - 5, 12, 10, TFT_WHITE);
  tft.drawCircle(x + w - 12, y + h / 2, 3, TFT_BLACK);

  (void)color;
}

void animateFlipCard(int x, int y, int w, int h, const char* newValue) {
  tft.fillRoundRect(x, y, w, h, 10, TFT_LIGHTGREY);
  tft.drawRoundRect(x, y, w, h, 10, TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.drawString(newValue, x + w / 2, y + h / 2 - 2, 8);

  delay(90);
  tft.drawFastHLine(x + 8, y + h / 2, w - 16, TFT_BLACK);
  delay(90);
  tft.fillRoundRect(x, y, w, h, 10, TFT_WHITE);
  tft.drawRoundRect(x, y, w, h, 10, TFT_BLACK);
  tft.drawFastHLine(x + 8, y + h / 2, w - 16, TFT_BLACK);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(newValue, x + w / 2, y + h / 2 - 2, 8);
}

void drawStatus(const struct tm& timeInfo) {
  char dateText[24];
  strftime(dateText, sizeof(dateText), "%Y-%m-%d  %A", &timeInfo);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(1);
  tft.drawString(dateText, 160, 18, 2);

  tft.drawFastHLine(10, 48, 300, TFT_BLACK);

  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString("WiFi OK  NTP Sync", 160, 218, 2);
  } else {
    tft.setTextColor(TFT_RED, TFT_WHITE);
    tft.drawString("RTC Mode  WiFi Offline", 160, 218, 2);
  }
}

void drawTime(bool animateMinuteChange) {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 0)) {
    tft.fillScreen(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString("Waiting for RTC...", 160, 120, 4);
    return;
  }

  int currentMinute = timeInfo.tm_hour * 60 + timeInfo.tm_min;
  int currentSecond = timeInfo.tm_sec;

  if (lastDisplayedMinute != currentMinute) {
    char timeDigits[5];
    snprintf(timeDigits, sizeof(timeDigits), "%02d%02d",
             timeInfo.tm_hour, timeInfo.tm_min);

    bool firstDraw = (lastDisplayedMinute < 0);
    if (firstDraw) {
      tft.fillScreen(TFT_WHITE);
    }

    const int cardWidth = 66;
    const int cardHeight = 116;
    const int cardY = 72;
    const int cardX[4] = {8, 86, 168, 246};

    for (int i = 0; i < 4; i++) {
      char digitText[2] = {timeDigits[i], '\0'};

      if (firstDraw) {
        drawFlipCard(cardX[i], cardY, cardWidth, cardHeight,
                     digitText, TFT_CYAN);
      } else if (previousDigits[i] != timeDigits[i]) {
        animateFlipCard(cardX[i], cardY, cardWidth, cardHeight,
                        digitText);
      }

      previousDigits[i] = timeDigits[i];
    }

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setTextSize(1);
    tft.drawString("H", 119, 52, 2);
    tft.drawString("M", 279, 52, 2);

    if (firstDraw || !animateMinuteChange) {
      drawStatus(timeInfo);
    }
    lastDisplayedMinute = currentMinute;
  }

  if (lastDisplayedSecond != currentSecond) {
    char secondText[3];
    snprintf(secondText, sizeof(secondText), "%02d", timeInfo.tm_sec);
    tft.fillRoundRect(265, 198, 45, 36, 6, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setTextSize(1);
    tft.drawString(secondText, 287, 216, 4);
    lastDisplayedSecond = currentSecond;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32 CYD NTP Flip Clock ===");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_WHITE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString("Connecting WiFi...", 160, 120, 4);

  connectWifi();
  for (int retry = 0; retry < 20 && WiFi.status() != WL_CONNECTED; retry++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    synchronizeRtc();
  } else {
    Serial.println("[WiFi] Connection failed; RTC mode will be kept");
  }

  drawTime(false);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      synchronizeRtc();
    }
  } else {
    wifiWasConnected = false;

    if (wifiConnecting && millis() - lastWifiRetry >= 15000) {
      Serial.println("[WiFi] Connection timeout; resetting WiFi");
      retryWifiConnection();
    } else if (!wifiConnecting &&
               millis() - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
      connectWifi();
    }
  }

  if (WiFi.status() == WL_CONNECTED &&
      (millis() - lastNtpSync >= NTP_SYNC_INTERVAL_MS)) {
    synchronizeRtc();
  }

  drawTime(true);
  delay(250);
}
