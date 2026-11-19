// ============================================================
// ESP32-S3 DHT11 溫濕度 Web Dashboard
// 感測器：DHT-11 接 GPIO 15
// 功能：連上 Wi-Fi，開啟 Web Server，提供即時溫濕度 Dashboard
// 瀏覽器開啟 http://<ESP32的IP> 即可查看
// ============================================================

#include <Arduino.h>       // Arduino 核心標頭
#include <WiFi.h>          // ESP32 Wi-Fi 庫
#include <WebServer.h>     // ESP32 內建 HTTP Web Server 庫
#include <DHT.h>           // Adafruit DHT 感測器庫

// ── Wi-Fi 設定 ─────────────────────────────────────────────
const char* SSID     = "mcugust";   // Wi-Fi 名稱（SSID）
const char* PASSWORD = "mcu18226";  // Wi-Fi 密碼

// ── DHT11 感測器設定 ────────────────────────────────────────
#define DHT_PIN  15        // DHT11 資料腳位：GPIO 15
#define DHT_TYPE DHT11     // 感測器型號：DHT11

DHT dht(DHT_PIN, DHT_TYPE); // 建立 DHT 物件

// ── Web Server（監聽 80 port）──────────────────────────────
WebServer server(80);      // 建立 HTTP 伺服器，Port 80（標準 HTTP）

// ── 感測器數值儲存 ──────────────────────────────────────────
float temperature = 0.0;   // 溫度（°C）
float humidity    = 0.0;   // 濕度（%）
unsigned long lastRead = 0; // 上次讀取感測器的時間戳

// ── Dashboard HTML 頁面 ────────────────────────────────────
// 使用 R"rawstring(...)rawstring" 原始字串，避免引號跳脫問題
// JavaScript 每 3 秒向 /data 取得最新 JSON，更新畫面數值
const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 環境監測</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      color: #fff;
    }
    h1 {
      font-size: 1.6rem;
      letter-spacing: 2px;
      margin-bottom: 8px;
      color: #e0e0e0;
    }
    .subtitle {
      font-size: 0.85rem;
      color: #7f8c8d;
      margin-bottom: 40px;
      letter-spacing: 1px;
    }
    .cards {
      display: flex;
      gap: 30px;
      flex-wrap: wrap;
      justify-content: center;
    }
    .card {
      background: rgba(255,255,255,0.07);
      border: 1px solid rgba(255,255,255,0.12);
      border-radius: 20px;
      padding: 40px 50px;
      text-align: center;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 32px rgba(0,0,0,0.3);
      transition: transform 0.2s;
      min-width: 200px;
    }
    .card:hover { transform: translateY(-4px); }
    .card-icon { font-size: 3rem; margin-bottom: 12px; }
    .card-label {
      font-size: 0.9rem;
      color: #95a5a6;
      letter-spacing: 2px;
      text-transform: uppercase;
      margin-bottom: 10px;
    }
    .card-value {
      font-size: 3.5rem;
      font-weight: 700;
      line-height: 1;
    }
    .card-unit {
      font-size: 1.4rem;
      font-weight: 300;
      color: #bdc3c7;
    }
    .temp .card-value { color: #ff6b6b; }
    .humi .card-value { color: #48dbfb; }
    .status-bar {
      margin-top: 40px;
      font-size: 0.8rem;
      color: #636e72;
      letter-spacing: 1px;
    }
    .dot {
      display: inline-block;
      width: 8px; height: 8px;
      border-radius: 50%;
      background: #00b894;
      margin-right: 6px;
      animation: pulse 1.5s infinite;
    }
    @keyframes pulse {
      0%,100% { opacity: 1; }
      50%      { opacity: 0.3; }
    }
  </style>
</head>
<body>
  <h1>🌡️ 環境監測 Dashboard</h1>
  <div class="subtitle">ESP32-S3 ・ DHT-11 ・ GPIO 15</div>

  <div class="cards">
    <div class="card temp">
      <div class="card-icon">🌡️</div>
      <div class="card-label">溫度</div>
      <div class="card-value"><span id="temp">--</span><span class="card-unit"> °C</span></div>
    </div>
    <div class="card humi">
      <div class="card-icon">💧</div>
      <div class="card-label">濕度</div>
      <div class="card-value"><span id="humi">--</span><span class="card-unit"> %</span></div>
    </div>
  </div>

  <div class="status-bar">
    <span class="dot"></span>
    每 3 秒自動更新 ・ 上次更新：<span id="ts">--</span>
  </div>

  <script>
    // 每 3 秒向 /data 請求最新數值並更新畫面
    function update() {
      fetch('/data')
        .then(r => r.json())
        .then(d => {
          document.getElementById('temp').textContent =
            isNaN(d.temperature) ? '--' : d.temperature.toFixed(1);
          document.getElementById('humi').textContent =
            isNaN(d.humidity) ? '--' : d.humidity.toFixed(1);
          document.getElementById('ts').textContent = new Date().toLocaleTimeString();
        })
        .catch(() => {
          document.getElementById('temp').textContent = '--';
          document.getElementById('humi').textContent = '--';
        });
    }
    update();                       // 頁面載入時立即取一次
    setInterval(update, 3000);      // 之後每 3000ms（3 秒）自動取一次
  </script>
</body>
</html>
)rawhtml";

// ── 路由：/ → 回傳 Dashboard HTML ──────────────────────────
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE); // 回傳 HTTP 200，Content-Type: text/html
}

