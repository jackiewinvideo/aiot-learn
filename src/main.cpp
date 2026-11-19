// ESP32-S3 內建 WS2812B LED 彩色變化
// 開發板：ESP32-S3
// LED 腳位：GPIO 48（內建 RGB LED）
// 使用庫：Adafruit NeoPixel（原生支援 ESP32-S3 新版 IDF RMT API）

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN     48    // ESP32-S3 內建 WS2812B 腳位
#define NUM_LEDS    1     // 僅一顆內建 LED
#define BRIGHTNESS  80    // 亮度（0~255，建議不超過 100 避免過亮）

// 初始化 NeoPixel 物件（1 顆, Pin 48, GRB 色彩順序 + 800KHz 訊號）
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t hue = 0;   // 當前色相 0~255

// 將 HSV 色相值轉換為 RGB（模擬 FastLED 的 CHSV）
// h: 0~255 色相, s: 飽和度 0~255, v: 明度 0~255
uint32_t hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;
  uint8_t region = h / 43;                     // 分成 6 個區域（0~5）
  uint8_t remainder = (h - (region * 43)) * 6; // 區域內的進度 0~255

  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return strip.Color(r, g, b);
}

void setup() {
  Serial.begin(115200);

  strip.begin();              // 初始化 NeoPixel 驅動
  strip.setBrightness(BRIGHTNESS); // 設定全域亮度
  strip.clear();              // 清除 LED（確保初始為熄滅）
  strip.show();

  // 開機閃爍白色一次，確認 LED 正常運作
  strip.setPixelColor(0, strip.Color(255, 255, 255));
  strip.show();
  delay(300);
  strip.clear();
  strip.show();
  delay(200);

  Serial.println("=== ESP32-S3 NeoPixel Rainbow Ready ===");
}

void loop() {
  // 利用 HSV 色彩空間產生彩虹漸變效果
  uint32_t color = hsvToRgb(hue, 255, 255);
  strip.setPixelColor(0, color);  // 設定第 0 顆 LED 的顏色
  strip.show();                   // 更新 LED 顯示

  // 解析 RGB 並印出
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8)  & 0xFF;
  uint8_t b = (color)       & 0xFF;
  Serial.printf("Hue: %3d | R:%3d G:%3d B:%3d\n", hue, r, g, b);

  hue++;          // 色相 +1（255 後自動歸零，形成彩虹循環）
  delay(20);      // 每 20ms 換色，完整彩虹循環約 5 秒
}
