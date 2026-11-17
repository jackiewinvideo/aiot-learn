// 引入 Arduino 核心庫，提供 pin、delay、Serial 等基本函數
#include <Arduino.h>

// 引入 Adafruit NeoPixel 燈條控制庫，用於發送 WS2812 專屬高頻訊號
#include <Adafruit_NeoPixel.h>

// ==========================================
// 1. 硬體常數設定區
// ==========================================

// 定義 WS2812 燈條的資料輸入線 (DIN) 連接在 Arduino 的 Pin 7 數位腳位
#define LED_PIN     7

// 定義燈條上的 RGB LED 燈珠數量，此處為 8 顆
#define LED_COUNT   8

// 設定整體全域亮度，範圍是 0 (最暗/關閉) 到 255 (最亮)，設定 50 柔和不刺眼且不耗電
#define BRIGHTNESS 50

// 建立 NeoPixel 燈條物件實例：
// 參數 1: 燈珠數量 (LED_COUNT = 8)
// 參數 2: 控制腳位 (LED_PIN = 7)
// 參數 3: 像素燈珠色彩排列格式 (NEO_GRB) 與 通訊時脈頻率 (NEO_KHZ800 支援 800KHz WS2812B)
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ==========================================
// 2. 輔助色彩演算法：色輪 (Color Wheel)
// ==========================================

