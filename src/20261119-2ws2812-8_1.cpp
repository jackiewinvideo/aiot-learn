// ============================================================
// ESP32-S3 雙 WS2812B 多特效輪播
// ── 外接燈條：GPIO 7，8 顆 WS2812B
// ── 內建 LED ：GPIO 48，1 顆 WS2812B（ESP32-S3 板載）
// 兩組 LED 同步作動，特效每 5 秒自動切換
// 使用庫：Adafruit NeoPixel
// 特效清單（共 8 種）：
//   1. 彩虹流動   2. 跑馬燈   3. 呼吸燈   4. 彩色填充
//   5. 劇院追逐   6. 火焰效果  7. 流星雨   8. 彩色閃爍
// ============================================================

#include <Arduino.h>             // Arduino 核心標頭
#include <Adafruit_NeoPixel.h>  // NeoPixel 控制庫

// ── 硬體設定 ───────────────────────────────────────────────
#define EXT_PIN      7           // 外接燈條資料腳位：GPIO 7
#define EXT_LEDS     8           // 外接燈條 LED 數量：8 顆
#define INT_PIN      48          // 內建 LED 資料腳位：GPIO 48
#define INT_LEDS     1           // 內建 LED 數量：1 顆
#define BRIGHTNESS   80          // 全域亮度（0~255）

// 建立兩個 NeoPixel 物件（GRB 色彩順序，800KHz）
Adafruit_NeoPixel ext(EXT_LEDS, EXT_PIN, NEO_GRB + NEO_KHZ800); // 外接 8 顆
Adafruit_NeoPixel intLed(INT_LEDS, INT_PIN, NEO_GRB + NEO_KHZ800); // 內建 1 顆

// ── 特效計時設定 ───────────────────────────────────────────
#define EFFECT_DURATION 5000     // 每種特效持續時間（ms）：5 秒
#define TOTAL_EFFECTS   8        // 特效總數

uint8_t       currentEffect = 0; // 目前特效編號（0 ~ 7）
unsigned long effectStart   = 0; // 目前特效的開始時間戳

// ── 各特效的狀態變數 ───────────────────────────────────────
uint8_t       rainbowOffset = 0; // 彩虹流動：色相偏移值
int           breathVal     = 0; // 呼吸燈：目前亮度
int           breathDir     = 1; // 呼吸燈：方向（+1 增 / -1 減）
uint8_t       chasePos      = 0; // 跑馬燈：目前點亮位置
uint8_t       theaterPos    = 0; // 劇院追逐：位移
int           meteorPos     = 0; // 流星雨：流星頭部位置
unsigned long lastTwinkle   = 0; // 閃爍：上次點亮時間

// ── 輔助函式：HSV → RGB ────────────────────────────────────
// 色相 h（0~255）、飽和度 s、明度 v → uint32_t NeoPixel 顏色
uint32_t hsv(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;
  uint8_t reg = h / 43;                             // 色相分 6 區
  uint8_t rem = (h - reg * 43) * 6;                 // 區域內進度
  uint8_t p   = (v * (255 - s)) >> 8;               // 最低分量
  uint8_t q   = (v * (255 - ((s * rem) >> 8))) >> 8;           // 下降
  uint8_t t   = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;   // 上升
  switch (reg) {
    case 0: r=v; g=t; b=p; break;  // 紅→黃
    case 1: r=q; g=v; b=p; break;  // 黃→綠
    case 2: r=p; g=v; b=t; break;  // 綠→青
    case 3: r=p; g=q; b=v; break;  // 青→藍
    case 4: r=t; g=p; b=v; break;  // 藍→紫
    default:r=v; g=p; b=q; break;  // 紫→紅
  }
  return ext.Color(r, g, b);        // 回傳合成顏色（兩個 strip 格式相同）
}

