#include <Arduino.h>
#include <TFT_eSPI.h>

// 建立 TFT 物件
TFT_eSPI tft = TFT_eSPI();

// CYD 螢幕背光腳位（platformio.ini 定義為 TFT_BL = 21）
#ifndef TFT_BL
#define TFT_BL 21
#endif

void setup() {
  // 初始化序列埠通訊
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== CYD (ESP32-2432S028R) TFT Test ===");

  // 初始化螢幕
  tft.init();
  tft.setRotation(1); // 橫向顯示 (0: 直向, 1: 橫向 320x240, 2: 反向直向, 3: 反向橫向)

  // 開啟螢幕背光
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // 初次清屏並顯示歡迎畫面
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("CYD Screen Ready!", 60, 100);

  Serial.println("TFT 初始化完成，進入主迴圈...");
  delay(1500);
}

void loop() {
  static uint32_t count = 0;
  count++;

  // 產生隨機 RGB 顏色
  uint8_t r = random(256);
  uint8_t g = random(256);
  uint8_t b = random(256);
  uint16_t color = tft.color565(r, g, b);

  // 填滿隨機背景色
  tft.fillScreen(color);

  // 顯示文字資訊
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("CYD Display Test", 60, 60);

  char buf[32];
  snprintf(buf, sizeof(buf), "R:%d G:%d B:%d", r, g, b);
  tft.drawString(buf, 60, 100);

  snprintf(buf, sizeof(buf), "Count: %lu", (unsigned long)count);
  tft.drawString(buf, 60, 140);

  Serial.printf("[CYD] Loop #%lu: R=%d, G=%d, B=%d\n", (unsigned long)count, r, g, b);

  delay(1000);  // 每次顯示暫停一秒
}
