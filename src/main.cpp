// ============================================================
// ESP32-S3 WS2812B 8 顆 LED 多種特效輪流播放
// 開發板：ESP32-S3
// LED 腳位：GPIO 7（外接 8 顆 WS2812B 燈條）
// 使用庫：Adafruit NeoPixel
// 特效清單：
//   1. 彩虹流動   Rainbow Flow
//   2. 跑馬燈     Color Chase
//   3. 呼吸燈     Breathing
//   4. 彩色填充   Color Wipe
//   5. 劇院追逐   Theater Chase
//   6. 火焰效果   Fire
//   7. 流星雨     Meteor
//   8. 彩色閃爍   Twinkle
// ============================================================

#include <Arduino.h>              // 引入 Arduino 核心標頭檔
#include <Adafruit_NeoPixel.h>   // 引入 Adafruit NeoPixel 庫

// ── 硬體設定 ──────────────────────────────────────────────
#define LED_PIN     7             // WS2812B 資料接腳：GPIO 7
#define NUM_LEDS    8             // LED 顆數：8 顆
#define BRIGHTNESS  80            // 全域亮度（0~255）

// 建立 NeoPixel 物件（GRB 色彩順序，800KHz 訊號）
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ── 特效計時設定 ───────────────────────────────────────────
#define EFFECT_DURATION 5000      // 每種特效播放時間（毫秒）：5 秒

// 特效總數（每次新增特效要更新這個數字）
#define TOTAL_EFFECTS   8

// 目前正在播放的特效編號（0 ~ TOTAL_EFFECTS-1）
uint8_t currentEffect = 0;

// 特效開始播放的時間戳記
unsigned long effectStart = 0;

// 彩虹流動用的色相偏移值
uint8_t rainbowOffset = 0;

// 呼吸燈用的亮度值與方向
int breathVal = 0;        // 目前亮度
int breathDir = 1;        // 遞增(1) 或 遞減(-1)

// 跑馬燈用的當前點亮位置
uint8_t chasePos = 0;

// 劇院追逐用的位移
uint8_t theaterPos = 0;

// 流星用的頭部位置（允許超出 LED 範圍讓流星消失）
int meteorPos = 0;

// 閃爍用的隨機種子計時
unsigned long lastTwinkle = 0;

// ── 輔助函式：HSV 轉 RGB ──────────────────────────────────
// 將色相(0~255)、飽和度、明度轉成 NeoPixel 格式的 uint32_t 顏色
uint32_t hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;                              // 輸出 RGB 三個分量
  uint8_t region    = h / 43;                   // 色相分成 6 個區域
  uint8_t remainder = (h - (region * 43)) * 6;  // 區域內進度（0~255）
  uint8_t p = (v * (255 - s)) >> 8;             // 最低亮度分量
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;        // 下降漸變
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8; // 上升漸變
  switch (region) {
    case 0: r = v; g = t; b = p; break;  // 紅→黃
    case 1: r = q; g = v; b = p; break;  // 黃→綠
    case 2: r = p; g = v; b = t; break;  // 綠→青
    case 3: r = p; g = q; b = v; break;  // 青→藍
    case 4: r = t; g = p; b = v; break;  // 藍→紫
    default: r = v; g = p; b = q; break; // 紫→紅
  }
  return strip.Color(r, g, b);                   // 回傳組合後的顏色值
}

// ── 重設所有特效狀態變數 ──────────────────────────────────
// 切換特效時呼叫，避免前一個特效的狀態影響下一個
void resetEffectState() {
  rainbowOffset = 0;    // 彩虹偏移歸零
  breathVal     = 0;    // 呼吸燈亮度歸零
  breathDir     = 1;    // 呼吸方向重設為遞增
  chasePos      = 0;    // 跑馬燈位置歸零
  theaterPos    = 0;    // 劇院追逐位移歸零
  meteorPos     = 0;    // 流星位置歸零
  lastTwinkle   = 0;    // 閃爍計時歸零
  strip.clear();        // 清除所有 LED 顏色
  strip.show();         // 推送清除結果
}

// ════════════════════════════════════════════════════════════
// 特效 1：彩虹流動
//   每顆 LED 顯示不同色相，整體色相偏移使彩虹「流動」
// ════════════════════════════════════════════════════════════
void effectRainbow() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    // 每顆 LED 色相 = 偏移量 + 均勻分佈（256/8=32 步差距）
    uint8_t hue = rainbowOffset + i * (256 / NUM_LEDS);
    strip.setPixelColor(i, hsvToRgb(hue, 255, 255)); // 設定顏色（全飽和全亮）
  }
  strip.show();          // 推送顯示
  rainbowOffset += 2;    // 每幀偏移 2，速度適中（+=1 慢，+=4 快）
  delay(20);             // 幀間隔 20ms
}