// ── 重設所有特效狀態 ───────────────────────────────────────
// 切換特效前呼叫，避免前一個特效殘留狀態
void resetState() {
  rainbowOffset = 0;    // 彩虹偏移歸零
  breathVal     = 0;    // 呼吸亮度歸零
  breathDir     = 1;    // 呼吸方向重設
  chasePos      = 0;    // 跑馬燈位置歸零
  theaterPos    = 0;    // 劇院追逐歸零
  meteorPos     = 0;    // 流星位置歸零
  lastTwinkle   = 0;    // 閃爍計時歸零
  ext.clear();    ext.show();    // 清除外接燈條
  intLed.clear(); intLed.show(); // 清除內建 LED
}

// ── 設定內建 LED 顏色並顯示 ───────────────────────────────
// 內建 LED 只有 1 顆，傳入顏色即可
void setIntLed(uint32_t color) {
  intLed.setPixelColor(0, color); // 設定第 0 顆（唯一一顆）
  intLed.show();                  // 推送顯示
}

// ════════════════════════════════════════════════════════════
// 特效 1：彩虹流動
//   外接：8 顆均勻分佈不同色相，整體色相偏移使彩虹流動
//   內建：顯示目前偏移色相（跟著彩虹主色移動）
// ════════════════════════════════════════════════════════════
void effectRainbow() {
  for (uint8_t i = 0; i < EXT_LEDS; i++) {
    uint8_t h = rainbowOffset + i * (256 / EXT_LEDS); // 均勻分佈 8 段色相
    ext.setPixelColor(i, hsv(h, 255, 255));            // 設定外接 LED
  }
  ext.show();                            // 推送外接燈條
  setIntLed(hsv(rainbowOffset, 255, 255)); // 內建 LED 跟隨主色相
  rainbowOffset += 2;                    // 色相偏移遞增（越大越快）
  delay(20);                             // 幀間隔 20ms
}

// ════════════════════════════════════════════════════════════
// 特效 2：跑馬燈
//   外接：單顆彩光在 8 顆 LED 間依序跑動
//   內建：同步顯示目前跑馬燈的顏色
// ════════════════════════════════════════════════════════════
void effectChase() {
  ext.clear();                                           // 清除外接 LED
  uint32_t c = hsv(chasePos * 32, 255, 255);            // 計算當前顏色
  ext.setPixelColor(chasePos, c);                        // 只點亮當前位置
  ext.show();                                            // 推送外接
  setIntLed(c);                                          // 內建 LED 同步顏色
  chasePos = (chasePos + 1) % EXT_LEDS;                 // 移到下一顆
  delay(100);                                            // 幀間隔 100ms
}

// ════════════════════════════════════════════════════════════
// 特效 3：呼吸燈（藍紫色）
//   外接：8 顆同步亮度 0→255→0 緩慢呼吸
//   內建：同步呼吸，色相稍偏（粉色），形成補色對比
// ════════════════════════════════════════════════════════════
void effectBreathing() {
  uint32_t extColor = hsv(192, 255, breathVal);  // 外接：藍紫色
  uint32_t intColor = hsv(0,   255, breathVal);  // 內建：紅色（補色對比）
  for (uint8_t i = 0; i < EXT_LEDS; i++) {
    ext.setPixelColor(i, extColor);              // 外接全部同色
  }
  ext.show();              // 推送外接
  setIntLed(intColor);     // 推送內建（紅色呼吸）
  breathVal += breathDir * 3;                     // 亮度每幀變化 3
  if (breathVal >= 255) { breathVal = 255; breathDir = -1; } // 頂端反向
  if (breathVal <= 0)   { breathVal = 0;   breathDir =  1; } // 底端反向
  delay(10);               // 幀間隔 10ms
}

// ════════════════════════════════════════════════════════════
// 特效 4：彩色填充
//   外接：紅→綠→藍 逐顆從左到右填充
//   內建：顯示目前正在填充的顏色
// ════════════════════════════════════════════════════════════
void effectColorWipe() {
  static uint32_t colors[] = {
    ext.Color(200, 0, 0),    // 紅色
    ext.Color(0, 200, 0),    // 綠色
    ext.Color(0, 0, 200)     // 藍色
  };
  static uint8_t colorIdx = 0; // 目前顏色索引
  static uint8_t pos      = 0; // 目前填充位置

  ext.setPixelColor(pos, colors[colorIdx]); // 點亮目前位置
  ext.show();                               // 推送外接
  setIntLed(colors[colorIdx]);              // 內建 LED 顯示當前填充色

  pos++;                                    // 移到下一顆
  if (pos >= EXT_LEDS) {                    // 填滿後換下一個顏色
    pos = 0;
    colorIdx = (colorIdx + 1) % 3;          // 顏色循環（0→1→2→0...）
  }
  delay(80);                                // 每顆間隔 80ms
}

