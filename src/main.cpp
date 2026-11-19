// 新的 Arduino 程式碼起點
// 日期：2026-11-17
// 開發板：Arduino Uno
// 請在此撰寫新的程式碼

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("=== Ready ===");
}

void loop() {

}
