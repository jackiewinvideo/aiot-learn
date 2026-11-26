#include <Arduino.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "local_config.h"

#define DHT_PIN 22
#define DHT_TYPE DHT11

constexpr unsigned long SENSOR_INTERVAL_MS = 2000;
constexpr unsigned long UPLOAD_INTERVAL_MS = 20000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 30000;

TFT_eSPI tft;
DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastReadAt = 0;
unsigned long lastUploadAt = 0;
unsigned long lastWifiRetryAt = 0;
float latestTemperature = 0.0f;
float latestHumidity = 0.0f;
bool sensorValid = false;

void drawReadings(bool valid, float temperature, float humidity) {
  tft.fillRect(0, 48, 320, 192, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.drawString("Temperature", 160, 70, 2);
  tft.drawString("Humidity", 160, 148, 2);

  if (valid) {
    char value[24];

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(value, sizeof(value), "%.1f C", temperature);
    tft.drawString(value, 160, 105, 4);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(value, sizeof(value), "%.1f %%", humidity);
    tft.drawString(value, 160, 183, 4);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Sensor ready", 160, 225, 2);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("--.- C", 160, 105, 4);
    tft.drawString("--.- %", 160, 183, 4);
    tft.drawString("Check sensor wiring", 160, 225, 2);
  }
}

void readSensor() {
  const float humidity = dht.readHumidity();
  const float temperature = dht.readTemperature();

  if (isnan(temperature) || isnan(humidity)) {
    sensorValid = false;
    Serial.println("[DHT11] Read failed; check GPIO22 wiring.");
    drawReadings(false, 0.0f, 0.0f);
    return;
  }

  sensorValid = true;
  latestTemperature = temperature;
  latestHumidity = humidity;
  Serial.printf("[DHT11] Temperature: %.1f C, humidity: %.1f %%\n",
                temperature, humidity);
  drawReadings(true, temperature, humidity);
}

void uploadReadings() {
  if (!sensorValid) {
    Serial.println("[ThingSpeak] Upload skipped: sensor reading is invalid.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ThingSpeak] Upload skipped: Wi-Fi is not connected.");
    return;
  }
  if (time(nullptr) < 1700000000) {
    Serial.println("[ThingSpeak] Upload skipped: waiting for time synchronization.");
    return;
  }

  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, "https://api.thingspeak.com/update")) {
    Serial.println("[ThingSpeak] Failed to initialize HTTPS request.");
    return;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  char fields[80];
  snprintf(fields, sizeof(fields), "&field1=%.1f&field2=%.1f",
           latestTemperature, latestHumidity);
  String payload = String("api_key=") + CONFIG_THINGSPEAK_WRITE_API_KEY + fields;
  int status = http.POST(payload);
  if (status == HTTP_CODE_OK) {
    String response = http.getString();
    response.trim();
    if (response.toInt() > 0) {
      Serial.println("[ThingSpeak] Upload successful.");
    } else if (response == "0") {
      Serial.println("[ThingSpeak] Upload rejected; check the Write API Key and rate limit.");
    } else {
      Serial.printf("[ThingSpeak] Unexpected response (length: %u).\n",
                    static_cast<unsigned>(response.length()));
    }
  } else {
    Serial.printf("[ThingSpeak] Upload failed, HTTP/client code: %d\n", status);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 48, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("CYD DHT11 Monitor", 160, 24, 4);

  dht.begin();
  Serial.println("[DHT11] Sensor initialized on GPIO22.");

  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    Serial.printf("[Wi-Fi] Disconnected, reason: %u\n",
                  static_cast<unsigned>(info.wifi_sta_disconnected.reason));
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.println("[Wi-Fi] Connecting to configured hotspot.");

  delay(2000);
  readSensor();
  lastReadAt = millis();
}

void loop() {
  if (millis() - lastReadAt >= SENSOR_INTERVAL_MS) {
    lastReadAt = millis();
    readSensor();
  }
  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastWifiRetryAt >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryAt = millis();
    Serial.println("[Wi-Fi] Not connected; retrying.");
    WiFi.reconnect();
  }
  if (millis() - lastUploadAt >= UPLOAD_INTERVAL_MS) {
    lastUploadAt = millis();
    uploadReadings();
  }
}