// ════════════════════════════════════════════════════════════
// 特效 2：跑馬燈
//   一顆彩色光點在 8 顆 LED 間依序「跑動」
// ════════════════════════════════════════════════════════════
void effectChase() {
  strip.clear();         // 先清除所有 LED
  // 只點亮目前位置的 LED，色相隨位置變化
  strip.setPixelColor(chasePos, hsvToRgb(chasePos * 32, 255, 255));
  strip.show();          // 推送顯示
  chasePos = (chasePos + 1) % NUM_LEDS; // 下一顆，超出則回到第 0 顆
  delay(100);            // 幀間隔 100ms（越小越快）
}

// ════════════════════════════════════════════════════════════
// 特效 3：呼吸燈（紫色）
//   全部 LED 同步亮度從 0 緩慢上升到 255，再回到 0
// ════════════════════════════════════════════════════════════
void effectBreathing() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    // 色相固定為 192（藍紫色），亮度隨 breathVal 變化
    strip.setPixelColor(i, hsvToRgb(192, 255, breathVal));
  }
  strip.show();          // 推送顯示
  breathVal += breathDir * 3;       // 每幀亮度變化 3（越大越快）
  if (breathVal >= 255) { breathVal = 255; breathDir = -1; } // 到頂後反向
  if (breathVal <= 0)   { breathVal = 0;   breathDir =  1; } // 到底後反向
  delay(10);             // 幀間隔 10ms
}

// ════════════════════════════════════════════════════════════
// 特效 4：彩色填充
//   三種顏色依序從第 0 顆 LED「填充」到第 7 顆
// ════════════════════════════════════════════════════════════
void effectColorWipe() {
  // 定義三個填充顏色（紅、綠、藍）
  static uint32_t colors[] = {
    strip.Color(200, 0,   0),   // 紅色
    strip.Color(0,   200, 0),   // 綠色
    strip.Color(0,   0,   200)  // 藍色
  };
  static uint8_t  colorIdx = 0; // 目前使用的顏色索引
  static uint8_t  pos      = 0; // 目前填充到第幾顆

  strip.setPixelColor(pos, colors[colorIdx]); // 點亮目前位置
  strip.show();                               // 推送顯示
  pos++;                                      // 移到下一顆

  if (pos >= NUM_LEDS) {         // 填滿後換下一個顏色
    pos = 0;                     // 位置重設回第 0 顆
    colorIdx = (colorIdx + 1) % 3; // 顏色循環（0→1→2→0...）
  }
  delay(80);                     // 每顆填充間隔 80ms
}

// ════════════════════════════════════════════════════════════
// 特效 5：劇院追逐（Theatre Chase）
//   每隔一顆點亮，整體左移，像跑馬燈廣告牌
// ════════════════════════════════════════════════════════════
void effectTheaterChase() {
  strip.clear();         // 清除所有 LED
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    // 每隔 3 顆點亮一顆（偏移 theaterPos），其餘熄滅
    if ((i + theaterPos) % 3 == 0) {
      strip.setPixelColor(i, hsvToRgb(theaterPos * 30, 255, 255)); // 彩色
    }
  }
  strip.show();              // 推送顯示
  theaterPos = (theaterPos + 1) % 3; // 位移循環（0→1→2→0...）
  delay(80);                 // 幀間隔 80ms
}

// ════════════════════════════════════════════════════════════
// 特效 6：火焰效果
//   每顆 LED 隨機顯示紅/橙/黃色，模擬跳動火焰
// ════════════════════════════════════════════════════════════
void effectFire() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    uint8_t flicker = random(100, 255); // 每顆亮度隨機（製造閃爍感）
    uint8_t r = flicker;                // 紅色分量 = 亮度
    uint8_t g = random(0, flicker / 3); // 綠色分量少量（橙黃色）
    uint8_t b = 0;                      // 藍色分量為 0（無藍才像火）
    strip.setPixelColor(i, strip.Color(r, g, b)); // 設定顏色
  }
  strip.show();   // 推送顯示
  delay(60);      // 幀間隔 60ms（越小火焰越「跳」）
}

