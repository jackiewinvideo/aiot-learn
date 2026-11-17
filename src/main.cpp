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

// 引入 Arduino 核心標頭檔，提供 pinMode、digitalRead、Serial 等核心函式與常數定義
#include <Arduino.h>

// 定義一個結構體（struct）NavKey，用來封裝單一按鍵的相關設定與狀態
struct NavKey {
    const char* name;    // 按鍵名稱字串（例如 "UP"、"DOWN"），供序列埠輸出顯示
    uint8_t pin;         // 按鍵連接在 Arduino 上的數位腳位編號（例如 2 代表 D2 腳位）
    bool lastState;      // 記錄按鍵上一次的讀取狀態（HIGH 或 LOW），用於負緣觸發判定
};

// 宣告並初始化按鍵結構體陣列，將 7 個按鍵的名稱、接腳及初始狀態集中管理
NavKey keys[] = {
    {"UP",    2, HIGH},  // 0: UP 上方向鍵，接在 D2 腳位，初始狀態設為 HIGH（未按下）
    {"DOWN",  3, HIGH},  // 1: DOWN 下方向鍵，接在 D3 腳位，初始狀態設為 HIGH（未按下）
    {"LEFT",  4, HIGH},  // 2: LEFT 左方向鍵，接在 D4 腳位，初始狀態設為 HIGH（未按下）
    {"RIGHT", 5, HIGH},  // 3: RIGHT 右方向鍵，接在 D5 腳位，初始狀態設為 HIGH（未按下）
    {"MID",   6, HIGH},  // 4: MID 中間確認鍵，接在 D6 腳位，初始狀態設為 HIGH（未按下）
    {"SET",   7, HIGH},  // 5: SET 設定功能鍵，接在 D7 腳位，初始狀態設為 HIGH（未按下）
    {"RST",   8, HIGH}   // 6: RST 復位功能鍵，接在 D8 腳位，初始狀態設為 HIGH（未按下）
};

// 計算按鍵陣列中的元素總數（利用陣列總位元組大小除以單一元素大小，計算出共有 7 顆按鍵）
const int KEY_COUNT = sizeof(keys) / sizeof(keys[0]);

// Arduino 開機或重置後首先執行的初始化函式（僅執行一次）
void setup() {
    // 啟動序列埠通訊，並設定鮑率（Baud Rate）為 115200 bps
    Serial.begin(115200);

    // 等待序列埠通訊就緒（主要針對原生 USB 晶片板子；Uno 板會直接通過）
    while (!Serial) { delay(10); }

    // 使用 for 迴圈依序巡訪陣列中的每一個按鍵
    for (int i = 0; i < KEY_COUNT; i++) {
        // 將該腳位設定為 INPUT_PULLUP 模式（啟用晶片內建上拉電阻，未按為 HIGH，按下接 GND 為 LOW）
        pinMode(keys[i].pin, INPUT_PULLUP);
    }

    // 在序列埠監視器印出開機初始化完成訊息，並自動換行
    Serial.println("=== 5-Way Navigation Key Initialized ===");
}

// 主循環函式，在 setup() 完成後會以無窮迴圈方式持續重複執行
void loop() {
    // 迴圈依序檢查 7 顆按鍵的即時觸發狀態
    for (int i = 0; i < KEY_COUNT; i++) {
        // 讀取第 i 個按鍵腳位的當前電位狀態（HIGH 代表未按下，LOW 代表按下接通）
        bool currentState = digitalRead(keys[i].pin);

        // 判斷是否為「剛好被按下的瞬間」（前一次為 HIGH 且當前為 LOW，即下降緣觸發）
        if (keys[i].lastState == HIGH && currentState == LOW) {
            // 延遲 15 毫秒進行硬體防彈跳（Debounce），避開微動開關機械接點彈跳產生的瞬間雜訊
            delay(15);

            // 防彈跳延遲後再次確認腳位電位，若仍維持在 LOW，代表這是一次真實有效的按鍵按下
            if (digitalRead(keys[i].pin) == LOW) {
                // 在序列埠印出提示前綴文字 "Pressed: "（不自動換行）
                Serial.print("Pressed: ");

                // 印出當前觸發的按鍵名稱，並自動換行（例如印出 "UP"）
                Serial.println(keys[i].name);
            }
        }

        // 更新歷史狀態，將本次讀取的電位存入 lastState，以供下一個迴圈比對
        keys[i].lastState = currentState;
    }
}