// ════════════════════════════════════════════════════════════
// 特效 5：劇院追逐
//   外接：每隔 3 顆點亮，整體左移（廣告牌效果）
//   內建：隨位移變色（與追逐同步）
// ════════════════════════════════════════════════════════════
void effectTheaterChase() {
  ext.clear();                                 // 清除外接
  uint32_t c = hsv(theaterPos * 30, 255, 255); // 計算當前彩色
  for (uint8_t i = 0; i < EXT_LEDS; i++) {
    if ((i + theaterPos) % 3 == 0) {           // 每隔 3 顆點亮
      ext.setPixelColor(i, c);
    }
  }
  ext.show();              // 推送外接
  setIntLed(c);            // 內建 LED 顯示同步顏色
  theaterPos = (theaterPos + 1) % 3; // 位移循環
  delay(80);               // 幀間隔 80ms
}

// ════════════════════════════════════════════════════════════
// 特效 6：火焰效果
//   外接：8 顆隨機紅橙黃，模擬跳動火焰
//   內建：顯示平均火焰色（偏橙紅），像餘燼
// ════════════════════════════════════════════════════════════
void effectFire() {
  for (uint8_t i = 0; i < EXT_LEDS; i++) {
    uint8_t flicker = random(100, 255);          // 每顆亮度隨機
    uint8_t r = flicker;                         // 紅色分量
    uint8_t g = random(0, flicker / 3);          // 少量綠色（橙黃感）
    ext.setPixelColor(i, ext.Color(r, g, 0));    // 藍色為 0（無藍才像火）
  }
  ext.show();                                    // 推送外接
  uint8_t br = random(120, 220);                 // 內建 LED 亮度（偏亮）
  setIntLed(ext.Color(br, random(0, br/4), 0)); // 內建：橙紅餘燼色
  delay(60);                                     // 幀間隔 60ms
}

// ════════════════════════════════════════════════════════════
// 特效 7：流星雨（白色流星從第 0 顆劃向第 7 顆）
//   外接：流星頭最亮，5 顆漸暗拖尾
//   內建：流星頭經過時閃亮，其他時間微弱藍光待機
// ════════════════════════════════════════════════════════════
void effectMeteor() {
  ext.clear();                                    // 清除外接

  for (int t = 0; t < 5; t++) {                  // 拖尾長度 5
    int p = meteorPos - t;                        // 拖尾各顆位置
    if (p >= 0 && p < EXT_LEDS) {                 // 在範圍內才畫
      uint8_t br = 255 - t * 50;                  // 亮度：頭 255，每節 -50
      ext.setPixelColor(p, ext.Color(br, br, br)); // 白色流星
    }
  }
  ext.show();                                     // 推送外接

  // 內建 LED：流星頭正好飛過外接末端時閃白，其餘藍色待機
  if (meteorPos == EXT_LEDS - 1) {
    setIntLed(ext.Color(255, 255, 255));           // 流星抵達時全亮白
  } else {
    setIntLed(ext.Color(0, 0, 30));               // 平時微弱藍光
  }

  meteorPos++;                                    // 流星前進
  if (meteorPos > EXT_LEDS + 5) meteorPos = 0;   // 超出畫面後重置
  delay(60);                                      // 幀間隔 60ms
}