// ════════════════════════════════════════════════════════════
// 特效 7：流星雨（白色流星從頭到尾劃過）
//   流星頭部最亮，拖曳尾巴逐漸暗淡
// ════════════════════════════════════════════════════════════
void effectMeteor() {
  strip.clear();              // 清除所有 LED

  // 繪製流星：頭部最亮，向後每顆亮度遞減
  for (int tail = 0; tail < 5; tail++) {   // 流星尾巴長度 5
    int pos = meteorPos - tail;             // 尾巴各顆位置
    if (pos >= 0 && pos < NUM_LEDS) {       // 在範圍內才畫
      uint8_t bright = 255 - (tail * 50);  // 亮度：頭 255，每節減 50
      strip.setPixelColor(pos, strip.Color(bright, bright, bright)); // 白色
    }
  }
  strip.show();               // 推送顯示
  meteorPos++;                // 流星頭部前進一格
  if (meteorPos > NUM_LEDS + 5) { // 完全離開畫面後重新從頭開始
    meteorPos = 0;
  }
  delay(60);                  // 幀間隔 60ms（越小流星越快）
}

// ════════════════════════════════════════════════════════════
// 特效 8：彩色閃爍（Twinkle）
//   每隔一段時間隨機點亮一顆，其他逐漸暗淡
// ════════════════════════════════════════════════════════════
void effectTwinkle() {
  // 所有 LED 亮度衰減（模擬自然熄滅）
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    uint32_t c = strip.getPixelColor(i);         // 取出目前顏色
    uint8_t r = max(0, (int)((c >> 16) & 0xFF) - 15); // R 通道衰減 15
    uint8_t g = max(0, (int)((c >>  8) & 0xFF) - 15); // G 通道衰減 15
    uint8_t b = max(0, (int)((c)       & 0xFF) - 15); // B 通道衰減 15
    strip.setPixelColor(i, strip.Color(r, g, b));      // 寫回衰減後顏色
  }

  // 每 100ms 隨機點亮一顆 LED（隨機色相）
  if (millis() - lastTwinkle > 100) {
    uint8_t idx = random(NUM_LEDS);                        // 隨機選一顆
    strip.setPixelColor(idx, hsvToRgb(random(256), 255, 255)); // 隨機彩色
    lastTwinkle = millis();                                // 更新計時
  }
  strip.show();   // 推送顯示
  delay(30);      // 幀間隔 30ms
}

// ============================================================
// setup() — 開機初始化，只執行一次
// ============================================================
void setup() {
  Serial.begin(115200);              // 啟動序列埠（鮑率 115200）

  strip.begin();                     // 初始化 NeoPixel 驅動
  strip.setBrightness(BRIGHTNESS);   // 設定全域亮度
  strip.clear();                     // 清除所有 LED
  strip.show();                      // 推送清除（確保初始熄滅）

  effectStart = millis();            // 記錄第一個特效的開始時間
  Serial.println("=== WS2812B 8-LED 多特效輪播 開始 ==="); // 序列埠就緒訊息
  Serial.printf("特效 1/%d 開始：彩虹流動\n", TOTAL_EFFECTS); // 顯示當前特效
}

// ============================================================
// loop() — 主循環：自動切換特效
// ============================================================
void loop() {
  // 判斷目前特效是否已播放超過 EFFECT_DURATION（5秒）
  if (millis() - effectStart >= EFFECT_DURATION) {
    currentEffect = (currentEffect + 1) % TOTAL_EFFECTS; // 切換到下一個特效
    effectStart = millis();                               // 重設計時器
    resetEffectState();                                   // 清除上個特效狀態

    // 在 Serial Monitor 顯示當前特效名稱
    const char* names[] = {
      "彩虹流動", "跑馬燈", "呼吸燈", "彩色填充",
      "劇院追逐", "火焰效果", "流星雨", "彩色閃爍"
    };
    Serial.printf("特效 %d/%d 開始：%s\n",
      currentEffect + 1, TOTAL_EFFECTS, names[currentEffect]);
  }

  // 根據 currentEffect 呼叫對應的特效函式
  switch (currentEffect) {
    case 0: effectRainbow();      break; // 特效 1：彩虹流動
    case 1: effectChase();        break; // 特效 2：跑馬燈
    case 2: effectBreathing();    break; // 特效 3：呼吸燈
    case 3: effectColorWipe();    break; // 特效 4：彩色填充
    case 4: effectTheaterChase(); break; // 特效 5：劇院追逐
    case 5: effectFire();         break; // 特效 6：火焰效果
    case 6: effectMeteor();       break; // 特效 7：流星雨
    case 7: effectTwinkle();      break; // 特效 8：彩色閃爍
  }
}