// 透過傳入 0 ~ 255 的數值，計算並回傳平滑輪轉的 32 位元 RGB 彩虹顏色
uint32_t Wheel(byte WheelPos) {
  // 將數值反轉，使得顏色由紅 -> 綠 -> 藍 順暢旋轉
  WheelPos = 255 - WheelPos;

  // 第一區段 (0 ~ 84)：由紅色平滑過渡到綠色
  if (WheelPos < 85) {
    // 依比例遞減紅色、遞增綠色，藍色為 0
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }

  // 第二區段 (85 ~ 169)：由綠色平滑過渡到藍色
  if (WheelPos < 170) {
    WheelPos -= 85; // 扣除偏移量以保持 0~84 計算基數
    // 紅色為 0、依比例遞減綠色、遞增藍色
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }

  // 第三區段 (170 ~ 255)：由藍色平滑過渡回紅色
  WheelPos -= 170; // 扣除偏移量
  // 依比例遞增紅色、遞減藍色，綠色為 0
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

// ==========================================
// 3. 特效 1：全彩彩虹流動 (Rainbow Flow)
// ==========================================

// 函式：產生平滑流動的全彩彩虹漸層效果
// waitMs: 每次流動更新的暫停毫秒數；cycles: 彩虹循環圈數
void effectRainbow(int waitMs, int cycles) {
  // 在序列埠監控視窗印出目前正在執行的特效名稱
  Serial.println(">>> [特效 1] 全彩彩虹流動 (Rainbow Flow)");

  // 外層迴圈：推動彩虹整體的旋轉相位偏移量 j，每次遞增 2 讓流暢度與速度兼具
  for (int j = 0; j < 256 * cycles; j += 2) {
    // 內層迴圈：依序計算 8 顆燈珠每一顆各自對應的顏色
    for (int i = 0; i < strip.numPixels(); i++) {
      // 根據燈珠索引 i 將色輪分佈均勻攤開在 8 顆燈上，並加上相位 j，最後取 255 的遮罩防溢位
      strip.setPixelColor(i, Wheel((i * 256 / strip.numPixels() + j) & 255));
    }
    // 將計算好的燈光顏色資料透過 Pin 7 輸出至實體燈珠上點亮
    strip.show();
    // 稍作延遲控制流動速度
    delay(waitMs);
  }
}

// ==========================================
// 4. 特效 2：霹靂遊俠來回掃描燈 (Knight Rider)
// ==========================================

// 函式：紅色主光點帶著柔和微光殘影在 8 顆燈間來回掃描
// color: 主光點顏色；waitMs: 掃描每步速度；cycles: 來回總次數
void effectKnightRider(uint32_t color, int waitMs, int cycles) {
  // 在序列埠印出特效資訊
  Serial.println(">>> [特效 2] 霹靂遊俠掃描燈 (Knight Rider)");

  // 執行指定次數的來回循環
  for (int c = 0; c < cycles; c++) {

    // --- 正向掃描：由第 0 顆燈掃描到第 7 顆燈 ---
    for (int i = 0; i < strip.numPixels(); i++) {
      // 先將所有燈珠熄滅清空緩衝區
      strip.clear();
      // 如果前一顆燈存在，將其設為弱光作為拖尾殘影 (RGB 30,0,0)
      if (i > 0) strip.setPixelColor(i - 1, strip.Color(30, 0, 0));
      // 當前位置 i 點亮為最亮的主光點 (如高亮紅光)
      strip.setPixelColor(i, color);
      // 如果後一顆燈存在，給予極微弱前導光 (RGB 10,0,0) 增加柔和立體感
      if (i < strip.numPixels() - 1) strip.setPixelColor(i + 1, strip.Color(10, 0, 0));
      // 刷新傳送資料點亮燈珠
      strip.show();
      // 停頓一下控制掃描速度
      delay(waitMs);
    }

    // --- 反向掃描：由第 7 顆燈倒退掃描回第 0 顆燈 ---
    for (int i = strip.numPixels() - 1; i >= 0; i--) {
      // 清空暫存緩衝區
      strip.clear();
      // 在後方留下殘影
      if (i < strip.numPixels() - 1) strip.setPixelColor(i + 1, strip.Color(30, 0, 0));
      // 點亮主光點
      strip.setPixelColor(i, color);
      // 在前方加上極微弱前導光
      if (i > 0) strip.setPixelColor(i - 1, strip.Color(10, 0, 0));
      // 刷新燈號
      strip.show();
      // 停頓一下控制倒退速度
      delay(waitMs);
    }
  }
}

// ==========================================
// 5. 特效 3：彩色流水跑馬填充 (Color Wipe)
// ==========================================

// 函式：將單一顏色從第 0 顆依序點亮至最後一顆
// color: 指定要點亮的顏色；waitMs: 每顆燈依序亮起的間隔時間
void effectColorWipe(uint32_t color, int waitMs) {
  // 從第 0 顆燈循序走到最後一顆燈 (7)
  for (int i = 0; i < strip.numPixels(); i++) {
    // 點亮當前這一顆燈為指定顏色
    strip.setPixelColor(i, color);
    // 立即刷新送出訊號
    strip.show();
    // 延遲一段短時間，營造流水逐一蔓延的視覺感
    delay(waitMs);
  }
}

// 函式：依序展示多種繽紛色彩的連續流水跑馬燈
void runColorWipeDemo() {
  // 在序列埠印出特效資訊
  Serial.println(">>> [特效 3] 彩色跑馬填充 (Color Wipe)");
  // 依序以紅色、綠色、藍色、暖黃、青藍、洋紅逐顆流水填充
  effectColorWipe(strip.Color(255, 0, 0), 40);   // 紅色填充
  effectColorWipe(strip.Color(0, 255, 0), 40);   // 綠色填充
  effectColorWipe(strip.Color(0, 0, 255), 40);   // 藍色填充
  effectColorWipe(strip.Color(255, 180, 0), 40); // 暖黃填充
  effectColorWipe(strip.Color(0, 255, 255), 40); // 青藍填充
  effectColorWipe(strip.Color(255, 0, 255), 40); // 洋紅填充
  effectColorWipe(strip.Color(0, 0, 0), 30);     // 黑色 (全滅) 流水清空燈條
}

// ==========================================
// 6. 特效 4：復古劇院霓虹燈 (Theater Chase)
// ==========================================

// 函式：像老式劇院招牌霓虹燈一樣每隔 3 顆燈跳躍追逐
// color: 追逐光顏色；waitMs: 跳動頻率；count: 追逐循環圈數
void effectTheaterChase(uint32_t color, int waitMs, int count) {
  // 在序列埠印出特效名稱
  Serial.println(">>> [特效 4] 復古劇院霓虹燈 (Theater Chase)");

  // 外層迴圈：循環執行指定圈數
  for (int c = 0; c < count; c++) {
    // 中層迴圈：q 從 0 輪到 2，創造三拍步進交錯跳動
    for (int q = 0; q < 3; q++) {

      // 內層迴圈 1：每間隔 3 顆燈 (i += 3) 將對應位置的燈點亮
      for (int i = 0; i < strip.numPixels(); i += 3) {
        // 防止索引超出 8 顆燈的上限
        if (i + q < strip.numPixels()) {
          // 點亮當前對應位置的燈
          strip.setPixelColor(i + q, color);
        }
      }
      // 送出訊號顯示目前點亮的燈
      strip.show();
      // 短暫停留呈現定格感
      delay(waitMs);

      // 內層迴圈 2：熄滅剛才點亮的那組燈，準備換下一組跳動
      for (int i = 0; i < strip.numPixels(); i += 3) {
        if (i + q < strip.numPixels()) {
          // 將該燈設為 0 (全黑熄滅)
          strip.setPixelColor(i + q, 0);
        }
      }
    }
  }
}

// ==========================================
// 7. 特效 5：彗星流星拖尾 (Comet Tail)
// ==========================================

// 函式：像流星劃過夜空般，白光彗星頭部帶著冰藍色尾跡快速滑過
// waitMs: 流星飛行速度；count: 流星飛過的次數
void effectComet(int waitMs, int count) {
  // 在序列埠印出特效名稱
  Serial.println(">>> [特效 5] 彗星流星拖尾 (Comet Effect)");

  // 外層迴圈：流星連續飛過 count 次
  for (int c = 0; c < count; c++) {
    // 內層迴圈：位置從 0 推進到 numPixels + 4，確保彗星完整滑出燈條末端
    for (int i = 0; i < strip.numPixels() + 4; i++) {
      // 每次移動前先清除上一幀的畫面
      strip.clear();

      // 彗星頭部：如果是落在燈條範圍內，給予最強的純白高光 (255, 255, 255)
      if (i < strip.numPixels()) {
        strip.setPixelColor(i, strip.Color(255, 255, 255));
      }

      // 彗星第一層尾跡 (後方 1 格)：給予亮冰藍色 (0, 150, 255)
      if (i - 1 >= 0 && i - 1 < strip.numPixels()) {
        strip.setPixelColor(i - 1, strip.Color(0, 150, 255));
      }

      // 彗星第二層尾跡 (後方 2 格)：給予漸暗深藍色 (0, 40, 120)
      if (i - 2 >= 0 && i - 2 < strip.numPixels()) {
        strip.setPixelColor(i - 2, strip.Color(0, 40, 120));
      }

      // 輸出刷新畫面
      strip.show();
      // 控制流星劃過的速度
      delay(waitMs);
    }
  }
}

// ==========================================
// 8. setup() 初始化設定（只執行一次）
// ==========================================
void setup() {
  // 初始化序列埠通訊，傳輸鮑率設定為 9600
  Serial.begin(9600);

  // 初始化 NeoPixel 函式庫，配置 Pin 7 腳位輸出模式
  strip.begin();

  // 套用全域亮度設定值 (50)
  strip.setBrightness(BRIGHTNESS);

  // 立即呼叫 show() 清空所有燈珠，確保通電時燈條為全滅初始狀態
  strip.show();

  // 在序列埠監控視窗印出歡迎看板
  Serial.println("==========================================");
  Serial.println("  8 顆 WS2812B RGB 炫彩燈光秀系統已啟動！");
  Serial.println("  訊號腳位：Pin 7");
  Serial.println("  燈珠數量：8 顆");
  Serial.println("==========================================");
}

// ==========================================
// 9. loop() 主循環任務（重複不斷執行）
// ==========================================
void loop() {
  // 執行特效 1：全彩彩虹流動（步進延遲 15ms，循環 2 圈）
  effectRainbow(15, 2);

  // 執行特效 2：霹靂遊俠紅色掃描（每步延遲 45ms，來回掃描 4 次）
  effectKnightRider(strip.Color(255, 0, 0), 45, 4);

  // 執行特效 3：彩色跑馬燈多色連續填充
  runColorWipeDemo();

  // 執行特效 4：劇院看板追逐（金色追逐 12 圈、青色追逐 12 圈）
  effectTheaterChase(strip.Color(255, 120, 0), 60, 12);
  effectTheaterChase(strip.Color(0, 200, 255), 60, 12);

  // 執行特效 5：彗星流星飛馳（每步延遲 50ms，飛過 4 次）
  effectComet(50, 4);

  // 在特效循環交替時微停頓 0.2 秒，增加節奏感
  delay(200);
}