// ════════════════════════════════════════════════════════════
// 特效 8：彩色閃爍（Twinkle）
//   外接：LED 逐漸暗淡，每 100ms 隨機點亮一顆
//   內建：每次有 LED 被點亮時，同步閃爍同色
// ════════════════════════════════════════════════════════════
void effectTwinkle() {
  // 外接 LED：所有顆亮度逐漸衰減
  for (uint8_t i = 0; i < EXT_LEDS; i++) {
    uint32_t c = ext.getPixelColor(i);                      // 取目前顏色
    uint8_t r = max(0, (int)((c >> 16) & 0xFF) - 15);      // R 衰減 15
    uint8_t g = max(0, (int)((c >>  8) & 0xFF) - 15);      // G 衰減 15
    uint8_t b = max(0, (int)((c)       & 0xFF) - 15);      // B 衰減 15
    ext.setPixelColor(i, ext.Color(r, g, b));               // 寫回衰減後的顏色
  }

  // 每 100ms 隨機點亮一顆，並同步內建 LED
  if (millis() - lastTwinkle > 100) {
    uint8_t idx = random(EXT_LEDS);                          // 隨機選一顆
    uint32_t c  = hsv(random(256), 255, 255);                // 隨機顏色
    ext.setPixelColor(idx, c);                               // 外接點亮
    setIntLed(c);                                            // 內建同色閃爍
    lastTwinkle = millis();                                  // 更新計時
  }
  ext.show();   // 推送外接
  delay(30);    // 幀間隔 30ms
}

// ============================================================
// setup() — 開機初始化，只執行一次
// ============================================================
void setup() {
  Serial.begin(115200);               // 啟動序列埠

  // 初始化外接燈條
  ext.begin();                        // 啟動 GPIO7 NeoPixel 驅動
  ext.setBrightness(BRIGHTNESS);      // 設定亮度
  ext.clear();                        // 清除顏色緩衝
  ext.show();                         // 推送清除（確保初始熄滅）

  // 初始化內建 LED
  intLed.begin();                     // 啟動 GPIO48 NeoPixel 驅動
  intLed.setBrightness(BRIGHTNESS);   // 設定亮度
  intLed.clear();                     // 清除顏色緩衝
  intLed.show();                      // 推送清除

  // 開機白燈測試（確認兩組 LED 都正常）
  ext.fill(ext.Color(255, 255, 255)); // 外接全白
  ext.show();
  setIntLed(intLed.Color(255, 255, 255)); // 內建全白
  delay(400);                         // 保持 400ms

  ext.clear();    ext.show();         // 外接熄滅
  intLed.clear(); intLed.show();      // 內建熄滅
  delay(200);                         // 熄滅後靜止

  effectStart = millis();             // 記錄第一個特效開始時間
  Serial.println("=== 雙 WS2812B 特效輪播 開始 ===");
  Serial.printf("特效 1/%d：彩虹流動\n", TOTAL_EFFECTS);
}

// ============================================================
// loop() — 主循環：自動切換特效
// ============================================================
void loop() {
  // 判斷目前特效是否播放超過設定時間
  if (millis() - effectStart >= EFFECT_DURATION) {
    currentEffect = (currentEffect + 1) % TOTAL_EFFECTS; // 切換下一個特效
    effectStart   = millis();                             // 重設計時器
    resetState();                                         // 重設狀態變數

    // Serial Monitor 顯示當前特效名稱
    const char* names[] = {
      "彩虹流動", "跑馬燈", "呼吸燈（藍紫+紅）",
      "彩色填充", "劇院追逐", "火焰效果",
      "流星雨",   "彩色閃爍"
    };
    Serial.printf("特效 %d/%d：%s\n",
      currentEffect + 1, TOTAL_EFFECTS, names[currentEffect]);
  }

  // 根據特效編號呼叫對應函式
  switch (currentEffect) {
    case 0: effectRainbow();      break; // 彩虹流動
    case 1: effectChase();        break; // 跑馬燈
    case 2: effectBreathing();    break; // 呼吸燈
    case 3: effectColorWipe();    break; // 彩色填充
    case 4: effectTheaterChase(); break; // 劇院追逐
    case 5: effectFire();         break; // 火焰效果
    case 6: effectMeteor();       break; // 流星雨
    case 7: effectTwinkle();      break; // 彩色閃爍
  }
}
