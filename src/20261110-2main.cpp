#include <Arduino.h>

// 腳位設定
const int RED_LED_PIN = 7;      // 紅燈（Pin 7）
const int YELLOW_LED_PIN = 13;  // 黃燈（Pin 13，若接在其他腳位可自由修改）

// 時間參數設定（毫秒）
// 紅燈：週期 2 秒（亮 0.5 秒，滅 1.5 秒）
const unsigned long RED_ON_TIME = 500;
const unsigned long RED_OFF_TIME = 1500;

// 黃燈：週期 3 秒（亮 0.5 秒，滅 2.5 秒）
const unsigned long YELLOW_ON_TIME = 500;
const unsigned long YELLOW_OFF_TIME = 2500;

// 時間軸紀錄變數
unsigned long redPreviousMillis = 0;
bool redState = false;

unsigned long yellowPreviousMillis = 0;
bool yellowState = false;

void setup() {
  Serial.begin(9600);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);

  // 初始化關閉 LED
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);

  unsigned long start = millis();
  redPreviousMillis = start;
  yellowPreviousMillis = start;

  Serial.println("==================================================");
  Serial.println("  雙 LED 獨立頻率非阻塞閃爍系統已啟動！");
  Serial.println("  * 紅燈：每隔 2 秒亮一下（亮 0.5 秒後滅）");
  Serial.println("  * 黃燈：每隔 3 秒亮一下（亮 0.5 秒後滅）");
  Serial.println("  * 完全不使用 delay()，採用 millis() 時間軸控制");
  Serial.println("==================================================");
}

void loop() {
  unsigned long currentMillis = millis();

  // ====================================================
  // 1. 紅燈獨立時間軸控制（週期 2000ms）
  // ====================================================
  if (redState) {
    // 目前紅燈為「亮」的狀態：檢查是否已亮滿 0.5 秒
    if (currentMillis - redPreviousMillis >= RED_ON_TIME) {
      redState = false;
      digitalWrite(RED_LED_PIN, LOW);
      redPreviousMillis = currentMillis; // 記錄轉為「滅」的時間點
      Serial.println("[紅燈] -> 滅 (OFF)");
    }
  } else {
    // 目前紅燈為「滅」的狀態：檢查是否已滅滿 1.5 秒（合計 2 秒）
    if (currentMillis - redPreviousMillis >= RED_OFF_TIME) {
      redState = true;
      digitalWrite(RED_LED_PIN, HIGH);
      redPreviousMillis = currentMillis; // 記錄轉為「亮」的時間點
      Serial.println("[紅燈] -> 亮 (ON)");
    }
  }

  // ====================================================
  // 2. 黃燈獨立時間軸控制（週期 3000ms）
  // ====================================================
  if (yellowState) {
    // 目前黃燈為「亮」的狀態：檢查是否已亮滿 0.5 秒
    if (currentMillis - yellowPreviousMillis >= YELLOW_ON_TIME) {
      yellowState = false;
      digitalWrite(YELLOW_LED_PIN, LOW);
      yellowPreviousMillis = currentMillis; // 記錄轉為「滅」的時間點
      Serial.println("  [黃燈] -> 滅 (OFF)");
    }
  } else {
    // 目前黃燈為「滅」的狀態：檢查是否已滅滿 2.5 秒（合計 3 秒）
    if (currentMillis - yellowPreviousMillis >= YELLOW_OFF_TIME) {
      yellowState = true;
      digitalWrite(YELLOW_LED_PIN, HIGH);
      yellowPreviousMillis = currentMillis; // 記錄轉為「亮」的時間點
      Serial.println("  [黃燈] -> 亮 (ON)");
    }
  }

  // 在這裡微控制器（MCU）完全不會被阻塞，隨時可以處理其他任務
}
