/**
 * @file 20261124-9彩色點陣圖.cpp
 * @brief ESP32 CYD (ESP32-2432S028R) 100x100 彩色點陣圖 (米奇 Mickey Mouse) 顯示程式
 * 
 * 核心特色：
 * 1. 100x100 RGB565 點陣圖（PROGMEM 儲存於 Flash，不佔 SRAM）
 * 2. 智慧邊緣泛洪填色背景處理（完美融合 TFT_BLACK 黑色畫面）
 * 3. 解決 CYD IPS 面板反相問題 (tft.invertDisplay(true))
 * 4. 解決位元組順序問題 (tft.setSwapBytes(true))
 */

#include "graphics.h"
#include "SPI.h"
#include "TFT_eSPI.h"

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[SYSTEM] ESP32 CYD - 100x100 Color Bitmap Display");

  // 設定 LED 背光顯示亮度  
  pinMode(TFT_LED, OUTPUT);
  analogWrite(TFT_LED, TFT_LED_BRIGHTNESS);

  // 初始化螢幕
  tft.init();
  tft.setRotation(1); // 橫向 320x240

  // ========================================================
  // 【關鍵修正 1：螢幕反相修正 (負片效果)】
  // CYD 許多批次為 IPS 面板，預設會呈現負片反相（黑變白、紅變青）
  // 呼叫 invertDisplay(true) 可立即還原原圖真彩！
  // ========================================================
  tft.invertDisplay(true);

  // 初次清屏
  tft.fillScreen(TFT_BLACK);

  // 位元組順序校正
  tft.setSwapBytes(true);

  // 在螢幕中央偏左位置 (座標 50, 50) 顯示 100x100 米奇點陣圖
  tft.pushImage(50, 50, MICKEY_WIDTH, MICKEY_HEIGHT, bitmap_mickey);

  // 顯示文字提示
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Mickey Mouse", 165, 80);
  tft.setTextSize(1);
  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.drawString("100x100 RGB565 Bitmap", 165, 110);
  tft.drawString("CYD TFT_eSPI Ready", 165, 130);
}

void loop(void) {
  // 靜態展示維持畫面
  delay(1000);
}