// ── 路由：/data → 回傳 JSON 格式的溫濕度數值 ──────────────
void handleData() {
  // 組合 JSON 字串（格式：{"temperature":25.5,"humidity":60.2}）
  String json = "{";
  json += "\"temperature\":" + String(temperature, 1) + ","; // 溫度，保留 1 位小數
  json += "\"humidity\":"    + String(humidity,    1);       // 濕度，保留 1 位小數
  json += "}";
  server.send(200, "application/json", json); // 回傳 HTTP 200，Content-Type: JSON
}

// ── 路由：404 頁面 ──────────────────────────────────────────
void handleNotFound() {
  server.send(404, "text/plain", "404 Not Found"); // 找不到路由時回傳 404
}

// ============================================================
// setup() — 開機初始化
// ============================================================
void setup() {
  Serial.begin(115200);              // 啟動序列埠（鮑率 115200）
  Serial.println("\n=== ESP32-S3 DHT11 Dashboard 啟動中 ===");

  // ── 初始化 DHT11 感測器 ──────────────────────────────────
  dht.begin();                       // 啟動 DHT 感測器驅動
  Serial.println("[DHT] DHT11 初始化完成，接腳：GPIO 15");

  // ── 連接 Wi-Fi ───────────────────────────────────────────
  Serial.printf("[WiFi] 連接至：%s\n", SSID);
  WiFi.begin(SSID, PASSWORD);        // 開始連接 Wi-Fi

  int retries = 0;                   // 重試計數
  while (WiFi.status() != WL_CONNECTED) { // 等待連線成功
    delay(500);                      // 每 500ms 檢查一次
    Serial.print(".");
    retries++;
    if (retries > 30) {              // 超過 15 秒仍未連線則重啟
      Serial.println("\n[WiFi] 連線逾時，重新啟動...");
      ESP.restart();                 // 重啟 ESP32
    }
  }
  Serial.println();
  Serial.printf("[WiFi] 連線成功！IP 位址：http://%s\n",
    WiFi.localIP().toString().c_str()); // 印出 IP，用瀏覽器開啟此網址

  // ── 設定 Web Server 路由 ─────────────────────────────────
  server.on("/",      handleRoot);    // 根路由 → Dashboard HTML
  server.on("/data",  handleData);    // /data  → JSON 溫濕度數值
  server.onNotFound(handleNotFound);  // 其他路由 → 404

  server.begin();                    // 啟動 Web Server（開始監聽 Port 80）
  Serial.println("[Server] Web Server 已啟動");
  Serial.println("=== 請用瀏覽器開啟上方 IP 位址 ===\n");
}

// ============================================================
// loop() — 主循環
// ============================================================
void loop() {
  server.handleClient();             // 處理 HTTP 請求（必須在 loop 中持續呼叫）

  // 每 2 秒讀取一次感測器（DHT11 最快取樣間隔約 1 秒）
  if (millis() - lastRead >= 2000) {
    lastRead = millis();             // 更新上次讀取時間

    float t = dht.readTemperature(); // 讀取溫度（°C）
    float h = dht.readHumidity();    // 讀取濕度（%）

    if (isnan(t) || isnan(h)) {      // 若讀取失敗（回傳 NaN）則跳過
      Serial.println("[DHT] 讀取失敗，請確認接線");
    } else {
      temperature = t;               // 更新全域溫度變數
      humidity    = h;               // 更新全域濕度變數
      Serial.printf("[DHT] 溫度：%.1f °C  濕度：%.1f %%\n", temperature, humidity);
    }
  }
}
