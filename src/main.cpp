//搖桿
//這是一塊五向導航按鍵模組（又稱 5D 搖桿模組或五方向按鈕模組）。
//主要功能與特點
//方向控制：中間的黑色按鈕支援上（UP）、下（DWN）、左（LFT）、右（RHT）、中（MID） 5 個方向的觸覺開關點擊。
//額外按鍵：右側帶有兩個獨立按鈕：SET（設置）與 RST（復位）。
//適用電壓：通常支援 2.0V 至 9.0V（常見為 3.3V 或 5V）寬工作電壓。
//應用場景：常搭配 Arduino、Raspberry Pi 等單片機（MCU）開發板，用作小型選單導航、介面控制或遊戲搖桿。
//引腳說明（從上到下）

//COM：公共端（Common）
//UP：上方向鍵
//DWN：下方向鍵
//LFT：左方向鍵
//RHT：右方向鍵
//MID：中間方向鍵（按壓確認）
//SET：設置按鍵
//RST：復位按鍵

// 引入 Arduino 核心標頭檔
#include <Arduino.h>

// 定義結構體 NavKey，封裝按鍵名稱、腳位與前次狀態
struct NavKey {
    const char* name;    // 按鍵名稱字串（例如 "UP"、"DOWN"）
    uint8_t pin;         // 連接在 Arduino 上的數位腳位編號
    bool lastState;      // 記錄前一次電位狀態（HIGH 未按，LOW 按下）
};

// 初始化按鍵陣列，定義 7 顆按鍵的名稱、接腳及初始狀態
NavKey keys[] = {
    {"UP",    2, HIGH},  // 0: UP 上方向鍵（D2）
    {"DOWN",  3, HIGH},  // 1: DOWN 下方向鍵（D3）
    {"LEFT",  4, HIGH},  // 2: LEFT 左方向鍵（D4）
    {"RIGHT", 5, HIGH},  // 3: RIGHT 右方向鍵（D5）
    {"MID",   6, HIGH},  // 4: MID 中間確認鍵（D6）
    {"SET",   7, HIGH},  // 5: SET 設定功能鍵（D7）
    {"RST",   8, HIGH}   // 6: RST 復位功能鍵（D8）
};

// 計算按鍵總數量
const int KEY_COUNT = sizeof(keys) / sizeof(keys[0]);

// 初始化設定函式
void setup() {
    // 啟動序列埠通訊，鮑率設為 115200 bps
    Serial.begin(115200);

    // 等待序列埠通訊就緒
    while (!Serial) { delay(10); }

    // 迴圈將所有按鍵腳位設為內建上拉模式（INPUT_PULLUP）
    for (int i = 0; i < KEY_COUNT; i++) {
        pinMode(keys[i].pin, INPUT_PULLUP);
    }

    // 發送初始化完成訊息
    Serial.println("=== 5-Way Continuous Navigation Ready ===");
}

// 主循環掃描函式
void loop() {
    // 循序檢查每一個按鍵的電位狀態
    for (int i = 0; i < KEY_COUNT; i++) {
        // 讀取當前腳位電位（HIGH 代表未按，LOW 代表按下）
        bool currentState = digitalRead(keys[i].pin);

        // 偵測「按下」瞬間（由 HIGH 變為 LOW）
        if (keys[i].lastState == HIGH && currentState == LOW) {
            delay(10); // 防彈跳延遲
            if (digitalRead(keys[i].pin) == LOW) {
                // 發送按下訊號，例如 "PRESS:UP"
                Serial.print("PRESS:");
                Serial.println(keys[i].name);
            }
        }
        // 偵測「放開」瞬間（由 LOW 變為 HIGH）
        else if (keys[i].lastState == LOW && currentState == HIGH) {
            delay(10); // 防彈跳延遲
            if (digitalRead(keys[i].pin) == HIGH) {
                // 發送放開訊號，例如 "RELEASE:UP"
                Serial.print("RELEASE:");
                Serial.println(keys[i].name);
            }
        }

        // 更新最後狀態
        keys[i].lastState = currentState;
    }
}
