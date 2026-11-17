#include <Arduino.h>

const int RED_LED_PIN = 7;         // 紅燈（Pin 7）
const int GREEN_LED_PIN = 13;      // 綠燈（Pin 13）
const int PEDESTRIAN_BUTTON_PIN = 2; // 行人按鈕（Pin 2，使用內建上拉，另一端接 GND 即可）

// 狀態機定義
enum TrafficState {
  STATE_GREEN_NORMAL,
  STATE_GREEN_FAST,
  STATE_RED_NORMAL,
  STATE_RED_FAST
};

TrafficState currentState = STATE_GREEN_NORMAL;

unsigned long phaseStartTime = 0;
unsigned long previousBlinkTime = 0;
bool currentLedState = false;

// 時間參數設定（毫秒）
const unsigned long DURATION_GREEN_NORMAL = 20000; // 綠燈正常 20 秒
const unsigned long DURATION_GREEN_FAST   = 10000; // 綠燈急閃 10 秒
const unsigned long DURATION_RED_NORMAL   = 20000; // 紅燈正常 20 秒
const unsigned long DURATION_RED_FAST     = 10000; // 紅燈急閃 10 秒

int lastPrintedSec = -1;

void setPhase(TrafficState newState, const char* msg) {
  currentState = newState;
  phaseStartTime = millis();
  previousBlinkTime = millis();
  currentLedState = false;
  lastPrintedSec = -1;

  // 進入新階段時先關閉所有燈
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);

  Serial.println(msg);
}

void setup() {
  Serial.begin(9600);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  // 使用 INPUT_PULLUP，按鈕一腳接 Pin 2、另一腳接 GND，按下為 LOW
  pinMode(PEDESTRIAN_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("==================================================");
  Serial.println("  進階紅綠燈模擬系統（有限狀態機 FSM + 行人請求）");
  Serial.println("  * 綠燈：前 20 秒正常閃爍，後 10 秒急閃");
  Serial.println("  * 紅燈：前 20 秒正常閃爍，後 10 秒急閃");
  Serial.println("  * 行人通行請求：可按 Pin 2 實體按鈕，");
  Serial.println("    或在 Serial Monitor 輸入字母 'p'！");
  Serial.println("==================================================");

  setPhase(STATE_GREEN_NORMAL, "\n>>> 【綠燈階段】通行開始（正常節奏）...");
}

void loop() {
  unsigned long now = millis();
  unsigned long elapsedInPhase = now - phaseStartTime;

  // 1. 偵測行人通行請求（支援實體按鈕 Pin 2 或 Serial 輸入 'p'）
  bool pedestrianRequested = false;
  if (digitalRead(PEDESTRIAN_BUTTON_PIN) == LOW) {
    pedestrianRequested = true;
    delay(20); // 簡易按鍵去彈跳
  }
  if (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch == 'p' || ch == 'P') {
      pedestrianRequested = true;
    }
  }

  // 若在綠燈正常階段按下行人按鈕，立即提前進入最後急閃倒數
  if (pedestrianRequested && currentState == STATE_GREEN_NORMAL) {
    Serial.println("\n🚶 [行人請求！] 綠燈將提早進入最後 10 秒急閃，準備變換紅燈！");
    setPhase(STATE_GREEN_FAST, "  ⚠️ 進入綠燈【急閃警示】！");
    elapsedInPhase = 0;
  }

  // 2. 狀態機與閃爍邏輯（完全不使用 delay，系統即時反應）
  int activePin = LOW;
  unsigned long blinkInterval = 500; // 預設 1Hz（亮 500ms、滅 500ms）
  unsigned long phaseDuration = 20000;
  const char* phaseName = "";

  switch (currentState) {
    case STATE_GREEN_NORMAL:
      activePin = GREEN_LED_PIN;
      blinkInterval = 500;
      phaseDuration = DURATION_GREEN_NORMAL;
      phaseName = "綠燈正常";
      if (elapsedInPhase >= phaseDuration) {
        setPhase(STATE_GREEN_FAST, "\n⚠️ 綠燈進入最後 10 秒【急閃警示】！");
      }
      break;

    case STATE_GREEN_FAST:
      activePin = GREEN_LED_PIN;
      blinkInterval = 125; // 4Hz 急閃（亮 125ms、滅 125ms）
      phaseDuration = DURATION_GREEN_FAST;
      phaseName = "⚡ 綠燈急閃";
      if (elapsedInPhase >= phaseDuration) {
        setPhase(STATE_RED_NORMAL, "\n>>> 【紅燈階段】行人請止步...");
      }
      break;

    case STATE_RED_NORMAL:
      activePin = RED_LED_PIN;
      blinkInterval = 500;
      phaseDuration = DURATION_RED_NORMAL;
      phaseName = "紅燈正常";
      if (elapsedInPhase >= phaseDuration) {
        setPhase(STATE_RED_FAST, "\n⚠️ 紅燈進入最後 10 秒【急閃警示】！");
      }
      break;

    case STATE_RED_FAST:
      activePin = RED_LED_PIN;
      blinkInterval = 125; // 4Hz 急閃
      phaseDuration = DURATION_RED_FAST;
      phaseName = "⚡ 紅燈急閃";
      if (elapsedInPhase >= phaseDuration) {
        setPhase(STATE_GREEN_NORMAL, "\n>>> 【綠燈階段】通行開始（正常節奏）...");
      }
      break;
  }

  // 3. LED 閃爍切換
  if (now - previousBlinkTime >= blinkInterval) {
    previousBlinkTime = now;
    currentLedState = !currentLedState;
    digitalWrite(activePin, currentLedState ? HIGH : LOW);
  }

  // 4. 序列埠每秒印出一次倒數秒數
  int currentSec = elapsedInPhase / 1000;
  if (currentSec != lastPrintedSec) {
    lastPrintedSec = currentSec;
    int remainingSec = (phaseDuration - elapsedInPhase) / 1000;
    if (remainingSec >= 0) {
      Serial.print("  [");
      Serial.print(phaseName);
      Serial.print("] 已過 ");
      Serial.print(currentSec);
      Serial.print(" 秒 / 剩餘 ");
      Serial.print(remainingSec);
      Serial.println(" 秒");
    }
  }
}
