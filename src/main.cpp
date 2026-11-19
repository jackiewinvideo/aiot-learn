// ============================================================
// ESP32-S3 內建 WS2812B LED 彩虹色變化
// 開發板：ESP32-S3
// LED 腳位：GPIO 48（內建 RGB LED）
// 使用庫：Adafruit NeoPixel（原生支援 ESP32-S3 新版 IDF RMT API）
// ============================================================

#include <Arduino.h>              // 引入 Arduino 核心標頭檔，提供 setup/loop/Serial 等基礎函式
#include <Adafruit_NeoPixel.h>    // 引入 Adafruit NeoPixel 庫，用於控制 WS2812B LED

#define LED_PIN     48            // 定義 WS2812B 資料腳位為 GPIO 48（ESP32-S3 板子內建 LED）
#define NUM_LEDS    1             // 定義 LED 顆數為 1（僅一顆內建 LED）
#define BRIGHTNESS  80            // 定義全域亮度（0~255，80 約為 31%，避免過亮刺眼）

// 建立 NeoPixel 控制物件：
//   NUM_LEDS  = LED 顆數（1 顆）
//   LED_PIN   = 資料腳位（48）
//   NEO_GRB   = WS2812B 色彩順序為 G（綠）R（紅）B（藍）
//   NEO_KHZ800 = 訊號頻率 800KHz（WS2812B 標準傳輸速率）
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t hue = 0;   // 宣告色相變數（0~255 對應彩虹全色域），每幀遞增產生彩虹動畫

// ============================================================
// 自製 HSV 轉 RGB 函式（模擬 FastLED 的 CHSV 色彩轉換功能）
// 參數：
//   h = 色相（Hue）      0~255，0=紅, 85=綠, 170=藍
//   s = 飽和度（Sat）    0~255，255 最鮮艷，0 為白色
//   v = 明度（Value）    0~255，255 最亮，0 為黑色（關燈）
// 回傳：uint32_t 格式的 RGB 色彩值（供 NeoPixel 使用）
// ============================================================
uint32_t hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;                             // 宣告三個輸出色彩變數

  uint8_t region    = h / 43;                  // 將色相 0~255 分成 6 個區域（0~5），每區對應彩虹中的一段漸變
  uint8_t remainder = (h - (region * 43)) * 6; // 計算目前在該區域內的進度（0~255）

  uint8_t p = (v * (255 - s)) >> 8;            // p = 最低亮度分量（飽和度越高，p 越接近 0）
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;        // q = 下降漸變分量
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8; // t = 上升漸變分量

  switch (region) {                            // 根據色相區域決定 RGB 三個分量的對應關係
    case 0: r = v; g = t; b = p; break;        // 區域 0：紅→黃過渡（紅最強，綠上升，藍最低）
    case 1: r = q; g = v; b = p; break;        // 區域 1：黃→綠過渡（綠最強，紅下降，藍最低）
    case 2: r = p; g = v; b = t; break;        // 區域 2：綠→青過渡（綠最強，藍上升，紅最低）
    case 3: r = p; g = q; b = v; break;        // 區域 3：青→藍過渡（藍最強，綠下降，紅最低）
    case 4: r = t; g = p; b = v; break;        // 區域 4：藍→紫過渡（藍最強，紅上升，綠最低）
    default: r = v; g = p; b = q; break;       // 區域 5：紫→紅過渡（紅最強，藍下降，綠最低）
  }

  return strip.Color(r, g, b);                 // 將計算出的 RGB 組合成 NeoPixel 格式並回傳
}

// ============================================================
// setup() — Arduino 開機初始化函式，僅執行一次
// ============================================================
void setup() {
  Serial.begin(115200);           // 啟動序列埠，設定鮑率為 115200 bps（與 platformio.ini 一致）

  strip.begin();                  // 初始化 NeoPixel 驅動（設定 GPIO 並啟動 RMT 傳輸）
  strip.setBrightness(BRIGHTNESS); // 設定全域亮度（此處為 80，約 31%）
  strip.clear();                  // 清除所有 LED 顏色記憶體（初始確保全部熄滅）
  strip.show();                   // 將清除結果推送至硬體 LED（實際熄滅 LED）

  // --- 開機閃爍白光，確認 LED 硬體正常 ---
  strip.setPixelColor(0, strip.Color(255, 255, 255)); // 設定第 0 顆 LED 為純白色（R=255, G=255, B=255）
  strip.show();                   // 推送更新，讓 LED 實際點亮白色
  delay(300);                     // 保持白色點亮 300 毫秒（0.3 秒）讓使用者確認

  strip.clear();                  // 再次清除 LED 顏色
  strip.show();                   // 推送清除，讓 LED 熄滅
  delay(200);                     // 熄滅後靜止 200 毫秒，完成開機自我診斷

  Serial.println("=== ESP32-S3 NeoPixel Rainbow Ready ==="); // 在 Serial Monitor 印出就緒訊息
}

// ============================================================
// loop() — 主循環函式，開機後無限重複執行
// ============================================================
void loop() {
  uint32_t color = hsvToRgb(hue, 255, 255); // 以當前色相值轉換為全飽和、全亮的 RGB 彩虹色
  strip.setPixelColor(0, color);             // 將計算出的顏色寫入第 0 顆 LED 的顏色緩衝區
  strip.show();                              // 將緩衝區顏色推送至硬體，LED 實際變色

  // 解析 uint32_t 色彩值中各個分量（位移運算取出各通道）
  uint8_t r = (color >> 16) & 0xFF;         // 取出 R（紅色）分量（bit 16~23）
  uint8_t g = (color >> 8)  & 0xFF;         // 取出 G（綠色）分量（bit  8~15）
  uint8_t b = (color)       & 0xFF;         // 取出 B（藍色）分量（bit  0~7）

  // 在 Serial Monitor 格式化輸出當前色相與 RGB 值，方便除錯觀察
  Serial.printf("Hue: %3d | R:%3d G:%3d B:%3d\n", hue, r, g, b);

  hue++;          // 色相值遞增 1（uint8_t 超過 255 後自動歸零，形成無限彩虹循環）
  delay(20);      // 等待 20 毫秒後進入下一幀（20ms × 256 步 = 約 5.1 秒完整循環一次）
}
