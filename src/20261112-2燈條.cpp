#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// 定義硬體腳位與燈珠數
#define LED_PIN       7    // WS2812 訊號線 (DIN) 接在 Pin 7
#define LED_COUNT     8    // 8 顆燈珠 (索引 0 ~ 7)
#define BRIGHTNESS   80    // 全域亮度降為 80 (範圍 0 ~ 255)

// 循環時間與步進值設定
const int CYCLE_TIME_MS = 2000;                        // 每個循環 2 秒 (2000ms)
const int STEP_DELAY_MS = CYCLE_TIME_MS / LED_COUNT;   // 每顆燈停留 250ms (2000 / 8)
const int COLOR_STEP    = 50;                          // 每次遞增值 50

// 建立 NeoPixel 物件
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// RGB 三組顏色數值，從 (0, 0, 0) 開始
int rVal = 0;
int gVal = 0;
int bVal = 0;

// 輪轉指標：0 代表下一輪加 R、1 代表加 G、2 代表加 B
int channelIndex = 0;

void setup() {
  Serial.begin(9600);
  strip.begin();
  strip.setBrightness(BRIGHTNESS); // 設定全域亮度為 80
  strip.show();                   // 初始化清空全滅

  Serial.println("==================================================");
  Serial.println("  WS2812 系統更新：");
  Serial.println("  * 全域亮度：降為 80");
  Serial.println("  * 遞增步進值：50");
  Serial.println("  * 循環時間：2 秒 (每顆 250ms)");
  Serial.println("==================================================");
}

void loop() {
  // 1. 序列埠印出目前循環的 RGB 數值與亮度
  Serial.print(">>> 執行 2 秒循環，顏色 RGB: (");
  Serial.print(rVal);
  Serial.print(", ");
  Serial.print(gVal);
  Serial.print(", ");
  Serial.print(bVal);
  Serial.println(") | 亮度: 80");

  // 2. 每個循環開始前先清空燈條
  strip.clear();
  strip.show();

  // 3. LED 從 0 依序亮到 7，每顆間隔 250ms（8 顆合計 2000ms = 2 秒）
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(rVal, gVal, bVal));
    strip.show();
    delay(STEP_DELAY_MS);
  }

  // 4. 循環結束後，三組顏色依序遞增 50
  if (channelIndex == 0) {
    rVal += COLOR_STEP;  // 紅色加 50
    channelIndex = 1;    // 下一輪換綠色
  } else if (channelIndex == 1) {
    gVal += COLOR_STEP;  // 綠色加 50
    channelIndex = 2;    // 下一輪換藍色
  } else {
    bVal += COLOR_STEP;  // 藍色加 50
    channelIndex = 0;    // 下一輪換回紅色
  }

  // 5. 當超過 250 時重置回 (0, 0, 0) 重新循環
  if (rVal > 250) {
    rVal = 0;
    gVal = 0;
    bVal = 0;
    channelIndex = 0;
    Serial.println("  [數值達上限 250，重置回 (0,0,0) 重新開始]");
  }
}
